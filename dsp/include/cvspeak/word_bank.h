#pragma once
// The built-in vocabulary and phrase banks. The data lives in
// dsp/src/words_generated.cpp (`make words`, from tools/words.txt) and
// dsp/src/phrases_generated.cpp (`make phrases`, from tools/phrases.txt).

#include <cstdint>

namespace cvspeak {

struct LpcWord {
    const char* name;
    const uint8_t* data;  // lpc_format.h bitstream, ends with a stop frame
    uint16_t bytes;
    uint16_t frames;
    float medianPeriod;   // median voiced pitch period, in 8 kHz samples
};

extern const LpcWord kWords[];
extern const int kNumWords;

// A sequence of words for Phrase mode, as indices into kWords.
struct Phrase {
    const char* text;
    const uint16_t* words;
    uint16_t length;
};

extern const Phrase kPhrases[];
extern const int kNumPhrases;

}  // namespace cvspeak
