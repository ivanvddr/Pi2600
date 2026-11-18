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
#include <math.h>

// Tavolozza TIA a 128 voci generata a partire dal sistema YIQ (con incrementi di tonalità di 360/14 gradi, partendo dal "oro"),
// memorizzata in formato RGB565 con byte invertiti (swap), in modo da poter essere inviata direttamente tramite DMA.
static uint16_t pal565[128];

static void buildPalette() {
  static const float gray[8] = {0.0f, 0.29f, 0.435f, 0.557f, 0.667f, 0.753f, 0.84f, 0.925f};
  for (int h = 0; h < 16; h++) {
    for (int l = 0; l < 8; l++) {
      float Y, I = 0, Q = 0;
      if (h == 0) Y = gray[l];
      else {
        float ang = (-44.0f + (h - 1) * (360.0f / 14.0f)) * 3.14159265f / 180.0f;
        Y = 0.20f + l * 0.065f;
        I = 0.30f * cosf(ang); Q = 0.30f * sinf(ang);
      }
      float r = Y + 0.956f * I + 0.621f * Q;
      float g = Y - 0.272f * I - 0.647f * Q;
      float b = Y - 1.106f * I + 1.703f * Q;
      auto cl = [](float v) { return v < 0 ? 0 : v > 1 ? 255 : (int)(v * 255.0f + 0.5f); };
      uint16_t c = ((cl(r) & 0xF8) << 8) | ((cl(g) & 0xFC) << 3) | (cl(b) >> 3);
      pal565[h * 8 + l] = (c >> 8) | (c << 8);
    }
  }
}
