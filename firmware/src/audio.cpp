#include "audio.h"

#include <cstdint>
#include <initializer_list>

#include "cvspeak/voice.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/timer.h"
#include "i2s.pio.h"
#include "pico/multicore.h"
#include "pins.h"
#include "shared.h"

namespace audio {

namespace {

constexpr int kBlock = 32;            // control values are re-read every block
constexpr int kEowSamples = 384;      // 8 ms trigger (hardware notes: 5-10 ms)

PIO pio = pio0;
uint sm = 0;
cvspeak::Voice voice;

int16_t toPcm(float x) {
    const float s = x * 32767.f;
    return static_cast<int16_t>(s > 32767.f ? 32767.f : (s < -32768.f ? -32768.f : s));
}

void core1Main() {
    voice.setSampleRate(kSampleRate);

    bool trigWas = false, resetWas = false;
    uint32_t speakSeen = g_shared.speakPresses;
    int eowLeft = 0;
    uint32_t busyUs = 0, samples = 0;

    for (;;) {
        const Controls c = g_shared.read();
        cvspeak::VoiceInputs in;
        in.pitch = c.pitch;
        in.speed = c.speed;
        in.intonation = c.intonation;
        in.chirp = c.chirp;
        in.formant = c.formant;
        in.bend = c.bend;
        in.blend = c.blend;
        in.mode = c.mode;
        in.word = c.selected;
        in.phrase = c.selected;

        for (int i = 0; i < kBlock; ++i) {
            // The codec clocks the loop: one frame in per sample at 48 kHz.
            const uint32_t frame = pio_sm_get_blocking(pio, sm);
            const uint32_t t0 = time_us_32();

            float carrier = static_cast<float>(static_cast<int16_t>(frame >> 16)) / 32768.f;
            in.carrier = c.carrierPatched ? &carrier : nullptr;

            // Gate stages invert: the pin reads low while the gate is high.
            const bool trig = !gpio_get(pins::kGateTrig);
            const bool reset = !gpio_get(pins::kGateReset);
            const uint32_t presses = g_shared.speakPresses;
            in.trig = (trig && !trigWas) || presses != speakSeen;
            in.reset = reset && !resetWas;
            trigWas = trig;
            resetWas = reset;
            speakSeen = presses;

            float out;
            cvspeak::VoiceOutputs outs;
            voice.process(in, &out, 1, outs);
            in.trig = in.reset = false;

            const int16_t pcm = toPcm(out);
            const uint32_t word = (static_cast<uint32_t>(static_cast<uint16_t>(pcm)) << 16) |
                                  static_cast<uint16_t>(pcm);
            if (!pio_sm_is_tx_fifo_full(pio, sm)) pio_sm_put(pio, sm, word);

            if (outs.eow) eowLeft = kEowSamples;
            gpio_put(pins::kEowOut, eowLeft > 0);
            if (eowLeft > 0) --eowLeft;

            busyUs += time_us_32() - t0;
            if (++samples == static_cast<uint32_t>(kSampleRate)) {
                g_shared.status.loadPermille = busyUs / 1000u;  // of 1,000,000 us
                busyUs = 0;
                samples = 0;
            }
        }

        const bool speaking = voice.speaking();
        gpio_put(pins::kLedSpeak, speaking);
        Status& st = g_shared.status;
        st.speaking = speaking;
        st.currentWord = voice.currentWord();
        st.seqPhrase = voice.sequencer().phrase();
        st.spokenPos = voice.sequencer().spokenPosition();
        st.nextPos = voice.sequencer().nextPosition();
    }
}

}  // namespace

void start() {
    for (unsigned p : {pins::kGateTrig, pins::kGateReset}) {
        gpio_init(p);
        gpio_set_dir(p, GPIO_IN);
        gpio_pull_up(p);  // the board has 10k pull-ups; this helps on a breadboard (never pull-downs: RP2350-E9)
    }
    for (unsigned p : {pins::kEowOut, pins::kLedSpeak}) {
        gpio_init(p);
        gpio_set_dir(p, GPIO_OUT);
        gpio_put(p, 0);
    }

    const uint offset = pio_add_program(pio, &i2s_duplex_program);
    sm = pio_claim_unused_sm(pio, true);
    i2s_duplex_program_init(pio, sm, offset, pins::kI2sDout, pins::kI2sDin);

    multicore_launch_core1(core1Main);
}

}  // namespace audio
