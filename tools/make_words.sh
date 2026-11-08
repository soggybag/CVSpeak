#!/bin/bash
# Speaks every word in tools/words.txt with macOS `say`, then LPC-encodes
# them into dsp/src/words_generated.cpp. Run via `make words`.
#   VOICE="Daniel" RATE=140 make words    # try other voices or speaking rates
set -eo pipefail
cd "$(dirname "$0")/.."

VOICE="${VOICE:-Reed (English (US))}"
RATE="${RATE:-150}"
OUT_DIR=build/words
mkdir -p "$OUT_DIR"

args=()
while IFS= read -r line || [ -n "$line" ]; do
    case "$line" in ''|\#*) continue ;; esac
    name=$(echo "$line" | tr '[:lower:]' '[:upper:]')
    file="$OUT_DIR/$(echo "$name" | tr ' ' '_').wav"
    say -v "$VOICE" -r "$RATE" -o "$file" --file-format=WAVE --data-format=LEI16@8000 "$line"
    args+=("$name=$file")
done < tools/words.txt

build/lpc_encode dsp/src/words_generated.cpp "${args[@]}"
