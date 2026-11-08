# CVSpeak — Project Plan

A Eurorack speech module in the style of the Speak & Spell, built on the RP2350.
We prototype it as a VCV Rack module first, using a DSP core that is shared with the firmware.

## 1. Feature set

### Display and menu (Ornament & Crime style)
- **128×64 OLED** with a **push encoder**, plus a **back/shift button**. Starting point: one encoder and one button. Try it in VCV and add a second encoder only if the menu feels cramped.
- The display shows the current mode, bank or phrase name, and the word being spoken (with the current word highlighted in Phrase mode). It also has a settings menu: retrigger behavior, excitation source, calibration, and so on.
- **Main screen:** turn the encoder to scroll through banks or phrases, and push it to open the menu.
- The UI is shared code: it lives in `dsp/` (`ui.h`, `framebuffer.h`) and draws into a 1-bit 128×64 framebuffer. VCV draws that framebuffer on screen, and the firmware sends it to the OLED. Menus behave the same on both, and we design them in VCV first.

### Speech modes (selected from the menu)
| Mode | Word / Phrase knob + CV | TRIG | RESET |
|---|---|---|---|
| **Word** | Selects a word from the whole vocabulary | Speaks the selected word | — |
| **Phrase** | Selects a phrase (a sentence or paragraph bank) | Speaks the **next word** in the phrase, wrapping at the end | Moves back to the first word (doesn't cut off the current word) |

- **EOW out**: fires a trigger when each word ends. Patch it back into TRIG to speak a whole phrase on its own, or use it to clock other modules.
- **Retrigger behavior**: if a trigger arrives while a word is still playing, the current word is cut off and the next one starts. A "finish word first" setting can come later.
- **Phrase changes**: if the Phrase CV changes partway through, the next trigger starts at word 0 of the new phrase. This keeps the behavior predictable when you sequence phrases.
- **Phrase files**: phrases are written as plain text, e.g. `phrases.txt` → `HELLO / I AM / A ROBOT`. A tool script turns them into tables of word indices at build time.

### Sound controls (apply in every mode)
- **Pitch**: 1V/oct, and independent of speed
- **Speed**: how fast the LPC frames advance, i.e. time-stretch
- **Formant**: shifts or warps the filter coefficients
- **Bend**: imitates circuit bending by corrupting bits, freezing frames, scrambling coefficients, and so on

### Excitation and vocoder
1. **Internal excitation**: the classic buzz-and-noise source of the TMS5220
2. **Carrier In (talking synth)**: when a cable is plugged in, external audio replaces the internal excitation, so the LPC words "speak through" your oscillator. The DSP cost is close to zero. A **Blend** control mixes internal and external excitation.
3. **Channel vocoder mode** *(stretch goal)*: a 12–16-band vocoder. **Mod In** is the modulator (a live voice, drums, etc.) and **Carrier In** is the carrier. If Mod In is empty, the module's own speech becomes the modulator.

## 2. Panel I/O draft (14 HP)
- **Display**: 128×64 OLED (1.3" SH1106 or 0.96" SSD1306)
- **Encoder** (with push) and a **Back/Shift button**
- **Speak button**: a manual trigger that ORs with TRIG. It's needed to kick-start an EOW → TRIG self-patch, since EOW can't fire until a word has played.
- **Knobs**: Pitch, Speed, Formant, Bend, Blend. Word/Phrase selection is on the encoder, and its CV is added on top. Intonation and Chirp (added in Phase 1) could be knobs or menu settings; decide from VCV play-testing.
- **Inputs**: Trig, Reset, Word/Phrase CV, Pitch 1V/oct, Speed CV, Formant CV, Bend CV, Carrier In, Mod In
- **Outputs**: Audio Out, EOW

## 2a. Manufacturing constraints
- The PCBs will be **assembled by JLCPCB using SMD parts**. Prefer **LCSC Basic** parts, and use **Extended** parts only when no Basic part fits. Each Extended part adds a setup fee per order, so keep the number of different Extended parts low.
- Use **0603 passives** from the Basic library, and pick standard values (E12/E24 series) that are in Basic.
- Expected Extended parts: the RP2350 itself, QSPI flash, the audio codec, a precision voltage reference, and possibly the op-amps. *Check stock on LCSC before locking the schematic.*
- **Through-hole panel parts**: jacks, pots, the encoder, the OLED and the power header. These are usually hand-soldered or done with JLC's paid THT assembly. Use **two boards**: an SMD main board, and a control board that carries the panel parts and stacks on headers. This is the Ornament & Crime layout.
- Keep the design JLC-friendly: 2- or 4-layer board, standard via sizes, no parts on the bottom side unless needed, and fiducials and tooling holes per JLC's guidelines.

## 3. Architecture
```
CVSpeak/
  dsp/        plain C++17, float only: LPC decoder, word bank, phrase sequencer,
              excitation, vocoder. No dependency on Rack or the Pico SDK.
              plus the shared menu and screens (ui, framebuffer), drawn into a 128x64 1-bit framebuffer
  tests/      unit tests for the core (make test)
  tools/      Python: WAV → LPC encoding, phrases.txt → C++ tables, render-to-WAV test
  src/ res/   VCV Rack plugin wrapper and panel (GPLv3); plugin.json + Makefile at the repo root
  firmware/   Pico SDK wrapper: ADC/I2S/GPIO → dsp core
  hardware/   KiCad schematic + PCB + panel
```
- The core runs at its own internal rate (8 kHz for LPC; 32–48 kHz for the vocoder). Each wrapper handles resampling.
- One `Voice::process(const Inputs&, float* out, int n)` API, called by both wrappers.

## 4. Phases

### Phase 0: Setup ✅ (2026-11-07)
- Git repo, folder layout, Rack SDK 2.6.6 in `sdk/` (gitignored).
- Desktop harness: `make test` (unit tests) and `make render` (renders the core to WAV).
- VCV plugin skeleton: Trig, Reset, V/Oct, Audio and EOW, plus a stand-in OLED display, all driven by a placeholder buzz voice.
- ☐ Install VCV Rack 2 Free. The machine currently has Rack 1.1.6, which can't load v2 plugins.

### Phase 1: LPC core ✅ (2026-11-07)
- TMS5220-style bitstream (`dsp/include/cvspeak/lpc_format.h`): 4-bit energy, 6-bit pitch, repeat flag, K1–K10 at 5/5/4/4/4/4/4/3/3/3 bits, and unvoiced frames carry only K1–K4. The quantization tables are our own (arcsine-spaced K, log-spaced energy and pitch), not TI's.
- Decoder (`LpcSynth`): runs at 8 kHz with 25 ms frames, interpolates parameters per sample, and uses a 10-pole lattice filter, de-emphasis and a DC blocker. Voice resamples to the host rate with a 3.6 kHz anti-image low-pass.
- Our own encoder (`tools/lpc_encode`) shares the format and analysis code with the decoder. `make words` speaks `tools/words.txt` with macOS `say` (voice "Reed", 150 wpm) and writes `dsp/src/words_generated.cpp`: 30 words in about 3 KB.
- Controls: Pitch (1V/oct; word median → V/Oct), Speed (0.25–4×, independent of pitch), **Intonation** (0 = monotone, 1 = recorded contour), **Chirp** (impulse ↔ TI-style chirp excitation, loudness-normalized).
- Verified: 12 unit tests. Decoded loudness is within about 1.3 dB of the originals on average (worst case 3 dB) at the default Chirp of 0.5, and the frame-loudness envelope tracks the originals (correlation 0.67–0.99).
- Not done yet: a TI-compatible table mode, so existing TMS5220 LPC data could be played.

### Phase 2: Modes and sequencer ✅ (2026-11-07)
- Word mode and Phrase mode (`dsp/include/cvspeak/sequencer.h`). TRIG advances and wraps. RESET rewinds without cutting off the current word, and it applies before TRIG, so RESET and TRIG together speak word 1. Changing phrase starts the new one from word 1. In Word mode, RESET does nothing.
- Selection = knob (0..1 across the list) + CV at **one item per semitone** (1/12 V), wrapping at both ends, so note sequencers and quantizers can pick words.
- `tools/phrases.txt` → `make phrases` (`tools/make_phrases.py`) → `dsp/src/phrases_generated.cpp`. The script rejects words that aren't in `words.txt`, and `make words` rebuilds phrases too, since word indices can shift. 11 phrases so far.
- VCV: Mode switch (moves to the menu in Phase 3), Select knob with name tooltip, SEL CV jack. The Phrase display highlights the spoken word, underlines the next one, and scrolls long phrases.
- 19 unit tests.

### Phase 3: VCV Rack module
**3a ✅ (2026-11-07):**
- **Formant:** the synth runs at 8 kHz × 2^formant, with pitch and frame timing in real time; ±1 octave on the knob, ±1.5 octaves with CV.
- **Bend:** bitstream bit flips (up to 3%/bit) and frame stutters (up to 50%), both scaling with amount². Reseeded on every trigger.
- **Carrier In + Blend:** the carrier is decimated to the synth rate and normalized to unit power. It replaces voiced excitation only.
- CV inputs for Speed, Formant and Bend.
- Output stage: a safety limiter above 3× (instant attack, 50 ms release), then a tanh soft clipper with its knee at 0.7, so the output stays within ±1. Bend could spike to 16×, and some words reach about 1.3×. Normal words change level by under 0.2 dB; the limiter is active 0% / 0.6% / 2.6% of the time at Bend 0 / 0.5 / 1. The same stage goes before the hardware DAC, so overloads sound the same in VCV and on the module.
- 28 unit tests.

**3b ✅ (2026-11-07): shared display and menu**
- `dsp/include/cvspeak/framebuffer.h`: 128×64 1-bit framebuffer in SSD1306/SH1106 page order (the firmware sends `data()` as-is), plus our own 5×7 font, uppercase only (lowercase prints as uppercase), at 1× or 2×.
- `dsp/include/cvspeak/ui.h`: screens and menu, driven by `turn()`, `push()` and `back()`. The UI owns the settings (mode, word, phrase, intonation, chirp).
  - Main screen, Word mode: the selected word large (2×, or 1× if it doesn't fit), its neighbours above and below, and the word being spoken.
  - Main screen, Phrase mode: the phrase word-wrapped, spoken word inverted, next word underlined, scrolling, word position in the header.
  - The header shows the SEL CV offset (`CV+3`).
  - Menu: Mode, Intonation, Chirp (5% steps).
- **Decided: Intonation, Chirp and Mode move into the menu.** That leaves 5 knobs (Pitch, Speed, Formant, Bend, Blend), as in section 2, and saves about $3–5 of pots per module. Back on the main screen toggles Word/Phrase for quick live switching.
- `SelectCv`: SEL CV with hysteresis (0.15 semitone past the halfway point), so a CV sitting on a boundary doesn't flicker. Shared, so the hardware gets it too.
- VCV:
  - An endless encoder widget (drag or scroll to turn, click to push) plus Back, Speak and the display grouped together at the top.
  - Events are applied on the audio thread.
  - The display is drawn 1.5× the real OLED size by default (one OLED pixel ≈ one screen pixel at 100% zoom). The context menu has a true 1.3" size option.
  - Settings are saved in the patch JSON, with the word and phrase saved by name. v2.0.x patches migrate from their old knob and switch values; the old param ids are kept but hidden.
- `make screens` prints every screen as ASCII art.
- 32 unit tests.

**Remaining (3c):**
- Final SVG panel artwork at the real HP size, with labels as paths, matching section 2 and the layout tried here.
- ✅ Done when: the module is fun to patch. Iterate on the controls here, where changes are cheap.

### Phase 4: Vocoder mode (stretch)
- Channel vocoder in the core, with a tunable band count and an envelope follower.
- Measure its CPU cost and confirm it fits the RP2350 budget at 32 kHz on one core.

### Phase 5: Firmware on a Pico 2 breadboard
- Pico 2 + audio codec breakout + pots + CV scaling circuits on a breadboard.
- Core 0 handles UI and CV; core 1 runs the audio DSP, with I2S driven by DMA.
- Store the word bank in QSPI flash.
- ✅ Done when: the breadboard sounds the same as the VCV version.

### Phase 6: Hardware
- KiCad: ±12 V power, reverse-polarity protection, 3.3 V regulator, op-amp CV scaling and clamps, transistor or comparator gate inputs, ±5 V output buffer.
- 1V/oct calibration routine.
- Panel design, then order the first-revision PCBs.

### Later ideas
- Load custom words and phrases over USB (mass storage or WebSerial).
- A SAM- or Votrax-style phoneme mode for arbitrary words.

## 5. Open decisions
1. **Audio I/O chip**: vocoder mode needs clean audio inputs, and the RP2350's internal ADC is too noisy for that. Proposed: a stereo I2S codec (e.g. WM8731 / WM8960 / TLV320AIC3204) that handles both inputs and the output, in place of a DAC-only PCM5102A. *This affects the hardware, so decide before Phase 5, and choose based on what LCSC has in stock.*
2. **Width**: 14 HP for now. Revisit once the VCV panel layout is settled.
3. **Vocabulary**: what words or phrases to ship, and whose voice records them.
4. ~~**License**~~: decided. GPL-3.0-or-later for everything (`LICENSE`, 2026-11-07).

## 6. Hardware notes (collected during VCV development)

Things the VCV prototype has settled that the firmware and PCB must match, so the module behaves the same as the plugin.

### Audio output
- **The DSP output stage is final and runs before the DAC:** a safety limiter above 3× (instant attack, 50 ms release), then a tanh soft clipper (knee 0.7). The core's output is always strictly within ±1.
- **Map ±1.0 to the DAC's full scale, and full scale to ±5 V at the jack.** The DAC then never clips, and the analog stage never sees more than ±5 V. That's comfortable headroom on ±12 V rails, so rail-to-rail op-amps aren't needed.
- Output buffer: op-amp gain stage to ±5 V, with about 1 kΩ in series at the jack for protection. The DSP removes DC (blocker at about 13 Hz), so DC coupling is fine. Add a coupling capacitor only if the codec's output has a DC offset.
- Bandwidth: speech fills up to about 3.6 kHz × the formant ratio, so about 10 kHz at +1.5 octaves. The codec's own 48 kHz reconstruction filter is enough; no extra analog anti-image filter is needed.
- Firmware: `tanh` only runs above the knee. On the M33, replace it with a cheap rational approximation if profiling shows it matters.

### Inputs
- **Carrier In needs a clean audio ADC** (see open decision 1). The DSP evens out the carrier's level, boosting it by up to 40 dB, so the input's **noise floor matters more than its gain**: an unpatched, noisy input would get boosted and "speak noise".
- **Use switched jacks with insertion detection on Carrier In (and Mod In).** VCV switches the carrier path on with `isConnected()`. The hardware needs the same: a jack with a normalling contact to ground plus a GPIO sense line. Without it, the module can't tell an empty jack from a silent cable.
- CV ranges, to match VCV:
  - Speed and Formant CV: ±5 V sweeps the whole knob range.
  - Bend CV: 0–10 V.
  - **SEL CV: one item per semitone (1/12 V ≈ 83 mV).** A 12-bit ADC across ±5 V gives about 2.4 mV per step, which is plenty. It still needs calibration, plus **hysteresis at item boundaries** so a CV sitting between two items doesn't flicker.
  - V/Oct needs calibration: offset and scale, stored in flash. This is already in Phase 6.
- TRIG and RESET: Schmitt behavior like Rack's (on above about 1 V, re-arms below about 0.1 V). Use a transistor or comparator front end, not the RP2350's internal pull-downs (erratum E9).
- RESET and TRIG arriving on the same step must speak word 1. In firmware, process RESET before TRIG within the same control tick, like the core does.

### Outputs
- EOW: in VCV it's a 1 ms, 10 V pulse. **On hardware, use 5–10 ms.** Some modules miss 1 ms triggers, especially if they scan their inputs slowly. Output 0–10 V (or 0–5 V) through a transistor or op-amp stage.

### CPU budget (to profile in Phase 5)
- The synth's tick rate is 8 kHz × 2^formant, so **at +1.5 octaves it runs at 22.6 kHz. That's the worst case to measure.** Each tick runs a 10-stage lattice filter, `log2`/`exp2` for pitch, a `sqrt`, and the noise or pulse generator.
- Each output sample at 48 kHz: interpolation, a biquad, the limiter, and the soft clip.
- If the worst case is too heavy: compute the pitch math once per frame instead of per tick, or limit Formant to +1 octave on the hardware.
- Bend adds only a xorshift random number per bit read and per frame: negligible.

### Controls
- SPEAK: a momentary tactile button that ORs with TRIG (needed to kick-start an EOW → TRIG self-patch).
- The encoder and back button take over Mode, Intonation and Chirp (decided in 3b): 5 pots, 1 encoder with push, and 2 buttons (Back, Speak).
- Display: 1.3" 128×64 OLED, SH1106, SPI version (7-pin), on the control board. Send the framebuffer by DMA. Its active area is 29.42 × 14.71 mm; the VCV panel reserves that area (drawn 1.5× by default).

### Memory
- The word bank is about 110 bytes per word (51 words ≈ 5.6 KB). Even thousands of words fit easily in QSPI flash.
