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
 */
#pragma once
#include <string.h>
#include <TFT_eSPI.h>
#include <hardware/sync.h>
#include "config.h"
#include "palette.h"

extern TFT_eSPI tft;

// ---- triple buffer: core0 (writer) non attende mai core1 (reader) ----------------
static uint8_t  fbMem[3][FB_W * FB_H];
static int      fbLinesOf[3] = {NTSC_LINES, NTSC_LINES, NTSC_LINES};
static volatile int  fbW = 0, fbM = 1, fbR = 2;
static volatile bool fbFresh = false;
static spin_lock_t*  fbLock;
static volatile uint32_t presentCount = 0;   // frame effettivamente inviati al pannello

static uint16_t lineBuf[2][320 * ROWS_PER_CHUNK] __attribute__((aligned(4)));
static uint16_t rowMap[240];
static int      rowMapLines = 0;

static inline void videoInit() {
  fbLock = spin_lock_init(spin_lock_claim_unused(true));
  memset(fbMem, 0, sizeof(fbMem));
  buildPalette();
}

// core0: consegna il frame completato, ottiene il buffer di back successivo (alcuni cicli sotto spinlock)
static inline uint8_t* fbPublish(int lines) {
  fbLinesOf[fbW] = lines;
  uint32_t s = spin_lock_blocking(fbLock);
  int t = fbW; fbW = fbM; fbM = t; fbFresh = true;
  spin_unlock(fbLock, s);
  return fbMem[fbW];
}

// core1: frame più recente o nullptr
static inline const uint8_t* fbAcquire(int* lines) {
  uint32_t s = spin_lock_blocking(fbLock);
  bool f = fbFresh;
  if (f) { int t = fbR; fbR = fbM; fbM = t; fbFresh = false; }
  spin_unlock(fbLock, s);
  if (!f) return nullptr;
  *lines = fbLinesOf[fbR];
  return fbMem[fbR];
}

// core1: 160xN indexed -> 320x240 RGB565, convertito a blocchi mentre quello precedente è in DMA
static void presentFrame(const uint8_t* src, int lines) {
  if (lines != rowMapLines) {
    for (int y = 0; y < 240; y++) rowMap[y] = (y * lines) / 240;
    rowMapLines = lines;
  }
  tft.startWrite();
  tft.setAddrWindow(0, 0, 320, 240);
  int b = 0;
  for (int y0 = 0; y0 < 240; y0 += ROWS_PER_CHUNK) {
    uint32_t* dst = (uint32_t*)lineBuf[b];
    int prevSrc = -1;
    for (int r = 0; r < ROWS_PER_CHUNK; r++) {
      int sy = rowMap[y0 + r];
      if (sy == prevSrc) {                                   // riga duplicata: copia semplice
        memcpy(dst, dst - 160, 320 * 2);
      } else {
        const uint8_t* s = src + sy * FB_W;
        for (int x = 0; x < FB_W; x++) { uint32_t c = pal565[s[x]]; dst[x] = c | (c << 16); }
        prevSrc = sy;
      }
      dst += 160;
    }
    tft.dmaWait();
    tft.pushPixelsDMA(lineBuf[b], 320 * ROWS_PER_CHUNK);
    b ^= 1;
  }
  tft.dmaWait();
  tft.endWrite();
}
