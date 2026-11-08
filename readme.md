# CVSpeak

CVSpeak is a Eurorack speech synthesizer in the spirit of the Speak & Spell. It speaks words and phrases with a gritty 8 kHz LPC voice. You trigger words from a sequencer, play them at a pitch with 1V/oct, stretch their timing, and step through whole sentences one trigger at a time.

The hardware will be an RP2350-based module with an OLED display and an encoder, in the style of Ornament & Crime, and will be built with SMD parts assembled by JLCPCB. The module is being prototyped first as a **VCV Rack 2 plugin**. The plugin and the future firmware share the same C++ DSP core, so what you hear in Rack is what the module will do.

**Status:** early development. The speech engine, Word and Phrase modes and the VCV plugin work. The panel design, display menus, vocoder and hardware are still to come. See [PLAN.md](PLAN.md) for the full roadmap.

## Features

- **LPC speech engine** modeled on the TMS5220 (the Speak & Spell chip family): 8 kHz, 10-pole lattice filter, 25 ms frames. It uses its own encoder and quantization tables, not TI ROM data.
- **Word mode**: TRIG speaks the selected word.
- **Phrase mode**: each TRIG speaks the *next* word of a sentence, RESET goes back to the first word, and EOW (end of word) can be patched back into TRIG to speak the whole phrase.
- **Pitch** (1V/oct) and **Speed** (0.25–4×) are independent of each other.
- **Intonation**: from a flat, singable robot monotone to the word's natural pitch contour.
- **Chirp**: from a clean pulse to a buzzy, metallic TI-style excitation.
- **Select CV** at one word or phrase per semitone, so a note sequencer can pick words.
- A tiny vocabulary footprint of about 100 bytes per word. 51 words take about 5.6 KB.

## Download

