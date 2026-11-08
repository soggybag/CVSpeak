# CVSpeak: Pico 2 pin map (draft, 2026-11-07)

Pin assignment for dev board v0. It is the source of truth for the firmware until the schematic exists; after that, the schematic script is.

The Pico 2 brings out GPIO0–22 and 26–28. GPIO23–25 and 29 are used on the module itself (SMPS mode, VBUS sense, LED, VSYS sense).

**Budget:** 20 digital and 3 ADC pins used, 3 spare (GP15, GP20, GP22). OLED CS is tied low. The buttons and Carrier detect are read through spare analog-mux channels, which saves 4 GPIO.

| GPIO | Pico pin | Net | Function |
|---|---|---|---|
| GP0 | 1 | I2C_SDA | I2C0 SDA, ES8388 control (address 0x10, CE low). 4k7 pull-up to 3V3 |
| GP1 | 2 | I2C_SCL | I2C0 SCL, 4k7 pull-up |
| GP2 | 4 | OLED_SCK | SPI0 SCK |
| GP3 | 5 | OLED_MOSI | SPI0 TX |
| GP4 | 6 | OLED_DC | Data/command |
| GP5 | 7 | OLED_RES | Reset (CS tied low) |
| GP6 | 9 | ENC_A | Encoder A, internal pull-up |
| GP7 | 10 | ENC_B | Encoder B, internal pull-up |
| GP8 | 11 | GATE_TRIG | TRIG gate (inverted, MMBT3904) |
| GP9 | 12 | GATE_RESET | RESET gate (inverted) |
| GP10 | 14 | EOW_OUT | → ×2.8 stage → 0/9.2 V, 5–10 ms pulse |
| GP11 | 15 | LED_SPEAK | SPEAKING LED (about 2 mA) |
| GP12 | 16 | MUX_S0 | Select bit 0, both 74HC4051s |
| GP13 | 17 | MUX_S1 | Select bit 1 |
| GP14 | 19 | MUX_S2 | Select bit 2 |
| GP15 | 20 | SPARE_15 | Spare, to a test pad |
| GP16 | 21 | I2S_DOUT | RP2350 → ES8388 DSDIN (DAC data) |
| GP17 | 22 | I2S_DIN | ES8388 ASDOUT → RP2350 (ADC data, Carrier In) |
| GP18 | 24 | I2S_BCLK | ES8388 SCLK (codec is master) |
| GP19 | 25 | I2S_LRCK | ES8388 LRCK. Must be BCLK + 1 for the PIO program |
| GP20 | 26 | SPARE_20 | Spare, to a test pad |
| GP21 | 27 | CODEC_MCLK | GPOUT0 = 12 MHz from XOSC → ES8388 MCLK (USB mode, Fs = 48 kHz) |
| GP22 | 29 | SPARE_22 | Spare, to a test pad |
| GP26 | 31 | ADC_VOCT | ADC0: V/OCT (±5 V, 0.1 % stage, no mux) |
| GP27 | 32 | ADC_MUX_A | ADC1: mux A common |
| GP28 | 34 | ADC_MUX_B | ADC2: mux B common |
| – | 35 | ADC_VREF | Driven from 3V3A |
| – | 36 | 3V3 | Pico 3V3 out: logic, OLED, ES8388 DVDD/PVDD, gate pull-ups |
| – | 39 | VSYS | +5 V from the buck, through a Schottky |
| – | 3, 8, 13, … | GND | |
| – | 33 | AGND | Analog ground |

## Mux A (74HC4051 on 3V3A → ADC1)

| Channel | Signal |
|---|---|
| X0 | PITCH pot |
| X1 | SPEED pot |
| X2 | FORMANT pot |
| X3 | BEND pot |
| X4 | BLEND pot |
| X5 | INTONE pot |
| X6 | BACK button (pull-up, low = pressed) |
| X7 | Encoder push (pull-up, low = pressed) |

## Mux B (74HC4051 on 3V3A → ADC2)

| Channel | Signal |
|---|---|
| X0 | SEL CV (±10 V) |
| X1 | SPEED CV |
| X2 | FORM CV |
| X3 | BEND CV |
| X4 | SPEAK button (pull-up, low = pressed) |
| X5 | Carrier insertion detect |
| X6 | Spare (future Mod In CV or detect) |
| X7 | Spare |

## Signal conventions for firmware

- **CV inputs are inverted:** +10 V reads about 0 V and −10 V about 3.3 V, with 0 V near mid-scale. V/OCT does the same over ±5 V. Calibrate per jack (two points); V/OCT needs it.
- Pots read 0 V fully counter-clockwise and 3V3A fully clockwise.
- Gates are inverted: the GPIO reads low while the gate is high.
- Read the buttons on the mux as digital: below 1 V = pressed. Debounce in firmware.
- Mux settling: allow a few µs after changing the select lines before starting the ADC conversion.
