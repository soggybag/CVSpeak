# CVSpeak hardware

The Eurorack module: 14 HP, one PCB plus a front panel, with a Raspberry Pi Pico 2 and an ES8388 codec. The firmware lives in `../firmware/`, and the DSP core it shares with the VCV plugin lives in `../dsp/`.

## Docs

- [Hardware decisions](docs/Hardware%20Decisions.md): what was decided and why, the circuit blocks, open items, and what to bench on the Palimpsest boards first.
- [Pin map](docs/Pin%20map.md): Pico 2 GPIO, mux channels, and the signal conventions for the firmware.
- Panel layout: `../tools/make_panel.py` (`make panel`). It is the single source of the hole and part positions, shared with the VCV panel.

## Status

2026-11-07: decisions and pin map drafted. No schematic yet.
