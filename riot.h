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

static const uint8_t kRiotShift[4] = {0, 3, 6, 10};

// MOS 6532: 128 byte di RAM presenti nell'Atari, che includono: porte di I/O e timer a intervalli
class RIOT {
public:
  uint8_t outA = 0, ddrA = 0, outB = 0, ddrB = 0;
  uint8_t inA = 0xFF;          // joysticks (active low), aggiornato dallo sketch
  uint8_t inB = 0x3F;          // console switches
  uint32_t start = 0;
  uint8_t  shift = 10, val = 0xFF;
  bool     flagClr = false;

  void reset(uint32_t cyc) {
    outA = ddrA = outB = ddrB = 0;
    start = cyc; shift = 10; val = 0xFF; flagClr = false;
  }

  RAMFUNC(riot_read) uint8_t read(uint16_t a, uint32_t cyc) {
    if (a & 4) {
      int32_t rem = remaining(cyc);
      if (a & 1) return (rem < 0 && !flagClr) ? 0x80 : 0x00;           // INSTAT
      if (rem < 0) { flagClr = true; return (uint8_t)rem; }             // INTIM, free running
      return (uint8_t)(rem >> shift);                                    // INTIM
    }
    switch (a & 3) {
      case 0: return (outA & ddrA) | (inA & ~ddrA);
      case 1: return ddrA;
      case 2: return (outB & ddrB) | (inB & ~ddrB);
      default: return ddrB;
    }
  }

  RAMFUNC(riot_write) void write(uint16_t a, uint8_t v, uint32_t cyc) {
    if (a & 4) {
      if (a & 0x10) {
        shift = kRiotShift[a & 3]; val = v; start = cyc; flagClr = false;
      }
      return;                                  // controllo di rilevamento dei bordi ignorato
    }
    switch (a & 3) {
      case 0: outA = v; break;
      case 1: ddrA = v; break;
      case 2: break;
      default: ddrB = v; break;
    }
  }

private:
  RAMFUNC(riot_remaining) int32_t remaining(uint32_t cyc) {
    int32_t rem = (int32_t)((uint32_t)val << shift) - (int32_t)(cyc - start);
    if (rem < -65536) {                        // mantiene corretti i calcoli a 32 bit durante esecuzioni molto lunghe
      int32_t over = ((-rem) - 256) & ~0xFF;
      start += over; rem += over;
    }
    return rem;
  }
};
