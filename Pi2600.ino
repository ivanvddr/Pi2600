/*
 * Questo file fa parte del progetto Pi2600.
 *
 * Copyright (C) 2026 Ivan Vettori
 *
 * Questo file è opera originale dell'autore e viene rilasciato
 * sotto licenza GNU General Public License v3.0.
 * Puoi ridistribuirlo e/o modificarlo secondo i termini della GPLv3.
 *
 * Questo programma è distribuito nella speranza che sia utile,
 * ma SENZA ALCUNA GARANZIA; senza neppure la garanzia implicita
 * di COMMERCIABILITÀ o IDONEITÀ PER UN PARTICOLARE SCOPO.
 * Vedi la licenza GPLv3 per maggiori dettagli.
 *
 * Atari 2600 emulator - Raspberry Pi Pico (RP2040), arduino-pico core
 *  core0 : 6507 + TIA + RIOT + audio samples + framebuffer filling (non attende mai core1)
 *  core1 : framebuffer -> ILI9341 (TFT_eSPI + DMA), salta rendering frames quando troppo lento
 *  audio : MAX98357 via I2S (PIO). Audio pompato in sincrono con l'emulazione real time
 *
 *  Tools menu: Board = Raspberry Pi Pico, Optimize = -O3 (raccomandato)
 */
#include "config.h"
#include <SPI.h>
#include <SD.h>
#include <LittleFS.h>
#include <TFT_eSPI.h>
#if EMU_CLOCK_KHZ
#include <pico/stdlib.h>
#include <hardware/vreg.h>
#endif

TFT_eSPI tft = TFT_eSPI();

#include "atari.h"
#include "video.h"

static Atari atari;

static volatile bool emuRun = false;      // core0 -> core1
static volatile bool c1Idle = true;       // core1 -> core0

// ---------------------------------------------------------------------------
//  ROM browser data (SD su SPI1 and/or LittleFS in flash)
// ---------------------------------------------------------------------------
struct RomEntry { char name[44]; uint8_t src; };         // src 0 = SD, 1 = flash
static RomEntry roms[MAX_ROMS];
static int romCount = 0;
static bool sdOk = false, flashOk = false;

// ---------------------------------------------------------------------------
//  Input
// ---------------------------------------------------------------------------
struct Pad { bool up, down, left, right, fire, b; };
static Pad pad[2];

static inline bool pressed(int pin) { return digitalRead(pin) == LOW; }

static void inputInit() {
  const int pins[] = {PIN_J1_UP, PIN_J1_DOWN, PIN_J1_LEFT, PIN_J1_RIGHT, PIN_J1_FIRE, PIN_J1_B,
                      PIN_J2_UP, PIN_J2_DOWN, PIN_J2_LEFT, PIN_J2_RIGHT, PIN_J2_FIRE, PIN_J2_B};
  for (int p : pins) pinMode(p, INPUT_PULLUP);
}

static void pollInput() {
  pad[0] = {pressed(PIN_J1_UP), pressed(PIN_J1_DOWN), pressed(PIN_J1_LEFT), pressed(PIN_J1_RIGHT), pressed(PIN_J1_FIRE), pressed(PIN_J1_B)};
  pad[1] = {pressed(PIN_J2_UP), pressed(PIN_J2_DOWN), pressed(PIN_J2_LEFT), pressed(PIN_J2_RIGHT), pressed(PIN_J2_FIRE), pressed(PIN_J2_B)};
}

static void applyInput() {
  uint8_t a = 0xFF;
  if (pad[0].right) a &= ~0x80;  if (pad[0].left) a &= ~0x40;
  if (pad[0].down)  a &= ~0x20;  if (pad[0].up)   a &= ~0x10;
  if (pad[1].right) a &= ~0x08;  if (pad[1].left) a &= ~0x04;
  if (pad[1].down)  a &= ~0x02;  if (pad[1].up)   a &= ~0x01;
  atari.riot.inA = a;

  uint8_t b = 0x37;                                         // bits 0-2,4,5 = 1
  if (CONSOLE_COLOR)     b |= 0x08;
  if (DIFFICULTY_P0_A)   b |= 0x40;
  if (DIFFICULTY_P1_A)   b |= 0x80;
  if (pad[0].b) b &= ~0x01;                                 // RESET
  if (pad[1].b) b &= ~0x02;                                 // SELECT
  atari.riot.inB = b;

  atari.tia.setFire(0, pad[0].fire);
  atari.tia.setFire(1, pad[1].fire);
}

