# CVSpeak hardware: decisions

Decisions made with Mitchell for the first board (dev board v0). The VCV plugin is the behavioural reference: where this file says "as in VCV", the plugin's numbers in `PLAN.md` section 6 apply. The pin map is in [Pin map.md](Pin%20map.md).

## 2026-11-07

| Topic | Decision |
|---|---|
| Boards | **One PCB + front panel**, as in Palimpsest. Panel parts (jacks, pots, encoder, buttons, OLED, LED) go on the front. The Pico 2 (on female headers), the power header and all SMD parts go on the back, and JLC assembles the back side. 14 HP: the board is about 66 × 100 mm, clear of the rails (top and bottom 10 mm). |
| MCU | **Raspberry Pi Pico 2 module** (RP2350A) on female headers for v0. This keeps USB and BOOTSEL reachable and avoids the riskiest SMD work. Chip-down RP2350 is for v1, to cut cost. |
| Codec | **ES8388** (Everest, QFN-28 4×4, LCSC C365736, Extended part, about $1.10, around 15k in stock on 2026-11-07). One chip covers Carrier In and the audio out. PCM3060 was the alternative: better known in Eurorack (Daisy Seed 1.2), but $3–5 with only a few hundred in stock. |
| Codec clock | **USB mode: MCLK = 12 MHz, Fs = MCLK/250 = 48 kHz.** The Pico 2's 12 MHz crystal is brought out on GPIO21 (GPOUT0), so the clock is exact and needs no extra parts. The ES8388 is the **I2S master** (it makes BCLK and LRCK from MCLK); the RP2350 receives and sends data with PIO. |
| Levels | Doepfer A-100 levels, as on Palimpsest: CV inputs ±10 V at 100k, gates trigger at about 1.2 V, outputs through 1k. V/OCT is the exception (see below). |
| Pots | 6 × Alpha RD901F 9 mm, linear: PITCH, SPEED, FORMANT, BEND, BLEND, INTONE. |
| Display | 1.3" 128×64 OLED, **SH1106, SPI 7-pin module**. A new footprint: measure a real module (outline, header position, active area) before placing. |
| Encoder | EC11 type, with push. |
| Buttons | BACK and SPEAK: the same round 12 mm tact switch as Palimpsest (DAIER TSL-A064). |
| Light | SPEAKING: a 3 mm LED on a GPIO. |

## Audio path (ES8388 numbers from its datasheet, Rev 5.0)

- **Full scale is 1 Vrms = 2.83 Vpp at AVDD = 3.3 V**, for both the ADC input and the DAC output. The ADC input impedance is 20 kΩ.
- **Carrier In:** ×−0.2 (100k in, 20k feedback), as on Palimpsest. 10 Vpp becomes 2 Vpp at the codec, about −3 dBFS, with headroom up to about 14 Vpp. AC coupled into LIN1. The DSP normalises the carrier's level, so the exact gain doesn't matter much; **noise does** (see the input notes in `PLAN.md` section 6).
- **Audio out:** the DSP's ±1.0 is the DAC's full scale, 2.83 Vpp. ×−3.5 makes ±5 V (10 Vpp) at the jack. Then 1k in series. AC coupled from the codec, whose outputs sit at VMID; the DSP already removes DC.
- **Op-amp:** OPA1678 (LCSC C192421), as on Palimpsest.
- Performance: ADC SNR 95 dB, DAC SNR 96 dB, both A-weighted. That's plenty for an 8 kHz LPC voice.

## Analog supply and the ADC

- The Pico 2's own 3.3 V comes from a switching regulator and is noisy. **A separate low-noise 3.3 V LDO (3V3A), fed from the 5 V buck output,** supplies the ES8388's AVDD and HPVDD, the pots, the mux, the CV bias and the Pico's **ADC_VREF** pin.
- Everything analog is then ratiometric to 3V3A. Calibration takes care of the LDO's initial tolerance.
- The ES8388's DVDD and PVDD come from the Pico's 3V3, the same as the logic levels.

## V/OCT accuracy

- The RP2350's ADC has about 9 effective bits.
- **V/OCT gets its own ADC pin** (no mux) and a **±5 V range**: 0.33 V per volt, 27.5 mV per semitone, about 34 codes per semitone at 12 bits.
- Averaging at control rate (64 samples) brings the noise to about one code.
- 0.1 % resistors, as on Palimpsest's SPEED input. Two-point calibration is stored in flash.
- If that isn't good enough on the bench, v1 adds an external ADC for V/OCT only.

## Inputs, gates and outputs

