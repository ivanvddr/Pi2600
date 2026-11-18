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
 *  TIA: video con precisione a livello di ciclo di clock (renderizzato in modo "lazy" tramite span, solo quando un registro cambia o alla fine di una riga), oggetti basati sulla posizione, 2 canali audio
 */
#pragma once
#include <stdint.h>
#include <string.h>
#include "config.h"

#define TIA_LINE_CLOCKS 228
#define TIA_HBLANK       68

// tabelle di ricerca a livello di file (le variabili locali statiche all'interno di funzioni contrassegnate come RAMFUNC compromettono l'attributo di sezione)
static const uint8_t kCopyOffs[4]   = {0, 16, 32, 64};
static const uint8_t kCopyMask[8]   = {1, 3, 5, 7, 9, 1, 13, 1};
static const uint8_t kSizeShift[8]  = {0, 0, 0, 0, 0, 1, 0, 2};

class TIA {
public:
  // ---- video target (owned by the sketch) ---------------------------------
  uint8_t* fb = nullptr;           // FB_W * FB_H palette indices (0..127)
  int  fbFirst = NTSC_FIRST_LINE;
  int  fbLines = NTSC_LINES;
  bool render  = false;            // renderizzo il frame corrente? (le collisioni sono sempre calcolate)

  // ---- timing --------------------------------------------------------------
  int  clk = 0;                    // colour clock in line, sempre un multiplo di 3
  int  line = 0;                   // scanline dalla fine del VSYNC
  int  lastLines = 262;
  bool wsync = false;
  bool frameEnded = false;

  uint8_t cx[8];

  void setTarget(uint8_t* buf, bool doRender) { fb = buf; render = doRender; setRow(); }

  void reset() {
    memset(cx, 0, sizeof(cx));
    vsync = vblank = colupf = colubk = ctrlpf = pf0 = pf1 = pf2 = 0;
    pfMask = 0; enabl = enablOld = vdelbl = 0;
    for (int i = 0; i < 2; i++) { nusiz[i] = colup[i] = refp[i] = grp[i] = grpOld[i] = enam[i] = vdelp[i] = resmp[i] = 0; }
    for (int i = 0; i < 5; i++) { pos[i] = 0; hm[i] = 0; }
    ch[0] = AudioCh(); ch[1] = AudioCh();
    clk = 0; line = 0; rendered = 0; wsync = false; frameEnded = false;
    vsyncFell = false; hmoveBlank = false; hmoveBlankNext = false; dirty = 0x3F;
    inpt[0] = inpt[1] = latch[0] = latch[1] = 0x80;
    setRow();
  }

  void setFire(int p, bool pressed) {
    if (pressed) { inpt[p] = 0x00; latch[p] = 0x00; } else inpt[p] = 0x80;
  }

  // ---- audio ---------------------------------------------------------------
  RAMFUNC(tia_audioTick) void audioTick() { chTick(ch[0]); chTick(ch[1]); }
  inline int audioMix() const { return (ch[0].bit ? ch[0].v : 0) + (ch[1].bit ? ch[1].v : 0); }

  // ---- bus -----------------------------------------------------------------
  RAMFUNC(tia_read) uint8_t read(uint8_t r) {
    r &= 0x0F;
    if (r < 8) { catchUp(); return cx[r]; }
    if (r == 0x0C) return (vblank & 0x40) ? latch[0] : inpt[0];
    if (r == 0x0D) return (vblank & 0x40) ? latch[1] : inpt[1];
    return 0;
  }

