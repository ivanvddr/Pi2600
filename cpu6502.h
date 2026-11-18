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
#include "config.h"

// ============================================================================
//  Core NMOS 6502 (6507 nell'Atari: 13 linee di indirizzo, assenza di IRQ/NMI).
//  Ogni accesso al bus avviene tramite BUS::read / BUS::write, che fanno avanzare
//  il resto del sistema di un ciclo CPU; si tratta di un'implementazione accurata
//  a livello di ciclo di bus, necessaria per la tecnica del "race the beam"
//  (sincronizzazione con il fascio elettronico) con il TIA. Include letture/scritture
//  fittizie (dummy), la modalità decimale e gli "opcode" non documentati (illegal
//  opcodes) di uso comune.
// ============================================================================
static const uint8_t kBranchFlag[4] = {0x80, 0x40, 0x01, 0x02};   // N V C Z

template <class BUS>
class CPU6502 {
public:
  BUS* bus = nullptr;
  uint16_t pc = 0;
  uint8_t a = 0, x = 0, y = 0, sp = 0xFD, p = 0x24;
  bool jammed = false;

  enum : uint8_t { FC = 0x01, FZ = 0x02, FI = 0x04, FD = 0x08, FB = 0x10, FU = 0x20, FV = 0x40, FN = 0x80 };

  void reset() {
    a = x = y = 0; sp = 0xFD; p = FI | FU; jammed = false;
    uint16_t l = rd(0xFFFC), h = rd(0xFFFD);
    pc = l | (h << 8);
  }

