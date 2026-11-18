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

// ---- System clock -----------------------------------------------------------
#define EMU_CLOCK_KHZ      250000   // 0 = non toccare il clock (decide il menu dell'IDE)
#define EMU_OVERVOLT       1        // vreg 1.20V quando clock > 133 MHz
#define ATARI_RAMFUNC       1        // run hot code from SRAM (avoids XIP cache misses)

#if ATARI_RAMFUNC
#define RAMFUNC(id) __attribute__((section(".time_critical." #id)))
#else
#define RAMFUNC(id)
#endif

// ---- Video standard / visible window ---------------------------------------
#define TIA_CLOCK_HZ        3579545UL // NTSC colour clock (PAL: 3546894UL)
#define NTSC_FIRST_LINE     34        // prima linea di scansione acquisita (conteggiata a partire dalla fine del VSYNC)
#define NTSC_LINES          210       // scanlines acquisite (estese a 240 righe)
#define PAL_FIRST_LINE      42
#define PAL_LINES           228
#define TIA_MAX_LINES       330       // forzare la fine di un fotogramma se la ROM non invia mai il segnale VSYNC

// ---- Display (TFT_eSPI, ILI9341 320x240, DMA) -------------------------------
#define TFT_ROTATION        1
#define FB_W                160       // TIA pixels per line
#define FB_H                240       // max captured lines
#define ROWS_PER_CHUNK      8         // rows converted per DMA chunk (240 % this == 0)

// Frameskip on core0 (it only saves framebuffer writes, collisions are always computed)
//   0  = render every frame (core1 drops what it cannot show anyway)
//   N  = render 1 frame every N+1
//  -1  = automatic, based on how long core0 had to wait for the audio clock
#define FRAMESKIP           0
#define AUTOSKIP_MAX        3

// ---- Audio: MAX98357 over I2S (PIO) ------------------------------------------
#define AUDIO_ENABLE        1
#define PIN_I2S_BCLK        26        // LRC (word clock) is BCLK+1 = GP27 (PIO limitation)
#define PIN_I2S_DIN         28
#define AUDIO_SAMPLE_RATE   31400     // Hz. TIA native tick is ~31400 Hz. Try 44100 / 48000
#define AUDIO_GAIN          700       // per AUDV unit, 2 channels * 15 * 700 = 21000
#define AUDIO_LP_SHIFT      1         // one-pole low-pass, 0 = off, 1 ~ 5 kHz, 2 ~ 2.3 kHz
#define AUDIO_CHUNK         64        // samples pompati a I2S per chiamata (blocks = real-time pacing)
#define AUDIO_DMA_BUFFERS   8
#define AUDIO_DMA_WORDS     64        // stereo frames per DMA buffer

// ---- TIA quirks ---------------------------------------------------------------
#define HMOVE_BLANK         0         // 1 = autentico 8-pixel di pettine nero sulla sx dopo HMOVE

// ---- Console switches --------------------------------------------------------
#define CONSOLE_COLOR       1         // 0 = B/W
#define DIFFICULTY_P0_A     0         // 1 = "A" (pro), 0 = "B"
#define DIFFICULTY_P1_A     0

// ---- Joystick boards (active LOW, internal pull-ups) -------------------------
#define PIN_J1_UP           2
#define PIN_J1_DOWN         3
#define PIN_J1_LEFT         4
#define PIN_J1_RIGHT        5
#define PIN_J1_FIRE         6
#define PIN_J1_B            7         // = console RESET
#define PIN_J2_UP           0
#define PIN_J2_DOWN         7
#define PIN_J2_LEFT         8
#define PIN_J2_RIGHT        9
#define PIN_J2_FIRE         14
#define PIN_J2_B            22         // = console SELECT
#define MENU_COMBO_MS       1000      // hold J1_B + J2_B to go back to the ROM menu

// ---- ROM storage -------------------------------------------------------------
#define ROM_DIR             "/Atari2600"
#define PIN_SD_CS           13        // SD on SPI1: SCK 10, MOSI 11, MISO 12
#define PIN_SD_SCK          10
#define PIN_SD_MOSI         11
#define PIN_SD_MISO         12
#define CART_MAX_SIZE       65536     // ROM mantenute in RAM (2K..64K: 2K,4K,F8,F6,F4,FA,E0,3F,EF/EFSC)
#define CART_BIG_MAX        524288    // la piú grande delle ROMs: 3F banks streamed from SD/flash file (rom[] = cache)
#define MAX_ROMS            64

#define ATARI_DEBUG         0         // Serial stats every ~300 frames
