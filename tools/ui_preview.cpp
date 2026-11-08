// Prints the display screens as ASCII art, for checking layouts without Rack.
//   make screens

#include <cstdio>

#include "cvspeak/ui.h"

using namespace cvspeak;

static void show(const char* title, const Ui& ui, const UiStatus& status) {
    Framebuffer fb;
    ui.render(fb, status);
    std::printf("%s\n+", title);
    for (int x = 0; x < Framebuffer::kWidth; ++x) std::putchar('-');
    std::printf("+\n");
    for (int y = 0; y < Framebuffer::kHeight; ++y) {
        std::putchar('|');
        for (int x = 0; x < Framebuffer::kWidth; ++x) std::putchar(fb.pixel(x, y) ? '#' : ' ');
        std::printf("|\n");
    }
    std::putchar('+');
    for (int x = 0; x < Framebuffer::kWidth; ++x) std::putchar('-');
    std::printf("+\n\n");
}

int main() {
    Ui ui;
    UiStatus st;

    ui.settings().word = 30;  // NYARLATHOTEP: too wide for the large font
    st.selected = 3;
    st.speaking = true;
    st.speakingWord = 3;
    show("Word mode, speaking", ui, st);

    st.selected = 30;
    st.cvOffset = -2;
    st.speaking = false;
    show("Word mode, long word, SEL CV -2", ui, st);

    ui.back();  // -> Phrase mode
    st = UiStatus{};
    st.selected = 11;
    st.seqPhrase = 11;
    st.speaking = true;
    st.spokenPos = 14;
    st.nextPos = 15;
    show("Phrase mode, speaking word 15 of a long phrase", ui, st);

    ui.push();
    ui.turn(1);
    show("Menu, cursor on Chirp", ui, st);
    ui.push();
    ui.turn(-3);
    show("Menu, editing Chirp", ui, st);
    return 0;
}
