// CVSpeak firmware: Raspberry Pi Pico 2 + ES8388 + SH1106 OLED.
//
// Core 0: codec setup, controls (1 kHz), UI and display (30 Hz).
// Core 1: audio (audio.cpp), paced by the codec at 48 kHz.
// The sound and the menus come from the same dsp/ core as the VCV plugin.

#include <cmath>
#include <cstdio>

#include "audio.h"
#include "controls.h"
#include "cvspeak/framebuffer.h"
#include "cvspeak/ui.h"
#include "es8388.h"
#include "hardware/clocks.h"
#include "hardware/i2c.h"
#include "hardware/spi.h"
#include "pico/stdlib.h"
#include "pins.h"
#include "oled.h"
#include "shared.h"

Shared g_shared;

namespace {

constexpr uint32_t kDisplayEveryMs = 33;
constexpr uint32_t kReportEveryMs = 1000;
constexpr int kDebounceMs = 3;

// A button counts as pressed or released after kDebounceMs steady scans.
struct Debounce {
    bool state = false;
    int count = 0;
    bool pressedEdge(bool raw) {
        if (raw == state) {
            count = 0;
            return false;
        }
        if (++count < kDebounceMs) return false;
        state = raw;
        count = 0;
        return state;
    }
};

void startCodecClock() {
    // 12 MHz straight from the crystal: the ES8388's USB mode (48 kHz = MCLK/250).
    clock_gpio_init(pins::kCodecMclk, CLOCKS_CLK_GPOUT0_CTRL_AUXSRC_VALUE_XOSC_CLKSRC, 1.f);
}

// Knob and CV mapping, the same as the VCV module (src/CVSpeak.cpp).
Controls mapControls(const controls::Readings& r, const cvspeak::Settings& s, int selected) {
    Controls c;
    c.pitch = (-2.f + 4.f * r.pitch) + r.voct;
    c.speed = std::exp2((-2.f + 4.f * r.speed) + 0.4f * r.speedCv);
    c.formant = (-1.f + 2.f * r.formant) + 0.2f * r.formantCv;
    c.bend = r.bend + 0.1f * r.bendCv;
    c.blend = r.blend;
    c.intonation = r.intonation;
    c.chirp = s.chirp;
    c.mode = s.mode;
    c.selected = selected;
    c.carrierPatched = r.carrierPatched;
    return c;
}

}  // namespace

int main() {
    stdio_init_all();
    startCodecClock();

    i2c_init(i2c0, 100 * 1000);
    gpio_set_function(pins::kI2cSda, GPIO_FUNC_I2C);
    gpio_set_function(pins::kI2cScl, GPIO_FUNC_I2C);
    sleep_ms(10);
    const bool codecOk = es8388::init(i2c0);

    spi_init(spi0, 8 * 1000 * 1000);
    gpio_set_function(pins::kOledSck, GPIO_FUNC_SPI);
    gpio_set_function(pins::kOledMosi, GPIO_FUNC_SPI);
    oled::init(spi0, pins::kOledDc, pins::kOledRes);

    controls::init();
    audio::start();

    cvspeak::Ui ui;
    cvspeak::Framebuffer fb;
    cvspeak::SelectCv selectCv;
    controls::Readings readings;
    Debounce back, push, speak;

    absolute_time_t next = get_absolute_time();
    uint32_t ms = 0;
    for (;;) {
        next = delayed_by_ms(next, 1);
        sleep_until(next);
        ++ms;

        controls::scan(readings);
        ui.turn(controls::takeEncoderDetents());
        if (push.pressedEdge(readings.encoderPush)) ui.push();
        if (back.pressedEdge(readings.back)) ui.back();
        if (speak.pressedEdge(readings.speak)) g_shared.speakPresses = g_shared.speakPresses + 1;

        const cvspeak::Settings& s = ui.settings();
        const bool phrase = s.mode == cvspeak::Mode::Phrase;
        const int count = phrase ? cvspeak::kNumPhrases : cvspeak::kNumWords;
        const int cvOffset = selectCv.process(readings.selectCv);
        const int selected = cvspeak::wrapIndex((phrase ? s.phrase : s.word) + cvOffset, count);
        g_shared.publish(mapControls(readings, s, selected));

        if (ms % kDisplayEveryMs == 0) {
            const Status& st = g_shared.status;
            cvspeak::UiStatus us;
            us.selected = selected;
            us.cvOffset = cvOffset;
            us.speaking = st.speaking;
            us.speakingWord = st.currentWord;
            us.seqPhrase = st.seqPhrase;
            us.spokenPos = st.spokenPos;
            us.nextPos = st.nextPos;
            ui.render(fb, us);
            oled::show(fb.data());
        }

        if (ms % kReportEveryMs == 0) {
            const uint32_t load = g_shared.status.loadPermille;
            std::printf("codec %s, core 1 load %lu.%lu%%\n", codecOk ? "ok" : "NOT FOUND",
                        static_cast<unsigned long>(load / 10), static_cast<unsigned long>(load % 10));
        }
    }
}
