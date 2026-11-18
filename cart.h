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
#include <stdint.h>
#include <string.h>
#include "config.h"

enum CartType : uint8_t { CT_2K, CT_4K, CT_F8, CT_F6, CT_F4, CT_FA, CT_E0, CT_3F, CT_EF };

class Cart {
public:
  uint8_t  rom[CART_MAX_SIZE];
  uint32_t size = 0;
  CartType type = CT_4K;
  bool     sc = false;          // SuperChip RAM (F8/F6/F4)
  bool     is3F = false;
  uint8_t  ram[256];
  uint16_t ramSize = 0;
  uint8_t* slot[4];             // quattro finestre da 1 KB nella ROM[]
  uint16_t bank = 0, nBanks = 1;

  // ---- big 3F ROMs (up to CART_BIG_MAX): banchi trasmessi in streaming su richiesta, ROM utilizzata come cache ----
  typedef bool (*FetchFn)(uint32_t offset, uint8_t* dst, uint16_t len);
  static const int BIG_CACHE = 31;            // voci da 2K commutabili; voce 31 = ultimo banco fisso
  bool    big = false;
  FetchFn fetch = nullptr;
  int16_t tag[BIG_CACHE];
  uint32_t used[BIG_CACHE];
  uint32_t useClock = 0;
  int     cur = 0;
  uint8_t  e0[3] = {0, 1, 2};
  uint16_t hotLo = 0xFFFF, hotHi = 0;

  const char* typeName() const {
    switch (type) {
      case CT_2K: return "2K";
      case CT_4K: return "4K";
      case CT_F8: return sc ? "F8SC" : "F8";
      case CT_F6: return sc ? "F6SC" : "F6";
      case CT_F4: return sc ? "F4SC" : "F4";
      case CT_FA: return "FA";
      case CT_E0: return "E0";
      case CT_3F: return big ? "3F(stream)" : "3F";
      case CT_EF: return sc ? "EFSC" : "EF";
    }
    return "?";
  }

  // call after the loader filled rom[0..sz)
  void setup(uint32_t sz) {
    if (sz > CART_MAX_SIZE) sz = CART_MAX_SIZE;
    size = sz;
    uint32_t eff = sz <= 2048 ? 2048 : sz <= 4096 ? 4096 : sz <= 8192 ? 8192 :
                   sz <= 12288 ? 12288 : sz <= 16384 ? 16384 : sz <= 32768 ? 32768 : 65536;
    if (eff > sz) memset(rom + sz, 0xFF, eff - sz);
    size = eff;

    int e0hits = count3(0x8D, 0xE0, 0x1F) + count3(0xAD, 0xE0, 0x1F) +
                 count3(0x8D, 0xE0, 0xFF) + count3(0xAD, 0xE0, 0xFF);
    int t3f = count2(0x85, 0x3F);
    sc = false; is3F = false; big = false;
    int efHits = countEF();

    switch (eff) {
      case 2048:  type = CT_2K; break;
      case 4096:  type = CT_4K; break;
      case 8192:  type = e0hits ? CT_E0 : (t3f >= 2 ? CT_3F : CT_F8); break;
      case 12288: type = CT_FA; break;
      case 16384: type = (t3f >= 2) ? CT_3F : CT_F6; break;
      case 32768: type = (t3f >= 2) ? CT_3F : CT_F4; break;
      default:    type = (efHits || t3f < 2) ? CT_EF : CT_3F; break;
    }
    if (type == CT_F8 || type == CT_F6 || type == CT_F4 || type == CT_EF) sc = detectSC(eff / 4096);
    is3F = (type == CT_3F);
    reset();
  }

  // 3F ROM larger than rom[]: total = file size, f reads bytes from the file
  void setupBig(uint32_t total, FetchFn f) {
    big = true; fetch = f; type = CT_3F; is3F = true; sc = false;
    size = total; nBanks = total / 2048;
    reset();
  }

  void reset() {
    memset(ram, 0, sizeof(ram));
    ramSize = 0; hotLo = 0xFFFF; hotHi = 0;
    switch (type) {
      case CT_2K: case CT_4K: nBanks = 1; break;
      case CT_F8: nBanks = 2; hotLo = 0xFF8; hotHi = 0xFF9; break;
      case CT_F6: nBanks = 4; hotLo = 0xFF6; hotHi = 0xFF9; break;
      case CT_F4: nBanks = 8; hotLo = 0xFF4; hotHi = 0xFFB; break;
      case CT_FA: nBanks = 3; hotLo = 0xFF8; hotHi = 0xFFA; ramSize = 256; break;
      case CT_E0: nBanks = 8; hotLo = 0xFE0; hotHi = 0xFF7; break;
      case CT_3F: nBanks = size / 2048; break;
      case CT_EF: nBanks = 16; hotLo = 0xFE0; hotHi = 0xFEF; break;
    }
    if (sc) ramSize = 128;
    bank = (type == CT_3F || type == CT_E0) ? 0 : nBanks - 1;
    e0[0] = 0; e0[1] = 1; e0[2] = 2;
    if (big) {
      for (int i = 0; i < BIG_CACHE; i++) { tag[i] = -1; used[i] = 0; }
      useClock = 0;
      if (!fetch || !fetch((uint32_t)(nBanks - 1) * 2048, rom + BIG_CACHE * 2048, 2048)) memset(rom + BIG_CACHE * 2048, 0xFF, 2048);
      bank = 0; selectBig(0);
    }
    updateSlots();
  }

