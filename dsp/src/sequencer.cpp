#include "cvspeak/sequencer.h"

#include <cmath>

namespace cvspeak {

int selectIndex(float knob, float cvVolts, int count) {
    if (count <= 0) return 0;
    int base = static_cast<int>(knob * static_cast<float>(count));
    if (base < 0) base = 0;
    if (base >= count) base = count - 1;  // knob fully clockwise
    const int offset = static_cast<int>(std::lround(cvVolts * 12.f));
    const int i = (base + offset) % count;
    return i < 0 ? i + count : i;
}

int PhraseSequencer::next(int phrase) {
    if (count_ <= 0) return -1;
    if (phrase < 0) phrase = 0;
    if (phrase >= count_) phrase = count_ - 1;
    if (phrase != phrase_) {
        phrase_ = phrase;
        pos_ = 0;
    }
    const Phrase& p = phrases_[phrase_];
    if (pos_ >= p.length) pos_ = 0;
    spoken_ = pos_;
    pos_ = (pos_ + 1) % p.length;
    return p.words[spoken_];
}

void PhraseSequencer::reset() {
    pos_ = 0;
    spoken_ = -1;
}

}  // namespace cvspeak