  RAMFUNC(cpu_step) void step() {
    if (jammed) { bus->idle(); return; }
    uint8_t op = fetch();
    switch (op) {
      case 0x00: fetch(); push(pc >> 8); push(pc & 0xFF); push(p | FB | FU); p |= FI;
                 { uint16_t l = rd(0xFFFE), h = rd(0xFFFF); pc = l | (h << 8); } return;
      case 0x20: { uint16_t l = fetch(); rd(0x100 | sp); push(pc >> 8); push(pc & 0xFF); uint16_t h = fetch(); pc = l | (h << 8); } return;
      case 0x40: rd(pc); rd(0x100 | sp); p = (pull() & ~FB) | FU; { uint16_t l = pull(), h = pull(); pc = l | (h << 8); } return;
      case 0x60: rd(pc); rd(0x100 | sp); { uint16_t l = pull(), h = pull(); pc = l | (h << 8); } rd(pc); pc++; return;
      case 0x4C: { uint16_t l = fetch(), h = fetch(); pc = l | (h << 8); } return;
      case 0x6C: { uint16_t l = fetch(), h = fetch(); uint16_t ptr = l | (h << 8);
                   uint16_t lo = rd(ptr), hi = rd((ptr & 0xFF00) | ((ptr + 1) & 0xFF)); pc = lo | (hi << 8); } return;
      case 0x08: rd(pc); push(p | FB | FU); return;
      case 0x28: rd(pc); rd(0x100 | sp); p = (pull() & ~FB) | FU; return;
      case 0x48: rd(pc); push(a); return;
      case 0x68: rd(pc); rd(0x100 | sp); a = pull(); setNZ(a); return;
      case 0x88: rd(pc); y--; setNZ(y); return;
      case 0xA8: rd(pc); y = a; setNZ(y); return;
      case 0xC8: rd(pc); y++; setNZ(y); return;
      case 0xE8: rd(pc); x++; setNZ(x); return;
      case 0x18: rd(pc); p &= ~FC; return;
      case 0x38: rd(pc); p |= FC; return;
      case 0x58: rd(pc); p &= ~FI; return;
      case 0x78: rd(pc); p |= FI; return;
      case 0xB8: rd(pc); p &= ~FV; return;
      case 0xD8: rd(pc); p &= ~FD; return;
      case 0xF8: rd(pc); p |= FD; return;
      case 0x98: rd(pc); a = y; setNZ(a); return;
      case 0x8A: rd(pc); a = x; setNZ(a); return;
      case 0x9A: rd(pc); sp = x; return;
      case 0xAA: rd(pc); x = a; setNZ(x); return;
      case 0xBA: rd(pc); x = sp; setNZ(x); return;
      case 0xCA: rd(pc); x--; setNZ(x); return;
      case 0xEA: case 0x1A: case 0x3A: case 0x5A: case 0x7A: case 0xDA: case 0xFA: rd(pc); return;
      default: break;
    }

    if ((op & 0x1F) == 0x10) {                         // branches
      bool set = (p & kBranchFlag[op >> 6]) != 0;
      int8_t off = (int8_t)fetch();
      if (set == (bool)((op >> 5) & 1)) {
        rd(pc);
        uint16_t np = pc + off;
        if ((np ^ pc) & 0xFF00) rd((pc & 0xFF00) | (np & 0xFF));
        pc = np;
      }
      return;
    }

    const uint8_t cc = op & 3, bbb = (op >> 2) & 7, aaa = op >> 5;
    uint16_t ad;

    switch (cc) {
      case 1: {
        if (bbb == 2) { uint8_t v = fetch(); if (aaa != 4) alu(aaa, v); return; }
        switch (bbb) {
          case 0: ad = indx(); break;
          case 1: ad = zp(); break;
          case 3: ad = absl(); break;
          case 4: ad = indy(aaa == 4); break;
          case 5: ad = zpx(); break;
          case 6: ad = absy(aaa == 4); break;
          default: ad = absx(aaa == 4); break;
        }
        if (aaa == 4) { wr(ad, a); return; }
        alu(aaa, rd(ad));
        return;
      }
      case 2: {
        if (bbb == 2) {
          rd(pc);
          switch (aaa) { case 0: a = asl(a); break; case 1: a = rol(a); break; case 2: a = lsr(a); break; case 3: a = ror(a); break; default: break; }
          return;
        }
        if (bbb == 0) { if (aaa == 5) { x = fetch(); setNZ(x); } else fetch(); return; }
        if (bbb == 4) { jammed = true; return; }
        if (bbb == 6) { rd(pc); return; }
        const bool ldxstx = (aaa == 4 || aaa == 5);
        switch (bbb) {
          case 1: ad = zp(); break;
          case 3: ad = absl(); break;
          case 5: ad = ldxstx ? zpy() : zpx(); break;
          default: ad = ldxstx ? absy(aaa == 4) : absx(true); break;
        }
        if (aaa == 4) { if (bbb != 7) wr(ad, x); return; }
        if (aaa == 5) { x = rd(ad); setNZ(x); return; }
        uint8_t v = rd(ad); wr(ad, v);
        switch (aaa) {
          case 0: v = asl(v); break; case 1: v = rol(v); break; case 2: v = lsr(v); break; case 3: v = ror(v); break;
          case 6: v--; setNZ(v); break; default: v++; setNZ(v); break;
        }
        wr(ad, v);
        return;
      }
      case 0: {
        if (bbb == 0) {
          uint8_t v = fetch();
          if (aaa == 5) { y = v; setNZ(y); } else if (aaa == 6) cmp(y, v); else if (aaa == 7) cmp(x, v);
          return;
        }
        switch (bbb) {
          case 1: ad = zp(); break;
          case 3: ad = absl(); break;
          case 5: ad = zpx(); break;
          case 7: ad = absx(false); break;
          default: return;
        }
        if (aaa == 4) { if (bbb != 7) wr(ad, y); return; }
        uint8_t v = rd(ad);
        switch (aaa) {
          case 1: p = (p & ~(FN | FV | FZ)) | (v & 0xC0) | ((a & v) ? 0 : FZ); break;
          case 5: y = v; setNZ(y); break;
          case 6: cmp(y, v); break;
          case 7: cmp(x, v); break;
          default: break;
        }
        return;
      }
      default: {                                       // cc == 3, illegal opcodes
        if (bbb == 2) {
          uint8_t v = fetch();
          switch (aaa) {
            case 0: case 1: a &= v; setNZ(a); p = (p & ~FC) | (a >> 7); break;
            case 2: a &= v; p = (p & ~FC) | (a & 1); a >>= 1; setNZ(a); break;
            case 3: { a &= v; uint8_t c = p & FC; a = (a >> 1) | (c << 7); setNZ(a);
                      p &= ~(FC | FV); if (a & 0x40) p |= FC; if (((a >> 6) ^ (a >> 5)) & 1) p |= FV; } break;
            case 4: a = x & v; setNZ(a); break;
            case 5: a = x = v; setNZ(a); break;
            case 6: { uint8_t t = a & x; p = (p & ~FC) | (t >= v ? FC : 0); x = t - v; setNZ(x); } break;
            default: sbc(v); break;
          }
          return;
        }
        const bool rmw = (aaa != 4 && aaa != 5);
        switch (bbb) {
          case 0: ad = indx(); break;
          case 1: ad = zp(); break;
          case 3: ad = absl(); break;
          case 4: ad = indy(aaa != 5); break;
          case 5: ad = (aaa == 4 || aaa == 5) ? zpy() : zpx(); break;
          case 6: ad = absy(aaa != 5); break;
          default: ad = (aaa == 5) ? absy(false) : (aaa == 4 ? absy(true) : absx(true)); break;
        }
        if (aaa == 4) { if (bbb == 0 || bbb == 1 || bbb == 3 || bbb == 5) wr(ad, a & x); return; }  // SAX
        if (aaa == 5) { uint8_t v = rd(ad); a = x = v; setNZ(v); return; }                          // LAX
        (void)rmw;
        uint8_t v = rd(ad); wr(ad, v);
        switch (aaa) {
          case 0: v = asl(v); a |= v; setNZ(a); break;      // SLO
          case 1: v = rol(v); a &= v; setNZ(a); break;      // RLA
          case 2: v = lsr(v); a ^= v; setNZ(a); break;      // SRE
          case 3: v = ror(v); adc(v); break;                // RRA
          case 6: v--; cmp(a, v); break;                    // DCP
          default: v++; sbc(v); break;                      // ISC
        }
        wr(ad, v);
        return;
      }
    }
  }

private:
  inline uint8_t rd(uint16_t ad) { return bus->read(ad); }
  inline void wr(uint16_t ad, uint8_t v) { bus->write(ad, v); }
  inline uint8_t fetch() { return rd(pc++); }
  inline void push(uint8_t v) { wr(0x100 | sp, v); sp--; }
  inline uint8_t pull() { sp++; return rd(0x100 | sp); }
  inline void setNZ(uint8_t v) { p = (p & ~(FN | FZ)) | (v & FN) | (v ? 0 : FZ); }

