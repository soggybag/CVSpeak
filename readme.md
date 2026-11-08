# CVSpeak

A Eurorack speech synthesizer module in the style of the Speak & Spell, built on the RP2350.
It is prototyped first as a VCV Rack plugin, and the plugin shares its DSP core with the firmware.

See [PLAN.md](PLAN.md) for the features, architecture and roadmap.

## Layout

| Folder | What |
|---|---|
| `dsp/` | Shared C++17 DSP core (no Rack or Pico dependencies, float only) |
| `tests/` | Unit tests for the core |
| `tools/` | Desktop tools: render to WAV, and later the word encoding scripts |
| `vcv/` | VCV Rack 2 plugin wrapper |
| `firmware/` | RP2350 firmware (Phase 5) |
| `hardware/` | KiCad project (Phase 6) |
| `sdk/` | Rack SDK, downloaded locally and not committed |

## Setup

Requires the Xcode command line tools.

```sh
# Rack SDK (one time)
mkdir -p sdk && cd sdk
curl -LO https://vcvrack.com/downloads/Rack-SDK-2.6.6-mac-arm64.zip
unzip Rack-SDK-2.6.6-mac-arm64.zip && rm Rack-SDK-2.6.6-mac-arm64.zip
cd ..
```

Install **VCV Rack 2 Free** from <https://vcvrack.com/Rack>. Rack 1.x can't load this plugin.

## Build

```sh
make test          # run the core's unit tests
make render        # render the core to build/render.wav
make vcv           # build the VCV Rack plugin
make vcv-install   # build and install into your Rack 2 user folder
```

After `make vcv-install`, restart Rack and add **CVSpeak** from the module browser.
