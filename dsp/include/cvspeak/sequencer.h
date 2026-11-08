#pragma once
// Speech modes, the Phrase-mode word sequencer, and the knob + CV selection
// mapping. Shared so VCV and the hardware select and step identically.

#include "cvspeak/word_bank.h"

namespace cvspeak {

enum class Mode : uint8_t {
    Word,    // TRIG speaks the selected word
    Phrase,  // TRIG speaks the next word of the selected phrase
};

// Maps a selection knob (0..1) plus CV (volts) to an index in [0, count).
// CV moves one item per semitone (1/12 V), so a sequencer or quantizer can
// pick words by note. Results wrap around at either end.
int selectIndex(float knob, float cvVolts, int count);

// Wraps any index into [0, count).
int wrapIndex(int index, int count);

// SEL CV to an item offset (one item per semitone), with hysteresis so a CV
// sitting near the boundary between two items doesn't flicker between them.
class SelectCv {
public:
    // Hysteresis in semitones beyond the halfway point between items.
    static constexpr float kHysteresis = 0.15f;
    int process(float volts);
    int offset() const { return offset_; }

private:
    int offset_ = 0;
};

class PhraseSequencer {
public:
    PhraseSequencer() = default;
    PhraseSequencer(const Phrase* phrases, int count) : phrases_(phrases), count_(count) {}

    // Returns the word index (into kWords) to speak for this trigger and
    // advances, wrapping at the end. Selecting a different phrase starts it
    // from its first word. Returns -1 if there are no phrases.
    int next(int phrase);

    // The next trigger speaks the first word of the current phrase.
    void reset();

    int phrase() const { return phrase_; }           // -1 before the first trigger
    int spokenPosition() const { return spoken_; }   // word last spoken, -1 after reset
    int nextPosition() const { return pos_; }        // word the next trigger speaks

private:
    const Phrase* phrases_ = kPhrases;
    int count_ = kNumPhrases;
    int phrase_ = -1;
    int pos_ = 0;
    int spoken_ = -1;
};

}  // namespace cvspeak