  RAMFUNC(tia_write) void write(uint8_t r, uint8_t v) {
    if (r >= 0x15 && r <= 0x1A) {                       // audio: non necessario recuperare il video
      switch (r) {
        case 0x15: ch[0].c = v & 15; break;  case 0x16: ch[1].c = v & 15; break;
        case 0x17: ch[0].f = v & 31; break;  case 0x18: ch[1].f = v & 31; break;
        case 0x19: ch[0].v = v & 15; break;  case 0x1A: ch[1].v = v & 15; break;
      }
      return;
    }
    if (r == 0x02) { wsync = true; return; }
    catchUp();
    int hpos = clk - TIA_HBLANK;
    switch (r) {
      case 0x00: if ((vsync & 2) && !(v & 2)) vsyncFell = true; vsync = v; break;
      case 0x01: vblank = v; if (!(v & 0x40)) latch[0] = latch[1] = 0x80; break;
      case 0x04: nusiz[0] = v; dirty |= P0 | M0; break;
      case 0x05: nusiz[1] = v; dirty |= P1 | M1; break;
      case 0x06: colup[0] = v; break;
      case 0x07: colup[1] = v; break;
      case 0x08: colupf = v; break;
      case 0x09: colubk = v; break;
      case 0x0A: ctrlpf = v; dirty |= BL | PF; break;
      case 0x0B: refp[0] = v & 8; dirty |= P0; break;
      case 0x0C: refp[1] = v & 8; dirty |= P1; break;
      case 0x0D: pf0 = v; buildPFbits(); break;
      case 0x0E: pf1 = v; buildPFbits(); break;
      case 0x0F: pf2 = v; buildPFbits(); break;
      case 0x10: pos[0] = hpos < 0 ? 3 : (hpos + 5) % 160; dirty |= P0 | M0; break;
      case 0x11: pos[1] = hpos < 0 ? 3 : (hpos + 5) % 160; dirty |= P1 | M1; break;
      case 0x12: pos[2] = hpos < 0 ? 2 : (hpos + 4) % 160; dirty |= M0; break;
      case 0x13: pos[3] = hpos < 0 ? 2 : (hpos + 4) % 160; dirty |= M1; break;
      case 0x14: pos[4] = hpos < 0 ? 2 : (hpos + 4) % 160; dirty |= BL; break;
      case 0x1B: grp[0] = v; grpOld[1] = grp[1]; dirty |= P0 | P1; break;
      case 0x1C: grp[1] = v; grpOld[0] = grp[0]; enablOld = enabl; dirty |= P0 | P1 | BL; break;
      case 0x1D: enam[0] = v; dirty |= M0; break;
      case 0x1E: enam[1] = v; dirty |= M1; break;
      case 0x1F: enabl = v; dirty |= BL; break;
      case 0x20: case 0x21: case 0x22: case 0x23: case 0x24: hm[r - 0x20] = v; break;
      case 0x25: vdelp[0] = v & 1; dirty |= P0; break;
      case 0x26: vdelp[1] = v & 1; dirty |= P1; break;
      case 0x27: vdelbl = v & 1; dirty |= BL; break;
      case 0x28: if ((resmp[0] & 2) && !(v & 2)) pos[2] = lockedPos(0); resmp[0] = v; dirty |= M0; break;
      case 0x29: if ((resmp[1] & 2) && !(v & 2)) pos[3] = lockedPos(1); resmp[1] = v; dirty |= M1; break;
      case 0x2A:                                       // HMOVE: il movimento viene sempre applicato
        for (int i = 0; i < 5; i++) {
          int left = ((hm[i] >> 4) ^ 8) - 8;           // +7..-8 (positivo = movimento a sx)
          pos[i] = ((pos[i] - left) % 160 + 160) % 160;
        }
        dirty = 0x3F;
        if (clk < TIA_HBLANK) hmoveBlank = (HMOVE_BLANK != 0);          // comb: 8 black pixels
        else if (clk >= 216)  hmoveBlankNext = (HMOVE_BLANK != 0);     
        break;
      case 0x2B: memset(hm, 0, sizeof(hm)); break;
      case 0x2C: memset(cx, 0, sizeof(cx)); break;
      default: break;
    }
  }

  // chiamato dal sistema quando clk raggiunge 228
  RAMFUNC(tia_endOfLine) void endOfLine() {
    renderSpan(rendered, TIA_LINE_CLOCKS);
    clk -= TIA_LINE_CLOCKS;
    rendered = 0; hmoveBlank = hmoveBlankNext; hmoveBlankNext = false; wsync = false;
    if (vsyncFell) {
      vsyncFell = false; lastLines = line + 1; line = 0; frameEnded = true;
    } else if (++line >= TIA_MAX_LINES) {
      lastLines = line; line = 0; frameEnded = true;
    }
    setRow();
  }

private:
  enum { P0 = 1, P1 = 2, M0 = 4, M1 = 8, BL = 16, PF = 32 };

  struct AudioCh {
    uint8_t c = 0, f = 0, v = 0;
    uint8_t cnt = 0, p4 = 0x0F, p5 = 0x1F, d31 = 0, d3 = 0, bit = 0;
    uint16_t p9 = 0x1FF;
  };
  AudioCh ch[2];

  uint8_t vsync = 0, vblank = 0, nusiz[2], colup[2], colupf = 0, colubk = 0, ctrlpf = 0, refp[2];
  uint8_t pf0 = 0, pf1 = 0, pf2 = 0;
  uint32_t pfMask = 0;
  uint8_t grp[2], grpOld[2], enam[2], enabl = 0, enablOld = 0, vdelp[2], vdelbl = 0, resmp[2];
  uint8_t hm[5];
  int     pos[5];                     // 0=P0 1=P1 2=M0 3=M1 4=BL (pixel del bordo sinistro)
  uint8_t inpt[2] = {0x80, 0x80}, latch[2] = {0x80, 0x80};
  uint8_t* row = nullptr;
  int  rendered = 0;
  bool vsyncFell = false, hmoveBlank = false, hmoveBlankNext = false;

