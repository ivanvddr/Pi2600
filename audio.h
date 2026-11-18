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

#if AUDIO_ENABLE
#include <I2S.h>
static I2S i2s(OUTPUT);
#endif

static volatile uint32_t g_audioWaitUs = 0;   // tempo trascorso dal core0 in stato di blocco in attesa del clock I2S

static inline void audioBegin() {
#if AUDIO_ENABLE
  i2s.setBCLK(PIN_I2S_BCLK);                  // LRCLK = BCLK + 1
  i2s.setDATA(PIN_I2S_DIN);
  i2s.setBitsPerSample(16);
  i2s.setBuffers(AUDIO_DMA_BUFFERS, AUDIO_DMA_WORDS, 0);   // underrun -> silenzio
  i2s.begin(AUDIO_SAMPLE_RATE);
#endif
}

// Blocca l'esecuzione quando il ring buffer I2S è pieno: è questo che vincola la velocità dell'emulazione al tempo reale.
static inline void audioWrite(const int16_t* s, int n) {
#if AUDIO_ENABLE
  uint32_t t0 = micros();
  for (int i = 0; i < n; i++) i2s.write16(s[i], s[i]);
  g_audioWaitUs += micros() - t0;
#else
  (void)s; (void)n;
#endif
}