static bool hasRomExt(const char* n) {
  int l = strlen(n);
  if (l < 4) return false;
  const char* e = n + l - 4;
  return !strcasecmp(e, ".bin") || !strcasecmp(e, ".a26");
}

struct FsScanner {
  template <class FSX>
  static void scanFs(FSX& fs, uint8_t src) {
    File dir = fs.open(ROM_DIR, "r");                      
    if (!dir) return;
    File f = dir.openNextFile();
    while (f && romCount < MAX_ROMS) {
      if (!f.isDirectory()) {
        const char* n = f.name();
        const char* s = strrchr(n, '/'); if (s) n = s + 1;
        if (hasRomExt(n)) {
          strncpy(roms[romCount].name, n, sizeof(roms[0].name) - 1);
          roms[romCount].name[sizeof(roms[0].name) - 1] = 0;
          roms[romCount].src = src;
          romCount++;
        }
      }
      f.close();
      f = dir.openNextFile();
    }
    dir.close();
  }
};

static void scanRoms() {
  romCount = 0;
  if (sdOk) FsScanner::scanFs(SD, 0);
  if (flashOk) FsScanner::scanFs(LittleFS, 1);
  for (int i = 1; i < romCount; i++) {                    // insertion sort
    RomEntry t = roms[i]; int j = i - 1;
    while (j >= 0 && strcasecmp(roms[j].name, t.name) > 0) { roms[j + 1] = roms[j]; j--; }
    roms[j + 1] = t;
  }
}

static File bigFile;                                        // mantenuto aperto per ROM 3F trasmesse in streaming

static bool fetchBankFS(uint32_t off, uint8_t* dst, uint16_t len) {
  if (!bigFile || !bigFile.seek(off)) return false;
  return bigFile.read(dst, len) == len;
}

static bool loadRom(const RomEntry& e) {
  char path[80];
  snprintf(path, sizeof(path), "%s/%s", ROM_DIR, e.name);
  if (bigFile) bigFile.close();
  File f = (e.src == 0) ? SD.open(path, "r") : LittleFS.open(path, "r");
  if (!f) return false;
  uint32_t sz = f.size();
  if (sz == 0 || sz > CART_BIG_MAX) { f.close(); return false; }
  if (sz > CART_MAX_SIZE) {                                  // big ROM: presunte Tigervision 3F, streamed
    if (sz % 2048) { f.close(); return false; }
    bigFile = f;
    atari.cart.setupBig(sz, fetchBankFS);
    atari.reset();
    return true;
  }
  int n = f.read(atari.cart.rom, sz);
  f.close();
  if (n != (int)sz) return false;
  atari.cart.setup(sz);
  atari.reset();
  return true;
}

static void drawMenu(int sel, int top, const char* msg = nullptr) {
  const int rows = 12;
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  tft.drawString("Pi2600  -  @IV ATARI 2600 emulator", 8, 4);
  tft.drawFastHLine(0, 24, 320, TFT_DARKGREY);
  if (!romCount) {
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString("No ROM found in " ROM_DIR, 8, 40);
    tft.drawString("(.bin / .a26 on SD or flash)", 8, 60);
  }
  for (int i = 0; i < rows && top + i < romCount; i++) {
    int idx = top + i, y = 30 + i * 16;
    if (idx == sel) { tft.fillRect(0, y, 320, 16, TFT_NAVY); tft.setTextColor(TFT_YELLOW, TFT_NAVY); }
    else tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString(roms[idx].src ? "F" : "S", 6, y);
    tft.drawString(roms[idx].name, 24, y);
  }
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.drawString(msg ? msg : "J1: up/down + FIRE = start   B = rescan", 8, 224);
}

// si verifica quando una ROM è stata caricata
static void menuAndLoad() {
  scanRoms();
  int sel = 0, top = 0;
  drawMenu(sel, top);
  uint32_t lastNav = 0;
  for (;;) {
    pollInput();
    uint32_t now = millis();
    if (romCount && now - lastNav > 140 && (pad[0].up || pad[0].down)) {
      sel = (sel + (pad[0].down ? 1 : romCount - 1)) % romCount;
      if (sel < top) top = sel;
      if (sel >= top + 12) top = sel - 11;
      if (sel == 0) top = 0;
      lastNav = now;
      drawMenu(sel, top);
    }
    if (pad[0].b && now - lastNav > 300) { scanRoms(); sel = top = 0; lastNav = now; drawMenu(sel, top); }
    if (pad[0].fire && romCount) {
      while (pressed(PIN_J1_FIRE)) delay(5);               // do not leak the press into the game
      if (loadRom(roms[sel])) return;
      drawMenu(sel, top, "Load failed (size > 32K?)");
    }
    delay(10);
  }
}

