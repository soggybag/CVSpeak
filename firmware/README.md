# CVSpeak firmware

Raspberry Pi Pico 2 (RP2350) firmware. It uses the same `dsp/` core as the VCV plugin (speech, sequencer, menus and screens), so the module behaves like the plugin.

- **Core 0:** sets up the codec; scans controls at 1 kHz (two 74HC4051 muxes, V/OCT, encoder); runs the UI; draws the OLED at 30 Hz.
- **Core 1:** audio. The ES8388 is the I2S clock master, at 48 kHz from a 12 MHz MCLK. The loop is paced by its frames.

**Status (2026-11-07):** builds, **not yet run on hardware.** The ES8388 register setup, the I2S PIO program and the SH1106 setup come from the datasheets and still need a scope check (bring-up below).

## Build

One-time setup: Homebrew `cmake` and `picotool`, and an Arm GCC (Arm GNU Toolchain 10.3 or newer). Then download the Pico SDK into `sdk/`, which is gitignored like the Rack SDK:

```sh
git clone --depth 1 --branch 2.3.1 https://github.com/raspberrypi/pico-sdk.git sdk/pico-sdk
git -C sdk/pico-sdk submodule update --init --depth 1 lib/tinyusb
```

Then, from the repository root:

```sh
make firmware             # build/firmware/cvspeak.uf2 (the dev board)
make firmware-breadboard  # build/firmware-breadboard/cvspeak.uf2 (a bare Pico 2, see below)
```

**Flash:** hold BOOTSEL, plug the Pico 2 into USB, and drag the `.uf2` onto the `RP2350` drive. Debug text comes over USB serial at any baud rate, for example `screen /dev/tty.usbmodem* 115200`. Once a second it prints whether the codec answered and core 1's load.

## Breadboard wiring (`make firmware-breadboard`)

The breadboard build has no analog stages: the knobs sit at their defaults (Pitch C3, Speed 1×, Formant 0, Bend 0, Blend and Intonation full), and Carrier In is off. The pins match the dev board's pin map ([hardware/docs/Pin map.md](../hardware/docs/Pin%20map.md)), so the same wiring carries over.

| Pico 2 | Goes to |
|---|---|
| GP0 / GP1 | ES8388 SDA / SCL (the breakout usually has pull-ups; otherwise add 4k7 to 3V3). CE (AD0) low, so the address is 0x10. |
| GP16 | ES8388 DSDIN (DAC data in) |
| GP17 | ES8388 ASDOUT (ADC data out) |
| GP18 | ES8388 SCLK / BCLK |
| GP19 | ES8388 LRCK |
| GP21 | ES8388 MCLK. **Check the breakout:** if it has its own crystal on MCLK, remove it or cut the trace. |
| GP2 / GP3 | OLED SCK (D0) / MOSI (D1) |
| GP4 / GP5 | OLED DC / RES. Tie OLED CS to GND. |
| GP6 / GP7 | Encoder A / B; encoder common to GND |
| GP22 | Encoder push to GND |
| GP15 | BACK button to GND |
| GP20 | SPEAK button to GND |
| GP8 | TRIG: a button to GND acts as a gate (the board's transistor stage inverts, and so does a button to ground) |
| GP9 | RESET, the same way |
| GP10 | EOW out: 3.3 V, 8 ms (scope or LED) |
| GP11 | SPEAKING LED, through about 1k to GND |
| 3V3 (pin 36), GND | ES8388 breakout and OLED power |

## Bring-up checklist

1. **USB serial** prints `codec ok`. If it prints `NOT FOUND`, check the I2C wiring, CE and the pull-ups.
2. **Clocks on the scope:**
   - GP21 at 12 MHz;
   - BCLK at 2.4 MHz (MCLK/5);
   - LRCK at 48 kHz, with 25 BCLKs per half.

   If BCLK isn't MCLK/5, register 0x08 is wrong: see `src/es8388.cpp`.
3. **OLED** shows the Word screen. If it's mirrored or upside down, flip `0xA1` / `0xC8` in `src/sh1106.cpp`. If it's shifted by two pixels, change `kColumnOffset`.
4. **Encoder** scrolls one word per click. If it skips or doubles, change `kStepsPerDetent`. If it runs backwards, change `kEncoderDirection` (both in `src/controls.cpp`).
5. **SPEAK** says the word at the codec output. TRIG on GP8 does the same.
6. **Load:** the printed core 1 load at the defaults. For the worst case (Formant +1.5 octaves), set `formant` to 1.0 in `controls::Readings` and add 2.5 V to `formantCv`; or wait for the dev board and turn the knob plus CV. The budget is under about 70 %.
7. **Carrier In:** the dev board's detect circuit enables it. On the breadboard, set `carrierPatched = true` in `controls::Readings` to try it.

## Files

| File | What |
|---|---|
| `src/main.cpp` | Core 0: setup, control loop, UI, display |
| `src/audio.cpp` | Core 1: I2S, Voice, gates, EOW, LED, load meter |
| `src/i2s.pio` | Full-duplex I2S slave (16-bit). It never stalls: re-syncs on LRCK |
| `src/es8388.cpp` | Codec register setup (USB mode, master, 48 kHz, LIN1 in, LOUT1/ROUT1 out) |
| `src/sh1106.cpp` | OLED setup and page writes |
| `src/controls.cpp` | Muxes, ADC, encoder (IRQ), input scaling |
| `src/shared.h` | Data passed between the cores |
| `src/pins.h` | Pin assignment, mirroring `hardware/docs/Pin map.md` |

## Still to do

- Save settings (mode, word, phrase, chirp) to flash.
- Calibration: two-point per CV jack, and a 1V/oct routine for V/OCT.
- Read the Carrier detect once its circuit is decided.
- I2S by DMA if the per-sample FIFO loop costs too much CPU.