  inline uint16_t zp()  { return fetch(); }
  inline uint16_t zpx() { uint8_t b = fetch(); rd(b); return (uint8_t)(b + x); }
  inline uint16_t zpy() { uint8_t b = fetch(); rd(b); return (uint8_t)(b + y); }
  inline uint16_t absl() { uint16_t l = fetch(); uint16_t h = fetch(); return l | (h << 8); }
  inline uint16_t absx(bool w) {
    uint16_t l = fetch(), h = fetch(); uint16_t base = l | (h << 8), ad = base + x;
    if (w || ((base ^ ad) & 0xFF00)) rd((base & 0xFF00) | (ad & 0xFF));
    return ad;
  }
  inline uint16_t absy(bool w) {
    uint16_t l = fetch(), h = fetch(); uint16_t base = l | (h << 8), ad = base + y;
    if (w || ((base ^ ad) & 0xFF00)) rd((base & 0xFF00) | (ad & 0xFF));
    return ad;
  }
  inline uint16_t indx() {
    uint8_t z = fetch(); rd(z); z += x;
    uint16_t l = rd(z), h = rd((uint8_t)(z + 1)); return l | (h << 8);
  }
  inline uint16_t indy(bool w) {
    uint8_t z = fetch(); uint16_t l = rd(z), h = rd((uint8_t)(z + 1));
    uint16_t base = l | (h << 8), ad = base + y;
    if (w || ((base ^ ad) & 0xFF00)) rd((base & 0xFF00) | (ad & 0xFF));
    return ad;
  }

  inline void alu(uint8_t aaa, uint8_t v) {
    switch (aaa) {
      case 0: a |= v; setNZ(a); break;
      case 1: a &= v; setNZ(a); break;
      case 2: a ^= v; setNZ(a); break;
      case 3: adc(v); break;
      case 5: a = v; setNZ(a); break;
      case 6: cmp(a, v); break;
      case 7: sbc(v); break;
      default: break;
    }
  }
  inline void cmp(uint8_t r, uint8_t v) { p = (p & ~FC) | (r >= v ? FC : 0); setNZ((uint8_t)(r - v)); }

  inline void adcBin(uint8_t v) {
    uint16_t s = a + v + (p & FC);
    p &= ~(FC | FV);
    if (s > 0xFF) p |= FC;
    if (~(a ^ v) & (a ^ s) & 0x80) p |= FV;
    a = (uint8_t)s; setNZ(a);
  }
  inline void adc(uint8_t v) {
    if (!(p & FD)) { adcBin(v); return; }
    int al = (a & 0x0F) + (v & 0x0F) + (p & FC);
    if (al >= 0x0A) al = ((al + 6) & 0x0F) + 0x10;
    int r = (a & 0xF0) + (v & 0xF0) + al;
    uint8_t bin = (uint8_t)(a + v + (p & FC));
    p &= ~(FN | FV | FZ | FC);
    if (!bin) p |= FZ;
    if (r & 0x80) p |= FN;
    if (~(a ^ v) & (a ^ r) & 0x80) p |= FV;
    if (r >= 0xA0) r += 0x60;
    if (r >= 0x100) p |= FC;
    a = (uint8_t)r;
  }
  inline void sbc(uint8_t v) {
    if (!(p & FD)) { adcBin(~v); return; }
    uint8_t oa = a, c = p & FC;
    adcBin(~v);                                        // sets N V Z C like the binary operation
    int al = (oa & 0x0F) - (v & 0x0F) + c - 1;
    if (al < 0) al = ((al - 6) & 0x0F) - 0x10;
    int r = (oa & 0xF0) - (v & 0xF0) + al;
    if (r < 0) r -= 0x60;
    a = (uint8_t)r;
  }

  inline uint8_t asl(uint8_t v) { p = (p & ~FC) | (v >> 7); v <<= 1; setNZ(v); return v; }
  inline uint8_t lsr(uint8_t v) { p = (p & ~FC) | (v & 1); v >>= 1; setNZ(v); return v; }
  inline uint8_t rol(uint8_t v) { uint8_t c = p & FC; p = (p & ~FC) | (v >> 7); v = (v << 1) | c; setNZ(v); return v; }
  inline uint8_t ror(uint8_t v) { uint8_t c = p & FC; p = (p & ~FC) | (v & 1); v = (v >> 1) | (c << 7); setNZ(v); return v; }
};