  // 160-bit pixel maschera (5 words, bit0 of word0 = pixel più a sinistra), rebuilt lazily (su richiesta)
  uint32_t obj[5][5];               // P0 P1 M0 M1 BL
  uint32_t pfFull[5];
  uint8_t  dirty = 0x3F;

  void setRow() {
    row = (render && fb && line >= fbFirst && line < fbFirst + fbLines && (line - fbFirst) < FB_H)
              ? fb + (line - fbFirst) * FB_W : nullptr;
  }

  void buildPFbits() {
    uint32_t m = 0;
    for (int i = 0; i < 4; i++) if (pf0 & (0x10 << i)) m |= 1u << i;
    for (int i = 0; i < 8; i++) if (pf1 & (0x80 >> i)) m |= 1u << (4 + i);
    for (int i = 0; i < 8; i++) if (pf2 & (1 << i))    m |= 1u << (12 + i);
    pfMask = m; dirty |= PF;
  }

  static inline void orAt(uint32_t* m, uint32_t pat, int start) {      // pat bit0 = pixel più a sinistra
    uint64_t v = (uint64_t)pat << (start & 31);
    int i = start >> 5;
    m[i] |= (uint32_t)v;
    uint32_t hi = (uint32_t)(v >> 32);
    if (hi) m[i == 4 ? 0 : i + 1] |= hi;                               
  }

  void buildPlayer(int p) {
    uint32_t* m = obj[p]; memset(m, 0, 20);
    uint8_t g = vdelp[p] ? grpOld[p] : grp[p];
    if (!g) return;
    uint8_t n = nusiz[p] & 7;
    int sc = 1 << kSizeShift[n];
    uint32_t run = (1u << sc) - 1, pat = 0;
    for (int k = 0; k < 8; k++) if ((g >> (refp[p] ? k : 7 - k)) & 1) pat |= run << (k * sc);
    uint8_t cm = kCopyMask[n];
    for (int i = 0; i < 4; i++) if (cm & (1 << i)) orAt(m, pat, (pos[p] + kCopyOffs[i]) % 160);
  }

  void buildMissile(int p) {
    uint32_t* m = obj[2 + p]; memset(m, 0, 20);
    if (!(enam[p] & 2) || (resmp[p] & 2)) return;
    uint32_t pat = (1u << (1 << ((nusiz[p] >> 4) & 3))) - 1;
    uint8_t cm = kCopyMask[nusiz[p] & 7];
    for (int i = 0; i < 4; i++) if (cm & (1 << i)) orAt(m, pat, (pos[2 + p] + kCopyOffs[i]) % 160);
  }

  void buildBall() {
    uint32_t* m = obj[4]; memset(m, 0, 20);
    if (!(((vdelbl ? enablOld : enabl)) & 2)) return;
    orAt(m, (1u << (1 << ((ctrlpf >> 4) & 3))) - 1, pos[4]);
  }

  void rebuild() {
    if (dirty & P0) buildPlayer(0);
    if (dirty & P1) buildPlayer(1);
    if (dirty & BL) buildBall();
    if (dirty & PF) {
      memset(pfFull, 0, 20);
      const bool refl = ctrlpf & 1;
      for (int i = 0; i < 20; i++) {
        if ((pfMask >> i) & 1) orAt(pfFull, 0xF, i * 4);
        if ((pfMask >> (refl ? 19 - i : i)) & 1) orAt(pfFull, 0xF, 80 + i * 4);
      }
    }
    if (dirty & M0) buildMissile(0);
    if (dirty & M1) buildMissile(1);
    dirty = 0;
  }

  int lockedPos(int p) const {
    uint8_t m = nusiz[p] & 7;
    return (pos[p] + (m == 5 ? 6 : m == 7 ? 10 : 3)) % 160;
  }

  RAMFUNC(tia_catchUp) void catchUp() {
    if (clk > rendered) { renderSpan(rendered, clk); rendered = clk; }
  }

  // ---- audio core -----------------------------------------------------------
  static inline uint8_t lfsr4(uint8_t& s) { uint8_t f = ((s >> 3) ^ (s >> 2)) & 1; s = ((s << 1) | f) & 0x0F; return s & 1; }
  static inline uint8_t lfsr5(uint8_t& s) { uint8_t f = ((s >> 4) ^ (s >> 2)) & 1; s = ((s << 1) | f) & 0x1F; return s & 1; }
  static inline uint8_t lfsr9(uint16_t& s) { uint16_t f = ((s >> 8) ^ (s >> 4)) & 1; s = ((s << 1) | f) & 0x1FF; return s & 1; }