  RAMFUNC(cart_read) uint8_t read(uint16_t a) {
    a &= 0xFFF;
    if (a >= hotLo && a <= hotHi) hotspot(a);
    if (ramSize && a >= ramSize && a < (uint16_t)(ramSize * 2)) return ram[a - ramSize];
    return slot[a >> 10][a & 0x3FF];
  }

  RAMFUNC(cart_write) void write(uint16_t a, uint8_t v) {
    a &= 0xFFF;
    if (a >= hotLo && a <= hotHi) hotspot(a);
    if (ramSize && a < ramSize) ram[a] = v;
  }

  // Tigervision: a write to $3F selects the 2K bank mapped at $1000
  RAMFUNC(cart_write3F) void write3F(uint8_t v) {
    bank = v % nBanks;
    if (big) selectBig(bank);
    updateSlots();
  }

private:
  int count2(uint8_t a, uint8_t b) const {
    int n = 0;
    for (uint32_t i = 0; i + 1 < size; i++) if (rom[i] == a && rom[i + 1] == b) n++;
    return n;
  }
  int count3(uint8_t a, uint8_t b, uint8_t c) const {
    int n = 0;
    for (uint32_t i = 0; i + 2 < size; i++) if (rom[i] == a && rom[i + 1] == b && rom[i + 2] == c) n++;
    return n;
  }
  int countEF() const {
    int n = 0;
    for (uint32_t i = 0; i + 2 < size; i++)
      if ((rom[i] == 0xAD || rom[i] == 0x8D || rom[i] == 0xBD || rom[i] == 0x2C) &&
          rom[i + 1] >= 0xE0 && rom[i + 1] <= 0xEF && (rom[i + 2] == 0x1F || rom[i + 2] == 0xFF)) n++;
    return n;
  }
  void selectBig(uint16_t b) {
    int i = -1;
    for (int k = 0; k < BIG_CACHE; k++) if (tag[k] == (int16_t)b) { i = k; break; }
    if (i < 0) {                                   // miss: LRU victim, load 2 KB (blocking, a few ms)
      uint32_t best = 0xFFFFFFFFu;
      for (int k = 0; k < BIG_CACHE; k++) {
        if (tag[k] < 0) { i = k; break; }
        if (used[k] < best) { best = used[k]; i = k; }
      }
      if (!fetch || !fetch((uint32_t)b * 2048, rom + i * 2048, 2048)) memset(rom + i * 2048, 0xFF, 2048);
      tag[i] = (int16_t)b;
    }
    used[i] = ++useClock; cur = i;
  }
  bool detectSC(int banks) const {
    for (int b = 0; b < banks; b++) {
      const uint8_t* p = rom + b * 4096;
      for (int i = 1; i < 256; i++) if (p[i] != p[0]) return false;
    }
    return true;
  }

  void hotspot(uint16_t a) {
    switch (type) {
      case CT_F8: case CT_FA: bank = a - 0xFF8; break;
      case CT_EF: bank = a - 0xFE0; break;
      case CT_F6: bank = a - 0xFF6; break;
      case CT_F4: bank = a - 0xFF4; break;
      case CT_E0:
        if (a <= 0xFE7) e0[0] = a - 0xFE0;
        else if (a <= 0xFEF) e0[1] = a - 0xFE8;
        else e0[2] = a - 0xFF0;
        break;
      default: return;
    }
    updateSlots();
  }

  void updateSlots() {
    switch (type) {
      case CT_2K:
        for (int i = 0; i < 4; i++) slot[i] = rom + (i & 1) * 1024;
        break;
      case CT_E0:
        for (int i = 0; i < 3; i++) slot[i] = rom + (e0[i] & 7) * 1024;
        slot[3] = rom + 7 * 1024;
        break;
      case CT_3F: {
        uint8_t* lo = rom + (uint32_t)(big ? cur : bank) * 2048;
        uint8_t* hi = rom + (uint32_t)(big ? BIG_CACHE : nBanks - 1) * 2048;
        slot[0] = lo; slot[1] = lo + 1024; slot[2] = hi; slot[3] = hi + 1024;
        break; }
      default: {
        uint8_t* base = rom + (uint32_t)bank * 4096;
        for (int i = 0; i < 4; i++) slot[i] = base + i * 1024;
        break; }
    }
  }
};
