// Desktop harness: render the DSP core to a WAV file.
//   usage: render [out.wav]
// Speaks every word in the bank, each one triggered 100 ms after the previous
// word's EOW. Then plays "ZERO".."NINE" monotone on a C minor arpeggio, at
// double speed, to exercise V/Oct, Speed and Intonation.

#include <cstdio>
#include <vector>

#include "cvspeak/voice.h"
#include "wav.h"

namespace {

constexpr int kRate = 48000;
constexpr int kBlock = 32;

struct Renderer {
    cvspeak::Voice voice;
    std::vector<float> audio;

    Renderer() { voice.setSampleRate(kRate); }

    // Speaks one word and returns once it has finished, plus `gapSeconds` of silence.
    void say(int word, float pitch, float speed, float intonation, float gapSeconds) {
        cvspeak::VoiceInputs in;
        in.word = word;
        in.pitch = pitch;
        in.speed = speed;
        in.intonation = intonation;
        in.trig = true;
        cvspeak::VoiceOutputs outs;
        float block[kBlock];
        bool done = false;
        while (!done) {
            voice.process(in, block, kBlock, outs);
            in.trig = false;
            audio.insert(audio.end(), block, block + kBlock);
            done = outs.eow || !voice.speaking();
        }
        const int gap = static_cast<int>(gapSeconds * kRate) / kBlock;
        for (int i = 0; i < gap; ++i) {
            voice.process(in, block, kBlock, outs);
            audio.insert(audio.end(), block, block + kBlock);
        }
    }
};

}  // namespace

int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1] : "build/render.wav";
    Renderer r;

    for (int w = 0; w < cvspeak::kNumWords; ++w) {
        std::printf("%s ", cvspeak::kWords[w].name);
        r.say(w, -1.f, 1.f, 1.f, 0.1f);
    }
    std::printf("\n");

    // C minor arpeggio, monotone and fast: the "robot singing" sound.
    const float arp[] = {0.f, 3.f, 7.f, 12.f, 7.f, 3.f, 0.f, -5.f, 0.f, 12.f};
    for (int i = 0; i < 10; ++i) r.say(i, -1.f + arp[i] / 12.f, 1.6f, 0.f, 0.02f);

    if (!cvspeak::tools::writeWav16(path, r.audio, kRate)) {
        std::fprintf(stderr, "could not write %s\n", path);
        return 1;
    }
    std::printf("wrote %s (%.1f s)\n", path, static_cast<double>(r.audio.size()) / kRate);
    return 0;
}