  RAMFUNC(tia_chTick) void chTick(AudioCh& a) {
    if (a.cnt < a.f) { a.cnt++; return; }              // dividere per (AUDF+1)
    a.cnt = 0;
    switch (a.c) {
      case 0: case 11: a.bit = 1; break;
      case 1:  a.bit = lfsr4(a.p4); break;
      case 2:  if (++a.d31 >= 31) { a.d31 = 0; a.bit = lfsr4(a.p4); } break;
      case 3:  if (lfsr5(a.p5)) a.bit = lfsr4(a.p4); break;
      case 4: case 5: a.bit ^= 1; break;
      case 6: case 10: if (++a.d31 >= 31) a.d31 = 0; a.bit = a.d31 >= 18; break;
      case 7: case 9:  a.bit = lfsr5(a.p5); break;
      case 8:  a.bit = lfsr9(a.p9); break;
      case 12: case 13: if (++a.d3 >= 3) { a.d3 = 0; a.bit ^= 1; } break;
      case 14: if (++a.d3 >= 3) { a.d3 = 0; if (++a.d31 >= 31) a.d31 = 0; a.bit = a.d31 >= 18; } break;
      default: if (++a.d3 >= 3) { a.d3 = 0; a.bit = lfsr5(a.p5); } break;   // 15
    }
  }

  // ---- video core -----------------------------------------------------------
  RAMFUNC(tia_renderSpan) void renderSpan(int c0, int c1) {
    if (c0 < TIA_HBLANK) c0 = TIA_HBLANK;
    int x0 = c0 - TIA_HBLANK, x1 = c1 - TIA_HBLANK;
    if (x1 > 160) x1 = 160;
    if (x1 <= x0) return;
    uint8_t* out = row;

    if (vblank & 2) { if (out) memset(out + x0, 0, x1 - x0); return; }

    int blank = hmoveBlank ? 8 : 0;
    if (x0 < blank) {
      int e = x1 < blank ? x1 : blank;
      if (out) memset(out + x0, 0, e - x0);
      x0 = e;
      if (x0 >= x1) return;
    }
    if (dirty) rebuild();

    const bool prio = ctrlpf & 4, score = ctrlpf & 2;
    const uint8_t cP0 = colup[0] >> 1, cP1 = colup[1] >> 1, cPF = colupf >> 1, cBK = colubk >> 1;

    for (int w = x0 >> 5; w <= ((x1 - 1) >> 5); w++) {
      int base = w << 5;
      int lo = (x0 > base ? x0 : base) - base;
      int hi = (x1 < base + 32 ? x1 : base + 32) - base;
      uint32_t valid = (hi >= 32 ? 0xFFFFFFFFu : ((1u << hi) - 1)) & ~((1u << lo) - 1);
      uint32_t p0 = obj[0][w] & valid, p1 = obj[1][w] & valid;
      uint32_t m0 = obj[2][w] & valid, m1 = obj[3][w] & valid;
      uint32_t bl = obj[4][w] & valid, pf = pfFull[w] & valid;

      if (p0 | p1 | m0 | m1 | bl) {
        if (m0 & p1) cx[0] |= 0x80;
        if (m0 & p0) cx[0] |= 0x40;
        if (m1 & p0) cx[1] |= 0x80;
        if (m1 & p1) cx[1] |= 0x40;
        if (p0 & pf) cx[2] |= 0x80;
        if (p0 & bl) cx[2] |= 0x40;
        if (p1 & pf) cx[3] |= 0x80;
        if (p1 & bl) cx[3] |= 0x40;
        if (m0 & pf) cx[4] |= 0x80;
        if (m0 & bl) cx[4] |= 0x40;
        if (m1 & pf) cx[5] |= 0x80;
        if (m1 & bl) cx[5] |= 0x40;
        if (bl & pf) cx[6] |= 0x80;
        if (p0 & p1) cx[7] |= 0x80;
        if (m0 & m1) cx[7] |= 0x40;
      }
      if (!out) continue;

      uint8_t* o = out + base;
      uint32_t pl0 = p0 | m0, pl1 = p1 | m1, fld = pf | bl;
      if (!(pl0 | pl1 | fld)) { memset(o + lo, cBK, hi - lo); continue; }
      for (int b = lo; b < hi; b++) {
        uint32_t bit = 1u << b;
        uint8_t cpf = (score && (pf & bit)) ? ((base + b) < 80 ? cP0 : cP1) : cPF;
        uint8_t col;
        if (prio) col = (fld & bit) ? cpf : (pl0 & bit) ? cP0 : (pl1 & bit) ? cP1 : cBK;
        else      col = (pl0 & bit) ? cP0 : (pl1 & bit) ? cP1 : (fld & bit) ? cpf : cBK;
        o[b] = col;
      }
    }
  }
};