// ---------------------------------------------------------------------------
//  Frame handling su core0
// ---------------------------------------------------------------------------
static uint32_t frameNo = 0;
static int      autoSkip = 0;
static uint32_t comboSince = 0;

static bool wantRender() {
#if FRAMESKIP > 0
  return (frameNo % (FRAMESKIP + 1)) == 0;
#elif FRAMESKIP < 0
  return (frameNo % (autoSkip + 1)) == 0;
#else
  return true;
#endif
}

static void startEmu() {
  tft.fillScreen(TFT_BLACK);
  frameNo = 0; autoSkip = 0; comboSince = 0;
  atari.tia.fbFirst = NTSC_FIRST_LINE; atari.tia.fbLines = NTSC_LINES;
  atari.tia.setTarget(fbMem[fbW], true);
  emuRun = true;
}

static void stopEmu() {
  emuRun = false;
  while (!c1Idle) delay(1);
}

static void onFrameEnd() {
  TIA& t = atari.tia;
  if (t.render) atari.tia.setTarget(fbPublish(t.fbLines), false);   // swap buffers, target settato sotto

  bool pal = t.lastLines > 290;
  t.fbFirst = pal ? PAL_FIRST_LINE : NTSC_FIRST_LINE;
  t.fbLines = pal ? PAL_LINES : NTSC_LINES;

#if FRAMESKIP < 0
  uint32_t w = g_audioWaitUs; g_audioWaitUs = 0;
  if (w < 800)       { if (autoSkip < AUTOSKIP_MAX) autoSkip++; }   // mai aspettato: siamo in ritardo
  else if (w > 4000) { if (autoSkip > 0) autoSkip--; }
#else
  g_audioWaitUs = 0;
#endif
  frameNo++;
  t.setTarget(t.fb, wantRender());

#if !AUDIO_ENABLE
  static uint32_t next = 0;                                         // fallback senza I2S
  uint32_t us = (uint32_t)((uint64_t)t.lastLines * 228 * 1000000ULL / TIA_CLOCK_HZ);
  while ((int32_t)(micros() - next) < 0) {}
  next = micros() + us;
#endif

  pollInput();
  applyInput();
  if (pad[0].b && pad[1].b) {
    if (!comboSince) comboSince = millis() | 1;
    else if (millis() - comboSince > MENU_COMBO_MS) { atari.flushAudio(); stopEmu(); comboSince = 0; emuRun = false; }
  } else comboSince = 0;

#if ATARI_DEBUG
  static uint32_t t0 = 0;
  if ((frameNo % 300) == 0) {
    uint32_t now = millis();
    Serial.printf("emu %.1f fps  skip=%d  lines=%d  cart=%s\n", 300000.0f / (now - t0), autoSkip, t.lastLines, atari.cart.typeName());
    t0 = now;
  }
#endif
}

void setup() {
#if EMU_CLOCK_KHZ
#if EMU_OVERVOLT
  if (EMU_CLOCK_KHZ > 133000) { vreg_set_voltage(VREG_VOLTAGE_1_20); delay(10); }
#endif
  set_sys_clock_khz(EMU_CLOCK_KHZ, true);                  // prima che I2S / SPI siano configurati
  uint32_t f = clock_get_hz(clk_sys); clock_configure(clk_peri, 0, CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS, f, f);
  __sync_synchronize();
#endif
#if ATARI_DEBUG
  Serial.begin(115200);
#endif
  inputInit();
  videoInit();

  tft.init();
  tft.setRotation(TFT_ROTATION);
  tft.setSwapBytes(false);                                   // palette é giá byte-swapped
  tft.initDMA();
  tft.fillScreen(TFT_BLACK);

  SPI1.setRX(PIN_SD_MISO); SPI1.setTX(PIN_SD_MOSI); SPI1.setSCK(PIN_SD_SCK);
  sdOk = SD.begin(PIN_SD_CS, SPI1);
  flashOk = LittleFS.begin();

  audioBegin();
}

void loop() {
  if (!emuRun) {
    menuAndLoad();
    startEmu();
    return;
  }
  atari.runFrame();
  onFrameEnd();
}

// core1: display only
void setup1() {}

void loop1() {
  if (!emuRun) { c1Idle = true; delay(2); return; }
  c1Idle = false;
  int lines;
  const uint8_t* f = fbAcquire(&lines);
  if (f) presentFrame(f, lines);
  else delayMicroseconds(300);
}
