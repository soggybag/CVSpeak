// Unit tests for the DSP core. Tiny self-contained runner, no dependencies.
//   make test

#include <cmath>
#include <cstdio>
#include <vector>

#include "cvspeak/voice.h"

static int g_failures = 0;
#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
            ++g_failures;                                                  \
        }                                                                  \
    } while (0)

constexpr int kRate = 48000;

// Renders `samples` samples one at a time; returns the number of EOW events.
static int run(cvspeak::Voice& v, cvspeak::VoiceInputs in, int samples,
               std::vector<float>* capture = nullptr) {
    cvspeak::VoiceOutputs outs;
    int eows = 0;
    for (int i = 0; i < samples; ++i) {
        float s;
        v.process(in, &s, 1, outs);
        in.trig = in.reset = false;  // events last one block only
        eows += outs.eow;
        if (capture) capture->push_back(s);
    }
    return eows;
}

// Counts sawtooth wraps (large downward jumps) as a frequency proxy.
static int countCycles(const std::vector<float>& s) {
    int n = 0;
    for (size_t i = 1; i < s.size(); ++i)
        if (s[i - 1] - s[i] > 0.1f) ++n;
    return n;
}

static void testSilentUntilTriggered() {
    cvspeak::Voice v;
    v.setSampleRate(kRate);
    std::vector<float> s;
    CHECK(run(v, {}, 1000, &s) == 0);
    for (float x : s) CHECK(x == 0.f);
    CHECK(!v.speaking());
}

static void testTriggerSpeaksAndEowFiresOnce() {
    cvspeak::Voice v;
    v.setSampleRate(kRate);
    cvspeak::VoiceInputs in;
    in.trig = true;
    CHECK(run(v, in, 100) == 0);
    CHECK(v.speaking());
    CHECK(run(v, {}, kRate) == 1);
    CHECK(!v.speaking());
}

static void testResetSilences() {
    cvspeak::Voice v;
    v.setSampleRate(kRate);
    cvspeak::VoiceInputs in;
    in.trig = true;
    run(v, in, 100);
    in.trig = false;
    in.reset = true;
    run(v, in, 1);
    CHECK(!v.speaking());
}

static void testPitchIsOneVoltPerOctave() {
    auto cyclesAt = [](float volts) {
        cvspeak::Voice v;
        v.setSampleRate(kRate);
        cvspeak::VoiceInputs in;
        in.pitch = volts;
        in.trig = true;
        std::vector<float> s;
        run(v, in, kRate / 10, &s);
        return countCycles(s);
    };
    const int c0 = cyclesAt(0.f);
    const int c1 = cyclesAt(1.f);
    CHECK(std::abs(c0 - 26) <= 1);       // C4 ≈ 261.6 Hz over 0.1 s
    CHECK(std::abs(c1 - 2 * c0) <= 1);   // +1 V = one octave up
}

// EOW patched back into TRIG (one-sample cable delay, as in Rack): after one
// kick-start trigger the voice must keep speaking word after word.
static void testEowToTrigLoopSustains() {
    cvspeak::Voice v;
    v.setSampleRate(kRate);
    cvspeak::VoiceInputs in;
    cvspeak::VoiceOutputs outs;
    in.trig = true;  // the SPEAK button press
    int eows = 0, silentSamples = 0;
    for (int i = 0; i < 2 * kRate; ++i) {
        float s;
        v.process(in, &s, 1, outs);
        in.trig = outs.eow;  // the patch cable
        eows += outs.eow;
        if (!v.speaking()) ++silentSamples;
    }
    CHECK(eows == 5);              // 2 s / 0.4 s per placeholder word
    CHECK(silentSamples <= eows);  // at most one gap sample per word boundary
}

int main() {
    struct { const char* name; void (*fn)(); } tests[] = {
        {"silent until triggered", testSilentUntilTriggered},
        {"trigger speaks, EOW fires once", testTriggerSpeaksAndEowFiresOnce},
        {"reset silences", testResetSilences},
        {"pitch is 1V/oct", testPitchIsOneVoltPerOctave},
        {"EOW -> TRIG loop sustains", testEowToTrigLoopSustains},
    };
    for (auto& t : tests) {
        const int before = g_failures;
        t.fn();
        std::printf("%s %s\n", g_failures == before ? "ok  " : "FAIL", t.name);
    }
    std::printf("%s\n", g_failures ? "TESTS FAILED" : "all tests passed");
    return g_failures ? 1 : 0;
}
