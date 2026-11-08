#!/bin/bash
# Speaks every vocabulary word with macOS `say`, then LPC-encodes them into
# dsp/src/words_generated.cpp. The vocabulary is tools/words.txt plus any new
# words used in tools/phrases.txt (see make_phrases.py). Run via `make words`.
#   VOICE="Daniel" RATE=140 make words    # try other voices or speaking rates
# A recording at tools/recordings/<word>.wav (lower case, any rate/channels)
# is used instead of `say` for that word.
set -eo pipefail
cd "$(dirname "$0")/.."

VOICE="${VOICE:-Reed (English (US))}"
RATE="${RATE:-150}"
OUT_DIR=build/words
RECORDINGS=tools/recordings
mkdir -p "$OUT_DIR"

python3 tools/make_phrases.py --vocabulary > "$OUT_DIR/vocabulary.tsv"

args=()
while IFS=$'\t' read -r name spoken; do
    file="$OUT_DIR/$name.wav"
    recording="$RECORDINGS/$(echo "$name" | tr '[:upper:]' '[:lower:]').wav"
    if [ -f "$recording" ]; then
        echo "using $recording"
        afconvert -f WAVE -d LEI16@8000 -c 1 "$recording" "$file"
    else
        say -v "$VOICE" -r "$RATE" -o "$file" --file-format=WAVE --data-format=LEI16@8000 "$spoken"
    fi
    args+=("$name=$file")
done < "$OUT_DIR/vocabulary.tsv"

build/desktop/lpc_encode dsp/src/words_generated.cpp "${args[@]}"
