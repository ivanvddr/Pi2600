# Pi2600
Pi2600 is an Atari 2600 emulator developed for the Raspberry Pi Pico (RP2040) using the Arduino IDE environment (arduino-pico core).
The project integrates support for the ILI9341 SPI display, game loading from SD or flash and i2s audio output.

# 🚀 Features
-  Core: Emulation of the 6507 CPU (NMOS 6502: all documented opcodes plus the commonly used undocumented ones and decimal mode), TIA video and RIOT (MOS 6532) RAM-I/O-timer. The CPU is cycle-accurate at bus level, which is required for the classic "race the beam" technique.
-  Display: ILI9341 320×240 SPI with DMA; 128-entry TIA palette generated in YIQ and sent as RGB565. Dual-core architecture: core0 runs the emulator and fills a triple-buffered framebuffer, core1 converts and pushes frames to the panel, skipping renders when too slow.
-  Storage: Loading .bin / .a26 ROMs from MicroSD (dedicated SPI1 bus) or from flash via LittleFS. Supported mappers: 2K, 4K, F8, F6, F4, FA, E0, 3F, EF/EFSC with automatic SuperChip detection; big Tigervision 3F ROMs (up to 512KB) are streamed from SD/flash in 2KB banks with an LRU cache.
-  Audio: i2s sound generation through a MAX98357 amplifier, sampled at the TIA native ~31.4kHz rate and paced in real time by the I2S ring buffer.
-  Input: One/Dual joystick ports (via GPIO) plus console switches (RESET / SELECT on the extra buttons; color and difficulty set in config.h).

# 🛠️ Hardware Requirements
To build the project, you will need the following components:
- Microcontroller: Raspberry Pi Pico (RP2040).
- Display: 2.8" ILI9341 SPI (320×240).
- Audio:
  - MAX98357 amplifier.
  - 3W / 4–8Ω speaker
- Input:
  - 2× directional joystick boards with 5-way switch + 2 buttons (Joystick 1 and 2): FIRE is the main action button, the extra B button is wired to console RESET (J1) and SELECT (J2).
- Storage: a MicroSD module (on SPI1) or the Pico's internal flash (LittleFS).

# 🔌 Wiring
The Raspberry Pi Pico is the core of the system. Below are the detailed connections, divided by module.

