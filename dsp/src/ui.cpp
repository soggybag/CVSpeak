#include "cvspeak/ui.h"

#include <cmath>

namespace cvspeak {

namespace {

constexpr int W = Framebuffer::kWidth;
constexpr int kHeaderRule = 9;   // y of the line under the header
constexpr float kPercentStep = 0.05f;

// Tiny formatter (no printf: keeps the firmware small and allocation-free).
struct Text {
    char buf[32];
    int n = 0;
    Text() { buf[0] = 0; }
    Text& add(const char* s) {
        while (*s && n < 31) buf[n++] = *s++;
        buf[n] = 0;
        return *this;
    }
    Text& add(int v, int minDigits = 1) {
        char tmp[12];
        int k = 0;
        const bool neg = v < 0;
        unsigned u = neg ? 0u - static_cast<unsigned>(v) : static_cast<unsigned>(v);
        do {
            tmp[k++] = static_cast<char>('0' + u % 10);
            u /= 10;
        } while (u && k < 11);
        while (k < minDigits && k < 11) tmp[k++] = '0';
        if (neg && n < 31) buf[n++] = '-';
        while (k && n < 31) buf[n++] = tmp[--k];
        buf[n] = 0;
        return *this;
    }
    const char* str() const { return buf; }
};

int countFor(Mode m) { return m == Mode::Phrase ? kNumPhrases : kNumWords; }

int percent(float v) { return static_cast<int>(std::lround(v * 100.f)); }

float stepPercent(float v, int detents) {
    v = std::round(v / kPercentStep + static_cast<float>(detents)) * kPercentStep;
    return v < 0.f ? 0.f : (v > 1.f ? 1.f : v);
}

void drawRight(Framebuffer& fb, int y, const char* s, int right = W) {
    fb.drawText(right - Framebuffer::textWidth(s), y, s);
}

void drawCentered(Framebuffer& fb, int y, const char* s, int scale = 1, bool on = true) {
    fb.drawText((W - Framebuffer::textWidth(s, scale)) / 2, y, s, scale, on);
}

// Header: "TITLE 03/13", then the SEL CV offset, and `right` at the right edge.
void drawHeader(Framebuffer& fb, const char* title, int index, int count, int cvOffset,
                const char* right = "") {
    Text t;
    t.add(title).add(" ").add(index + 1, 2).add("/").add(count, 2);
    int x = fb.drawText(0, 0, t.str());
    if (cvOffset != 0) {
        Text cv;
        cv.add("CV").add(cvOffset > 0 ? "+" : "").add(cvOffset);
        fb.drawText(x + Framebuffer::kAdvance, 0, cv.str());
    }
    drawRight(fb, 0, right);
    fb.hline(0, kHeaderRule, W);
}

}  // namespace

void Ui::clampSettings() {
    settings_.word = kNumWords > 0 ? wrapIndex(settings_.word, kNumWords) : 0;
    settings_.phrase = kNumPhrases > 0 ? wrapIndex(settings_.phrase, kNumPhrases) : 0;
    settings_.chirp = stepPercent(settings_.chirp, 0);
}

void Ui::turn(int detents) {
    if (detents == 0) return;
    if (screen_ == Screen::Main) {
        int& sel = settings_.mode == Mode::Phrase ? settings_.phrase : settings_.word;
        sel = wrapIndex(sel + detents, countFor(settings_.mode));
    } else if (editing_) {
        editItem(detents);
    } else {
        cursor_ += detents;
        cursor_ = cursor_ < 0 ? 0 : (cursor_ >= kNumItems ? kNumItems - 1 : cursor_);
    }
}

void Ui::push() {
    if (screen_ == Screen::Main) {
        screen_ = Screen::Menu;
        editing_ = false;
    } else {
        editing_ = !editing_;
    }
}

void Ui::back() {
    if (screen_ == Screen::Main) {
        settings_.mode = settings_.mode == Mode::Word ? Mode::Phrase : Mode::Word;
    } else if (editing_) {
        editing_ = false;
    } else {
        screen_ = Screen::Main;
    }
}

void Ui::editItem(int detents) {
    switch (cursor_) {
        case kItemMode:
            if (detents % 2 != 0)
                settings_.mode = settings_.mode == Mode::Word ? Mode::Phrase : Mode::Word;
            break;
        case kItemChirp: settings_.chirp = stepPercent(settings_.chirp, detents); break;
        default: break;
    }
}

void Ui::render(Framebuffer& fb, const UiStatus& status) const {
    fb.clear();
    if (screen_ == Screen::Menu) {
        renderMenu(fb);
    } else if (settings_.mode == Mode::Phrase && kNumPhrases > 0) {
        renderPhraseMode(fb, status);
    } else {
        renderWordMode(fb, status);
    }
}

// The selected word large, with its neighbours above and below so turning
// the encoder feels like scrolling a list.
void Ui::renderWordMode(Framebuffer& fb, const UiStatus& status) const {
    drawHeader(fb, "WORD", status.selected, kNumWords, status.cvOffset);
    if (kNumWords <= 0) return;
    const int sel = wrapIndex(status.selected, kNumWords);

    if (kNumWords > 1) {
        drawCentered(fb, 13, kWords[wrapIndex(sel - 1, kNumWords)].name);
        drawCentered(fb, 45, kWords[wrapIndex(sel + 1, kNumWords)].name);
    }
    const char* name = kWords[sel].name;
    if (Framebuffer::textWidth(name, 2) <= W) {
        drawCentered(fb, 24, name, 2);
    } else {
        drawCentered(fb, 28, name);
    }

    if (status.speaking && status.speakingWord >= 0 && status.speakingWord < kNumWords) {
        Text t;
        t.add("> ").add(kWords[status.speakingWord].name);
        fb.drawText(0, 56, t.str());
    }
}

// The selected phrase, word-wrapped. The word being spoken is inverted and
// the word the next TRIG speaks is underlined. Long phrases scroll.
void Ui::renderPhraseMode(Framebuffer& fb, const UiStatus& status) const {
    const int sel = wrapIndex(status.selected, kNumPhrases);
    const Phrase& phrase = kPhrases[sel];
    int spoken = -1, next = 0;
    if (status.seqPhrase == sel) {
        spoken = status.speaking ? status.spokenPos : -1;
        next = status.nextPos;
    }
    Text pos;  // word position in the phrase
    pos.add((spoken >= 0 ? spoken : next) + 1).add("/").add(phrase.length);
    drawHeader(fb, "PHR", sel, kNumPhrases, status.cvOffset, pos.str());
    if (phrase.length <= 0) return;

    constexpr int kMaxWords = 128;
    constexpr int kTop = 13, kLineHeight = 10, kLines = 5;
    int xs[kMaxWords], lines[kMaxWords];
    const int n = phrase.length < kMaxWords ? phrase.length : kMaxWords;
    // 1 px margins so the spoken word's inverted box isn't clipped.
    constexpr int kLeft = 1, kRight = W - 1;
    int x = kLeft, line = 0;
    for (int i = 0; i < n; ++i) {
        const int w = Framebuffer::textWidth(kWords[phrase.words[i]].name);
        if (x > kLeft && x + w > kRight) {
            x = kLeft;
            ++line;
        }
        xs[i] = x;
        lines[i] = line;
        x += w + Framebuffer::kAdvance;
    }

    const int focus = spoken >= 0 ? spoken : (next < n ? next : 0);
    int first = lines[focus] - (kLines - 2);  // keep a line of context below
    if (first > lines[n - 1] - (kLines - 1)) first = lines[n - 1] - (kLines - 1);
    if (first < 0) first = 0;

    for (int i = 0; i < n; ++i) {
        const int l = lines[i] - first;
        if (l < 0 || l >= kLines) continue;
        const char* word = kWords[phrase.words[i]].name;
        const int y = kTop + l * kLineHeight;
        const int w = Framebuffer::textWidth(word);
        if (i == spoken) {
            fb.fillRect(xs[i] - 1, y - 1, w + 2, Framebuffer::kGlyphHeight + 2);
            fb.drawText(xs[i], y, word, 1, false);
        } else {
            fb.drawText(xs[i], y, word);
        }
        if (i == next && i != spoken) fb.hline(xs[i], y + Framebuffer::kGlyphHeight + 1, w);
    }

    // Scroll hints when lines are hidden.
    if (first > 0) fb.fillRect(W - 3, kTop, 3, 1);
    if (lines[n - 1] - first >= kLines) fb.fillRect(W - 3, kTop + kLines * kLineHeight - 3, 3, 1);
}

void Ui::renderMenu(Framebuffer& fb) const {
    fb.drawText(0, 0, "MENU");
    drawRight(fb, 0, editing_ ? "EDIT" : "");
    fb.hline(0, kHeaderRule, W);

    constexpr int kTop = 13, kRowHeight = 10;
    for (int i = 0; i < kNumItems; ++i) {
        const char* label = "";
        Text value;
        switch (i) {
            case kItemMode:
                label = "MODE";
                value.add(settings_.mode == Mode::Phrase ? "PHRASE" : "WORD");
                break;
            case kItemChirp:
                label = "CHIRP";
                value.add(percent(settings_.chirp)).add("%");
                break;
            default: break;
        }
        const int y = kTop + i * kRowHeight;
        const int vw = Framebuffer::textWidth(value.str());
        const int vx = W - 2 - vw;
        if (i == cursor_ && !editing_) {
            fb.fillRect(0, y - 1, W, Framebuffer::kGlyphHeight + 2);
            fb.drawText(2, y, label, 1, false);
            fb.drawText(vx, y, value.str(), 1, false);
        } else if (i == cursor_) {
            fb.drawText(2, y, label);
            fb.fillRect(vx - 2, y - 1, vw + 4, Framebuffer::kGlyphHeight + 2);
            fb.drawText(vx, y, value.str(), 1, false);
        } else {
            fb.drawText(2, y, label);
            fb.drawText(vx, y, value.str());
        }
    }
}

}  // namespace cvspeak
