#pragma once
// 128x64 1-bit framebuffer and a 5x7 pixel font, shared by VCV and the
// firmware. VCV draws the pixels on screen; the firmware sends data() to the
// OLED as-is.

#include <cstdint>

namespace cvspeak {

class Framebuffer {
public:
    static constexpr int kWidth = 128;
    static constexpr int kHeight = 64;
    static constexpr int kBytes = kWidth * kHeight / 8;

    // Font metrics: 5x7 glyphs in a 6x8 cell, all multiplied by `scale`.
    static constexpr int kGlyphWidth = 5;
    static constexpr int kGlyphHeight = 7;
    static constexpr int kAdvance = 6;

    void clear();
    void setPixel(int x, int y, bool on = true);  // ignores pixels off-screen
    bool pixel(int x, int y) const;
    void fillRect(int x, int y, int w, int h, bool on = true);
    void hline(int x, int y, int w, bool on = true) { fillRect(x, y, w, 1, on); }

    // Draws text with its top-left corner at (x, y), setting glyph pixels to
    // `on` and leaving the rest untouched (so it can draw dark text onto a
    // filled box). Lowercase prints as uppercase. Returns textWidth(text).
    int drawText(int x, int y, const char* text, int scale = 1, bool on = true);
    static int textWidth(const char* text, int scale = 1);

    // SSD1306/SH1106 page order: byte (y / 8) * 128 + x holds 8 pixels in a
    // column, with bit (y % 8).
    const uint8_t* data() const { return data_; }

private:
    void drawGlyph(int x, int y, char c, int scale, bool on);
    uint8_t data_[kBytes] = {};
};

}  // namespace cvspeak