## 1. ILI9341 Display
The display pins are not fixed by the sketch: they are configured in TFT_eSPI's User_Setup.h.
The suggested wiring below matches the PiSMS reference hardware (same modules and pins, all free in Pi2600's config.h):
| Component | Module Pin | Pico Pin (GPIO) | Note |
|----------|-------------|------------------|------|
| Display  | VCC         | VSYS             | Powered directly from Pico |
|          | GND         | GND              | |
|          | SDO (MISO)  | GP16             | SPI0 |
|          | LED         | 3V3              | Backlight |
|          | SCK         | GP18             | SPI0 |
|          | SDI (MOSI)  | GP19             | SPI0 |
|          | DC          | GP21             | |
|          | RESET       | GP20             | |
|          | CS          | GP17             | |

## 2. MicroSD Card (SPI1)
The ROM card runs on its own SPI1 bus, so it does not share the display bus:
| Component | Module Pin | Pico Pin (GPIO) | Note |
|----------|-------------|------------------|------|
| SD Card  | SD_CS       | GP13             | Chip select on SPI1 |
|          | SD_SCK      | GP10             | SPI1 |
|          | SD_MOSI     | GP11             | SPI1 |
|          | SD_MISO     | GP12             | SPI1 |

## 3. Audio (i2s Amplifier)
| Component | Module Pin | Pico Pin (GPIO) | Note |
|----------|-------------|------------------|------|
| Ampl.    | VCC         | VBUS             | 5V |
|          | GND         | GND              | |
|          | BCLK        | GP26             | |
|          | LRC         | GP27             | LRC is BCLK+1 (PIO limitation) |
|          | DIN         | GP28             | |

## 4. Joysticks
Joysticks use internal pull-up resistors.
Each button closes the contact to GND.
| Joystick 1 GPIO | Function | Joystick 2 GPIO | Function |
|-----------------|----------|-----------------|----------|
| GP2             | UP       | GP0             | UP |
| GP3             | DOWN     | GP7*            | DOWN |
| GP4             | LEFT     | GP8             | LEFT |
| GP5             | RIGHT    | GP9             | RIGHT |
| GP6             | FIRE     | GP14            | FIRE |
| GP7             | BTN1 (console RESET) | GP22 | BTN2 (console SELECT) |

\* Note: in the current config.h, J2 DOWN and J1 BTN1 are both mapped to GP7. If you build a two-joystick setup, move one of the two to a free GPIO (for example GP1 or GP15) by editing config.h.

# 📝 Technical Notes
- Power:
Pico powered via USB.
Audio amplifier requires 5V (VBUS).
- Dual-core: core0 emulates the console (CPU + TIA + RIOT + audio samples) and never waits for core1; core1 only converts the framebuffer to RGB565 and sends it via DMA. A triple framebuffer with spin-lock decouples the two cores: if core1 is late, it simply drops frames.
- Video standards: NTSC and PAL are auto-detected frame by frame (more than ~290 scanlines = PAL). The visible window is 160 pixels × 210 lines (NTSC) or 228 lines (PAL), rescaled to 320×240 on the panel.
- Audio pacing: the emulator runs as fast as it can and blocks only when the I2S ring buffer is full — this is what locks emulation speed to real time. TIA native sample rate is ~31.4kHz (AUDIO_SAMPLE_RATE 31400; 44100 / 48000 can also be tried).
- Frameskip: FRAMESKIP in config.h — 0 = render every frame, N = render 1 frame out of N+1, -1 = automatic, based on how long core0 had to wait for the audio clock.
- TFT_eSPI Configuration:
Replace User_Setup.h with:
- Driver: ILI9341
- DMA: Enabled
The sketch sets the display rotation itself (TFT_ROTATION).
If you notice graphical glitches, reduce the SPI frequency.
- Optional quirk: HMOVE_BLANK 1 in config.h reproduces the authentic 8-pixel black "comb" on the left after HMOVE.
- Debug: set ATARI_DEBUG 1 to print emulation stats (fps, frameskip, scanlines, cart type) on serial every ~300 frames.

# 📂 Software Setup
The project is developed for Arduino IDE using the RP2040 core (arduino-pico).
In the Tools menu: Board = Raspberry Pi Pico, Optimize = -O3 (recommended).

## Required Libraries
Make sure you have installed:
- TFT_eSPI (configured for ILI9341 on Pico)
- SD (for the MicroSD on SPI1)
- LittleFS (bundled with the arduino-pico core)

## File System Structure
The emulator looks for ROMs inside the /Atari2600 directory, both on the MicroSD and in the Pico's flash filesystem.
Organize your FS like this:
```
Atari2600/
├─ game1.bin
├─ game2.a26
└─ ...
```
- Supported extensions: .bin and .a26
- Up to 64 ROMs are listed in the browser (MAX_ROMS in config.h)
- ROMs up to 64KB are loaded into RAM; bigger Tigervision 3F ROMs (up to 512KB) are streamed in 2KB banks from SD/flash

The /data/Atari2600 folder in this repository is the folder that gets uploaded to the Pico's flash.

## Using LittleFS Instead of a Physical SD Card
Instead of using a physical SD card, you can store the whole Atari2600 folder directly inside the Pico's onboard flash memory.
To do this, use the LittleFS Upload Tool for Arduino IDE, which uploads the contents of your local /data folder into the Pico's flash filesystem.
Official documentation:
Arduino-Pico LittleFS guide:
https://arduino-pico.readthedocs.io/en/latest/filesystems.html
LittleFS Upload Tool (arduino-littlefs-upload):
https://github.com/earlephilhower/arduino-littlefs-upload

# 🎮 Usage
1. **Startup**: On boot, the ROM browser appears ("Pi2600 - @IV ATARI 2600 emulator"), listing alphabetically the ROMs found on SD and flash (source letter "S" = SD, "F" = flash).
2. **Navigation**: Use joystick 1 to move between ROMs:
**GAMES BROWSER**: Scroll through the game list with the joystick; press FIRE to load the selected game. The extra B button rescans the list.
3. **In game**:
- J1 BTN1 = console RESET, J2 BTN2 = console SELECT
- Hold J1 BTN1 + J2 BTN2 for about 1 second to go back to the ROM browser
- Console switches (color/B&W, difficulty) are set at compile time in config.h

# ⚡ Performance & Overclock
To achieve acceptable emulation speed, the Raspberry Pi Pico is pushed beyond its factory specifications.
In config.h:
```
#define EMU_CLOCK_KHZ 250000   // Overclock to 250MHz
#define EMU_OVERVOLT  1        // vreg 1.20V when clock > 133MHz
```
The clock is changed at boot, before I2S / SPI are configured, with the voltage regulator raised to 1.20V.
This frequency allows:
- Real-time processing of CPU cycles, TIA video and audio samples
- Hot emulation code running from SRAM (ATARI_RAMFUNC), avoiding flash XIP cache misses
You may still notice slowdowns in some games, graphical glitches, or audio ticks due to timing desynchronization.

# ⚠️ Disclaimer
This project is provided "as is", for hobby and experimental use.
Overclock: May reduce microcontroller lifespan or cause thermal instability.
Hardware: The author is not responsible for damage to components (Pico, display, speaker), overheating, or malfunctions caused by incorrect wiring or use of the provided software.
Risks: Any hardware or software modification is performed at the user's own risk.

# 📜 License
Pi2600 is released under the GNU General Public License v3.0. See the LICENSE file for details.
Author: Ivan Vettori ivanvddr