Prebuilt plugins for Windows, Linux, Intel Macs and Apple Silicon Macs are on the [Releases page](https://github.com/soggybag/CVSpeak/releases). Download the `.vcvplugin` file for your system. In Rack 2, choose **Help → Open user folder**, put the file in the `plugins-…` folder there, and restart Rack.

## VCV Rack module

| Control | What it does |
|---|---|
| **SELECT** | Chooses the word (Word mode) or phrase (Phrase mode). The tooltip shows its name. |
| **PITCH** / **V/OCT** | Speaking pitch. The word's median pitch lands on this note (default C3). |
| **SPEED** | How fast the word is spoken, without changing pitch. |
| **INTONE** | 0 = monotone at the set pitch, 1 = the recorded pitch contour. |
| **CHIRP** | Excitation character, from clean to TI-style buzz. |
| **MODE** | Word or Phrase. |
| **TRIG** / **SPEAK** button | Speaks the word, or the next word in a phrase. The button can kick-start an EOW → TRIG loop. |
| **RESET** | Phrase mode: the next TRIG speaks the first word. It doesn't cut off the current word, and RESET plus TRIG together speak word 1. |
| **SEL CV** | Adds to SELECT, one item per semitone (1/12 V), wrapping at the ends. |
| **EOW** | A trigger when each word finishes. |
| **OUT** | Audio, about ±5 V. |

## Building

You'll need macOS with the Xcode command line tools, plus VCV Rack 2 Free from <https://vcvrack.com/Rack>. Rack 1.x can't load this plugin. Creating new words also needs macOS, for `say` and `afconvert`, but building doesn't, because the generated word data is committed.

Download the Rack SDK once:

```sh
mkdir -p sdk && cd sdk
curl -LO https://vcvrack.com/downloads/Rack-SDK-2.6.6-mac-arm64.zip
unzip Rack-SDK-2.6.6-mac-arm64.zip && rm Rack-SDK-2.6.6-mac-arm64.zip
cd ..
```

Then:

```sh
make test          # run the DSP core's unit tests
make render        # speak every word into build/render.wav (no Rack needed)
make               # build the VCV Rack plugin
make install       # package it into Rack 2's plugin folder
make dist          # package dist/CVSpeak-<version>-<os>-<cpu>.vcvplugin to share
```

Restart Rack after installing, since Rack only loads plugins at startup. If you'd rather skip packaging while iterating, run `make vcv-link` once. It links Rack's plugin folder to this repository, and after that each change only needs `make` and a Rack restart. Don't mix the two: `make install` would put a packaged copy next to the link, so use `make vcv-install`, which refuses to run while the link exists.

The repository root is the plugin folder (`plugin.json`, `src/`, `res/`), which is the layout the VCV Library build expects.

### Releases

GitHub Actions ([`.github/workflows/build.yml`](.github/workflows/build.yml)) runs the unit tests and builds all four platforms on every push to `main` and every pull request. Those builds can be downloaded from the run's **Artifacts** section. To publish a release:

1. Bump `"version"` in `plugin.json` (e.g. `2.0.2`). The major number stays 2 to match Rack 2.
2. Commit, then tag and push: `git tag v2.0.2 && git push origin main v2.0.2`.

The workflow attaches the four `.vcvplugin` files to a new GitHub Release. It refuses to publish if the tag doesn't match `plugin.json`'s version.

## Creating words and phrases

All of the vocabulary is generated from two text files:

| File | Contents |
|---|---|
| [`tools/phrases.txt`](tools/phrases.txt) | The phrases for Phrase mode, one per line |
| [`tools/words.txt`](tools/words.txt) | Optional: word order, extra words, and pronunciation fixes |

### Quick start

1. Edit `tools/phrases.txt`, one phrase per line:
   ```
   hello I am a robot
   The world is indeed comic, but the joke is on mankind
   ```
   Punctuation and capitalization are ignored, and lines starting with `#` are comments.
2. Run `make words`.
3. Run `make install` and restart Rack.

`make words` finds every word used in your phrases. It speaks any word that isn't in the bank yet with macOS `say`, encodes everything, and compiles the phrases. You don't have to list words separately.

### How it works

```
words.txt + phrases.txt
   │  tools/make_phrases.py --vocabulary      (vocabulary = words.txt, then new phrase words)
   ▼
say / your recording → build/words/NAME.wav   (8 kHz, 16-bit mono)
   │  build/lpc_encode                        (LPC analysis → compact bitstream)
   ▼
dsp/src/words_generated.cpp                   (the word bank)
   │  tools/make_phrases.py
   ▼
dsp/src/phrases_generated.cpp                 (phrases as lists of word numbers)
```

For each word, the encoder:
1. Normalizes the recording's level and trims silence off both ends.
2. Analyzes it in 25 ms frames, measuring loudness, voiced (buzz) or unvoiced (hiss), pitch, and ten filter coefficients.
3. Packs each frame into a few bits.

It prints a summary line for each word:

```
HELLO         23 frames  119 bytes  voiced  69%  median 105.1 Hz  energy 1..11
```

A median pitch far from the others (e.g. 75 Hz when most words are around 105 Hz) usually means a creaky ending in the recording. That mostly matters when INTONE is turned up.

Don't edit the two `*_generated.cpp` files by hand. They're overwritten on every run, and they're committed so the project builds without macOS.

### Options

**Fix a pronunciation.** In `words.txt`, write `word = how to say it`. The module still shows and stores the word's real spelling; `say` speaks the text after the `=`:

```
cthulhu = kuh thoo loo
nyarlathotep = nyar lath oh tep
```

**Change the voice or speaking rate.** This applies to every word that isn't using a recording:

```sh
VOICE="Daniel" make words
VOICE="Fred" RATE=120 make words     # RATE is in words per minute (default 150)
say -v '?'                           # list the installed voices
```

The default voice is Reed (English (US)). Clear, low-pitched voices encode best. The 10-pole, 8 kHz model loses detail on high, breathy voices.

**Use your own recordings.** Save a recording as `tools/recordings/<word>.wav`, with the file name in lower case, e.g. `tools/recordings/robot.wav`. `make words` will use it instead of `say`. Any sample rate and channel count works, because `afconvert` converts it to 8 kHz mono. For best results:
- Put one word in each file.
- Record in a quiet room, close to the microphone.
- Leading and trailing silence is fine, because it gets trimmed.

Recordings aren't committed by default, because `.gitignore` excludes `*.wav`. Add `!tools/recordings/*.wav` to `.gitignore` if you want to share them.

**Add words without using them in a phrase.** List them in `words.txt`, one per line. They become selectable in Word mode.

**Control word order.** A word's position in the vocabulary decides which SEL CV semitone selects it in Word mode: `words.txt` order first, then new words in the order they first appear in `phrases.txt`. Inserting a word earlier in `words.txt` shifts the numbering of the words after it.

**Rebuild phrases only.** If you just rearranged existing words, `make phrases` is enough. If a phrase uses a word that isn't in the bank, it says so and asks you to run `make words`.

### What to expect

LPC at 8 kHz is meant to sound robotic, and that's the charm. A few things are normal:
- Consonants like "s", "t", "k" and "p" are played as short bursts of hiss, because that's how they're encoded. Hard consonants at the start or end of a word, such as the "k" in "comic" or the "p" in "-tep", come out as brief clicks or puffs.
- Loud noise in the middle of a *vowel* is a bug: the encoder misjudged a vowel as hiss. The encoder guards against this, and `make test` checks for it, but if you hear one, note the word and open an issue.
- Some words peak a little above full scale (about 1.3×). That's fine in Rack, and the hardware will have headroom for it.

## Project layout

| Folder | What |
|---|---|
| `dsp/` | Shared C++17 DSP core: no Rack or Pico dependencies, float only, no allocation in audio code |
| `tests/` | Unit tests for the core (`make test`) |
| `tools/` | Word/phrase sources, encoder, render tool and generation scripts |
| `src/`, `res/`, `plugin.json` | The VCV Rack 2 plugin: a thin wrapper around `dsp/`, its panel, and its manifest |
| `firmware/` | RP2350 firmware (planned) |
| `hardware/` | KiCad project (planned) |
| `sdk/` | Rack SDK, downloaded locally and not committed |

## License

GPL-3.0-or-later. See [LICENSE](LICENSE).