| Block | Circuit |
|---|---|
| CV inputs | TL074 inverting stage with a bias offset; BAT54S clamp to 0..3V3A. ±10 V → 3.3..0 V. This is Palimpsest's v0.2 stage. |
| V/OCT | The same stage with ±5 V → 3.3..0 V and 0.1 % resistors. Direct to ADC0. |
| Gates (TRIG, RESET) | MMBT3904 inverter: 100k series, 100k to ground (about 1.2 V threshold), 1N4148W base clamp, 10k pull-up to 3V3. Passes 1 kHz trains. Firmware applies RESET before TRIG in the same tick. |
| EOW out | GPIO → ×2.8 non-inverting stage → 0 / 9.2 V, then 1k. Firmware makes a **5–10 ms** pulse (VCV uses 1 ms). |
| Carrier insertion detect | The Carrier jack's switch lug (TN) has a 1M pull-up to 3V3A and 100n to ground, and goes to mux B X5. Unpatched, TN touches the tip, which sits at 0 V through the 100k input resistor, so it reads about 0.3 V. Patched, the plug lifts TN and it reads 3.3 V. Firmware threshold: 2.5 V. |
| Pots and slow CVs | **Two 74HC4051** on 3V3A share one set of select lines (GP12–14). Mux A has the 6 pots plus BACK and encoder push. Mux B has 4 CVs plus SPEAK, Carrier detect and 2 spares. Buttons on mux channels read 0 or 3V3A through a pull-up; a 1 kHz scan is plenty for them. |
| Power | 2×5 Eurorack header; 1N5819WS in series with each rail; 10u + 100n per rail. AP63205 buck, 12 V → 5 V. The 5 V goes to the Pico 2's VSYS through a Schottky diode, as the Pico datasheet recommends (USB can be plugged in at the same time). The 3V3A LDO also runs from the 5 V. Op-amps run on ±12 V. |
| Test points | Palimpsest style: the rails, 5 V, 3V3, 3V3A, CV bias, every ADC pin, codec in and out, I2S, MCLK, gates. Two GND through-holes for a scope ground. |

## RP2350 notes

- **Erratum E9:** GPIO inputs with internal pull-downs can latch high. Use no internal pull-downs. The gate inputs are actively driven, and the encoder uses pull-ups. Disable the digital input buffer on ADC pins (the SDK's `adc_gpio_init` does).
- ADC pins: the Pico 2 brings out GPIO26–28 (ADC0–2); ADC3 is used for VSYS sensing on the module.
- PIO: I2S in and out on one state machine pair, with BCLK and LRCK on adjacent pins (GP18, GP19) for the PIO `wait` instructions.

## 2026-11-07: schematic v0.0

Drawn by `hardware/scripts/build_sch.py` (`make schematic`), with the same scripted flow as Palimpsest. ERC: no errors; one warning for the OLED footprint, which waits for the module measurements.

| Topic | Decision |
|---|---|
| 3V3A LDO | **LP5907MFX-3.3** (SOT-23-5, 250 mA, about 6.5 µVrms), 1u in, 1u + 10u out. LCSC number still to pick. |
| Codec references | VREF, ADCVREF and VMID each get 10u + 100n. Each supply pin (DVDD, PVDD, AVDD, HPVDD) gets 10u + 100n. All four ground pins (DGND, AGND, HPGND, EPAD) tie together under the chip. |
| MCLK | 33R in series, placed at the Pico. |
| Audio out gain | 20k in, 68k feedback: ×−3.4, so DAC full scale gives 9.6 Vpp. |
| V/OCT bias | 3V3A × 12k / 32k = 1.24 V, so 0 V reads mid-scale (1.65 V). |
| Encoder | 10k pull-ups and 10n to ground on A and B (0.1 ms debounce), as well as the internal pull-ups. |
| Spare op-amps | U4 units C and D: followers to ground. |
| Mux B spares | X6 and X7: 100k to ground, plus test pads. |
| Pico symbol | A project copy of KiCad's `RaspberryPi_Pico` named `Pico2`, with GND and AGND retyped as power inputs. |

## 2026-11-08: OLED module ordered

Mitchell ordered 5 × "1.3 Inch 128x64 OLED Module IIC SPI Dual Interface 7 Pin SH1106". The header silkscreen (display side, pins out the back) reads **1 GND VDD SCK SDA RES DC CS 7**. That matches the schematic symbol in order. In SPI mode SDA is MOSI (GP3). Leave the I2C/SPI selection resistors as shipped (SPI). Measure on arrival: outline, header pin 1 position, mounting holes, active area, glass height and standoff height.

Until then, firmware can be tested with a 0.96" SSD1306 (`make firmware-breadboard SSD1306=1`). A Pico 2 is ordered too.

## Still open

- LP5907MFX-3.3 LCSC part and stock, or another low-noise 3.3 V LDO in SOT-23-5.
- ES8388 reference caps: check them against the datasheet's application figure (it's an image, so it isn't in the extracted text).
- OLED module measurements (module ordered 2026-11-08), then the `CVSpeak:OLED_SH1106_1.3in_SPI_7pin` footprint.
- LCSC numbers marked NEW in `build_sch.py` (68k, 1M, 33R, 10n): check them.
- LCSC numbers and stock for each new part, on the day of the order.
- Current budget, to measure: expected about 40 mA on +12 V (through the buck) and 20–30 mA on −12 V.

## Testing before ordering (Palimpsest boards, arriving week of 2026-11-09)

Palimpsest v0.2 uses the same power, CV, gate and output blocks. Bench them there first:
1. The buck's ripple and noise on 5 V, and on 3V3A if fitted.
2. CV stage accuracy and noise at the ADC pin, including the 0.1 % SPEED stage, our model for V/OCT.
3. Gate threshold and the 1 kHz pulse response.
4. The OLED over SPI and the encoder.

Anything we learn changes this file before the CVSpeak schematic is drawn.

In parallel, firmware bring-up can start on a bare Pico 2 with an ES8388 breakout module, the 1.3" OLED and an encoder on a breadboard. That proves I2C setup, 12 MHz MCLK, I2S both ways, and the CPU budget at Formant +1.5 octaves, before the PCB is ordered.

**ES8388 breakouts are hard to find (2026-11-07).** Plan: skip the breakout. The dev board puts the codec on the PCB with test points on I2C, I2S, MCLK and the codec's analog pins, and JLC assembles it. Until then the breadboard can still check the OLED, the encoder, the buttons and the CPU load (USB serial prints core 1's load even with no codec answering).
