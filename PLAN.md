# CVSpeak — Project Plan

A Eurorack speech module in the style of the Speak & Spell, built on the RP2350.
We prototype it as a VCV Rack module first, using a DSP core that is shared with the firmware.

## 1. Feature set

### Display and menu (Ornament & Crime style)
- **128×64 OLED** with a **push encoder**, plus a **back/shift button**. Starting point: one encoder and one button. Try it in VCV and add a second encoder only if the menu feels cramped.
- The display shows the current mode, bank or phrase name, and the word being spoken (with the current word highlighted in Phrase mode). It also has a settings menu: retrigger behavior, excitation source, calibration, and so on.
- **Main screen:** turn the encoder to scroll through banks or phrases, and push it to open the menu.
- The UI is shared code: it lives in `dsp/ui/` and draws into a 1-bit 128×64 framebuffer. VCV draws that framebuffer on screen, and the firmware sends it to the OLED. Menus behave the same on both, and we design them in VCV first.

### Speech modes (selected from the menu)
| Mode | Word / Phrase knob + CV | TRIG | RESET |
|---|---|---|---|
| **Word** | Selects a word from the whole vocabulary | Speaks the selected word | — |
| **Phrase** | Selects a phrase (a sentence or paragraph bank) | Speaks the **next word** in the phrase, wrapping at the end | Moves back to the first word |

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
- **Knobs**: Pitch, Speed, Formant, Bend, Blend. Word/Phrase selection is on the encoder, and its CV is added on top.
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
  dsp/ui/     shared menu and screens, drawn into a 128x64 1-bit framebuffer
  tests/      unit tests for the core (make test)
  tools/      Python: WAV → LPC encoding, phrases.txt → C++ tables, render-to-WAV test
  vcv/        VCV Rack plugin wrapper (GPLv3)
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

### Phase 1: LPC core
- TMS5220-style LPC decoder: frame parsing, lattice filter, excitation.
- Encode about 20 test words with python_wizard or BlueWizard. Use our own recordings, not TI ROM data.
- Pitch and speed controls, plus the EOW event.
- ✅ Done when: words render to WAV, and pitch and speed change independently.

### Phase 2: Modes and sequencer
- Word mode and Phrase mode, with reset and retrigger behavior.
- Phrase compiler script (`phrases.txt` → tables).
- ✅ Done when: unit tests pass for advance, wrap, reset, and phrase changes.

### Phase 3: VCV Rack module
- SVG panel at the real HP size, matching section 2.
- Shared UI (`dsp/ui/`): framebuffer, font, main screen, menu. Encoder and button events are driven from the VCV widgets.
- Wire up all params and ports, and add Carrier In with Blend.
- Add Formant and Bend.
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
4. **License**: GPLv3 for everything (simplest), or a closed-source firmware.
