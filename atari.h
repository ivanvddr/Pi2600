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
#include "cart.h"
#include "riot.h"
#include "tia.h"
#include "cpu6502.h"
#include "audio.h"

// ============================================================================
//  Sistema Atari 2600: decodifica del bus + clock principale.
//  Un ciclo CPU equivale a 3 cicli di clock colore. La funzione tick() viene
//  richiamata a ogni accesso al bus.
//  I campioni audio vengono generati a partire dal tempo *emulato* (accumulatore
//  dei cicli di clock colore); pertanto, la frequenza di campionamento è
//  arbitraria (AUDIO_SAMPLE_RATE) e l'emulazione è regolata dal consumer I2S
//  all'interno di audioWrite().
// ============================================================================
class Atari {
public:
  TIA  tia;
  RIOT riot;
  Cart cart;
  CPU6502<Atari> cpu;
  uint8_t  ram[128];
  uint32_t cycles = 0;

  void reset() {
    memset(ram, 0, sizeof(ram));
    cart.reset();
    riot.reset(0);
    sampAcc = 0; dc = 0; lp = 0; an = 0;
    cpu.bus = this;
    tia.reset();
    tia.wsync = false;
    cpu.reset();
    tia.clk = 0; tia.line = 0; tia.frameEnded = false;
    cycles = 0; riot.start = 0;
  }

  // esegue l'emulazione fino alla fine del prossimo VSYNC (o fino a TIA_MAX_LINES)
  RAMFUNC(atari_runFrame) void runFrame() {
    tia.frameEnded = false;
    while (!tia.frameEnded) cpu.step();
  }

  void flushAudio() { if (an) { audioWrite(abuf, an); an = 0; } }

  // bus (chiamato da CPU)
  RAMFUNC(atari_read) uint8_t read(uint16_t a) {
    a &= 0x1FFF;
    if (tia.wsync) stallToEndOfLine();             // RDY low: CPU in pausa fino a fine riga
    uint8_t v;
    if (a & 0x1000)       v = cart.read(a);
    else if (a & 0x80)    v = (a & 0x200) ? riot.read(a, cycles) : ram[a & 0x7F];
    else                  v = tia.read(a & 0x0F);
    tick();
    return v;
  }

  RAMFUNC(atari_write) void write(uint16_t a, uint8_t v) {
    a &= 0x1FFF;
    if (a & 0x1000)       cart.write(a, v);
    else if (a & 0x80) {
      if (a & 0x200) riot.write(a, v, cycles); else ram[a & 0x7F] = v;
    } else {
      tia.write(a & 0x3F, v);
      if (cart.is3F && a == 0x3F) cart.write3F(v);
    }
    tick();
  }

  RAMFUNC(atari_idle) void idle() { tick(); }

private:
  uint32_t sampAcc = 0;
  int32_t  dc = 0, lp = 0;
  int16_t  abuf[AUDIO_CHUNK];
  int      an = 0;

  // avanza i cicli CPU in blocco (senza ritorno a capo, senza tick audio intermedi)
  inline void advance(int n) {
    cycles += n;
    tia.clk += 3 * n;
    sampAcc += 3u * AUDIO_SAMPLE_RATE * (uint32_t)n;
    while (sampAcc >= TIA_CLOCK_HZ) { sampAcc -= TIA_CLOCK_HZ; emitSample(); }
  }

  // WSYNC: avanzamento rapido fino alla fine della linea di scansione, anziché procedere ciclo per ciclo
  RAMFUNC(atari_stall) void stallToEndOfLine() {
    while (tia.clk < TIA_LINE_CLOCKS) {
      int target = tia.clk < 9 ? 9 : tia.clk < 81 ? 81 : TIA_LINE_CLOCKS;
      advance((target - tia.clk) / 3);
      if (tia.clk == 9 || tia.clk == 81) tia.audioTick();
    }
    tia.endOfLine();
  }

  inline __attribute__((always_inline)) void tick() {
    cycles++;
    int c = tia.clk + 3;
    tia.clk = c;
    if (c == 9 || c == 81) tia.audioTick();         // 2 audio ticks per scanline (~31.4 kHz)
    sampAcc += 3u * AUDIO_SAMPLE_RATE;
    if (sampAcc >= TIA_CLOCK_HZ) { sampAcc -= TIA_CLOCK_HZ; emitSample(); }
    if (c >= TIA_LINE_CLOCKS) tia.endOfLine();
  }

  RAMFUNC(atari_emitSample) void emitSample() {
    int32_t x = tia.audioMix() * AUDIO_GAIN;
    dc += (x - dc) >> 10;                           // DC blocker
    int32_t y = x - dc;
#if AUDIO_LP_SHIFT > 0
    lp += (y - lp) >> AUDIO_LP_SHIFT;
    y = lp;
#endif
    if (y > 32767) y = 32767; else if (y < -32768) y = -32768;
    abuf[an++] = (int16_t)y;
    if (an >= AUDIO_CHUNK) { audioWrite(abuf, an); an = 0; }
  }
};
