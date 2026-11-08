#pragma once
// Display and menu logic, shared by VCV and the firmware: one push encoder
// plus a Back button, drawn into a 128x64 framebuffer.
//
//   Main screen   turn: choose the word or phrase   push: open menu
//                 back: toggle Word / Phrase mode
//   Menu          turn: move between settings       push: edit the setting
//                 back: leave the menu
//   Editing       turn: change the value            push / back: done
//
// The UI owns the settings below; the wrapper saves them (patch JSON in VCV,
// flash on the hardware) and feeds them to the Voice. Intonation is a knob,
// not a setting.

#include "cvspeak/framebuffer.h"
#include "cvspeak/sequencer.h"

namespace cvspeak {

struct Settings {
    Mode mode = Mode::Word;
    int word = 0;             // selection in Word mode (index into kWords)
    int phrase = 0;           // selection in Phrase mode (index into kPhrases)
    float chirp = 0.5f;       // excitation character
};

// What the voice is doing, for the screens. Filled in by the wrapper.
struct UiStatus {
    int selected = 0;         // item the next TRIG uses: selection + SEL CV, wrapped
    int cvOffset = 0;         // SEL CV offset in items
    bool speaking = false;
    int speakingWord = -1;    // word being spoken (index into kWords)
    int seqPhrase = -1;       // the phrase sequencer's state
    int spokenPos = -1;
    int nextPos = 0;
};

class Ui {
public:
    enum class Screen : uint8_t { Main, Menu };
    enum Item : uint8_t { kItemMode, kItemChirp, kNumItems };

    void turn(int detents);
    void push();
    void back();

    Settings& settings() { return settings_; }
    const Settings& settings() const { return settings_; }
    // Keeps the selections in range (e.g. after loading a patch saved with a
    // different vocabulary).
    void clampSettings();

    Screen screen() const { return screen_; }
    int cursor() const { return cursor_; }
    bool editing() const { return editing_; }

    void render(Framebuffer& fb, const UiStatus& status) const;

private:
    void renderWordMode(Framebuffer& fb, const UiStatus& status) const;
    void renderPhraseMode(Framebuffer& fb, const UiStatus& status) const;
    void renderMenu(Framebuffer& fb) const;
    void editItem(int detents);

    Settings settings_;
    Screen screen_ = Screen::Main;
    int cursor_ = 0;
    bool editing_ = false;
};

}  // namespace cvspeak
