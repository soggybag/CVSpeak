// Desktop harness: render the DSP core to a WAV file.
//   usage: render [out.wav]
// Fires a trigger every 0.5 s while stepping the pitch up a fifth each time,
// mimicking what a sequencer patched into TRIG and V/OCT would do.

#include <cstdio>
#include <vector>

#include "cvspeak/voice.h"
#include "wav.h"

int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1] : "build/render.wav";
    constexpr int kRate = 48000;
    constexpr int kBlock = 32;
    constexpr float kSeconds = 3.f;
    constexpr int kTrigEvery = kRate / 2;

    cvspeak::Voice voice;
    voice.setSampleRate(kRate);

    std::vector<float> audio(static_cast<size_t>(kSeconds * kRate));
    cvspeak::VoiceInputs in;
    cvspeak::VoiceOutputs outs;
    int words = 0;

    for (size_t pos = 0; pos < audio.size(); pos += kBlock) {
        in.trig = (pos % kTrigEvery) == 0;
        if (in.trig) in.pitch = (words++ % 4) * (7.f / 12.f) - 1.f;
        voice.process(in, &audio[pos], kBlock, outs);
        if (outs.eow) std::printf("EOW at %.3f s\n", static_cast<double>(pos) / kRate);
    }

    if (!cvspeak::tools::writeWav16(path, audio, kRate)) {
        std::fprintf(stderr, "could not write %s\n", path);
        return 1;
    }
    std::printf("wrote %s (%d words)\n", path, words);
    return 0;
}
