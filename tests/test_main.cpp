// Unit tests for the DSP core. Tiny self-contained runner, no dependencies.
//   make test

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "cvspeak/lpc_analysis.h"
#include "cvspeak/lpc_format.h"
#include "cvspeak/lpc_synth.h"
#include "cvspeak/sequencer.h"
#include "cvspeak/voice.h"

using namespace cvspeak;

static int g_failures = 0;
#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
            ++g_failures;                                                  \
        }                                                                  \
    } while (0)

constexpr int kRate = 48000;

static int findWord(const char* name) {
    for (int i = 0; i < kNumWords; ++i)
        if (std::strcmp(kWords[i].name, name) == 0) return i;
    return -1;
}

// Speaks `word` once and returns the host-rate audio until EOW (or a 5 s cap).
static std::vector<float> speak(int word, float pitch = -1.f, float speed = 1.f, float intonation = 1.f,
                                int* eowCount = nullptr) {
    Voice v;
    v.setSampleRate(kRate);
    VoiceInputs in;
    in.word = word;
    in.pitch = pitch;
    in.speed = speed;
    in.intonation = intonation;
    in.trig = true;
    VoiceOutputs outs;
    std::vector<float> audio;
    int eows = 0;
    for (int i = 0; i < 5 * kRate; ++i) {
        float s;
        v.process(in, &s, 1, outs);
        in.trig = false;
        audio.push_back(s);
        if (outs.eow) {
            ++eows;
            break;
        }
    }
    if (eowCount) *eowCount = eows;
    return audio;
}

// ---- format ---------------------------------------------------------------

static void testQuantizersRoundTrip() {
    for (int c = 0; c < lpc::kOrder; ++c) {
        const int levels = 1 << lpc::kKBits[c];
        for (int code = 0; code < levels; ++code) CHECK(lpc::quantizeK(c, lpc::kValue(c, code)) == code);
        for (int code = 1; code < levels; ++code) CHECK(lpc::kValue(c, code) > lpc::kValue(c, code - 1));
        CHECK(std::fabs(lpc::kValue(c, 0)) < 1.f);
    }
    for (int code = 1; code < lpc::kEnergyStop; ++code)
        CHECK(lpc::quantizeEnergy(lpc::energyAmplitude(code)) == code);
    CHECK(lpc::quantizeEnergy(0.f) == lpc::kEnergySilent);
    CHECK(lpc::quantizeEnergy(1e-6f) == lpc::kEnergySilent);
    for (int code = 1; code < 64; ++code) CHECK(lpc::quantizePitch(lpc::pitchPeriod(code)) == code);
    CHECK(lpc::quantizePitch(0.f) == 0);
}

static void testBitstreamRoundTrip() {
    std::vector<lpc::FrameCodes> frames;
    uint32_t seed = 1;
    auto rnd = [&seed](int n) {
        seed = seed * 1664525u + 1013904223u;
        return static_cast<int>((seed >> 16) % static_cast<uint32_t>(n));
    };
    for (int i = 0; i < 200; ++i) {
        lpc::FrameCodes f;
        f.energy = static_cast<uint8_t>(rnd(15));  // 0..14, never stop
        f.repeat = rnd(4) == 0;
        f.pitch = static_cast<uint8_t>(rnd(3) == 0 ? 0 : rnd(64));
        for (int c = 0; c < lpc::kOrder; ++c) f.k[c] = static_cast<uint8_t>(rnd(1 << lpc::kKBits[c]));
        frames.push_back(f);
    }
    uint8_t buf[4096];
    lpc::BitWriter w(buf, sizeof buf);
    for (const auto& f : frames) lpc::writeFrame(w, f);
    CHECK(!w.overflow());

    lpc::BitReader r(buf, w.bytes());
    for (const auto& want : frames) {
        lpc::FrameCodes got;
        CHECK(lpc::readFrame(r, got));
        CHECK(got.energy == want.energy);
        if (want.energy == lpc::kEnergySilent) continue;
        CHECK(got.repeat == want.repeat);
        CHECK(got.pitch == want.pitch);
        if (want.repeat) continue;
        const int n = want.pitch ? lpc::kOrder : lpc::kUnvoicedOrder;
        for (int c = 0; c < n; ++c) CHECK(got.k[c] == want.k[c]);
    }
}

// ---- analysis + lattice ----------------------------------------------------

// Synthesizes noise through the lattice with known coefficients, then checks
// Levinson recovers them. This pins down the encoder/decoder sign convention.
static void testLevinsonInvertsLattice() {
    const float k[lpc::kOrder] = {-0.8f, 0.5f, -0.3f, 0.2f, -0.1f, 0.1f, 0.05f, -0.05f, 0.f, 0.02f};
    LatticeFilter lattice;
    std::vector<float> x(40000);
    uint32_t s = 7;
    for (float& v : x) {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        v = lattice.process((s & 1u) ? 1.f : -1.f, k);
    }
    float r[lpc::kOrder + 1], est[lpc::kOrder];
    lpc::autocorrelate(x.data(), static_cast<int>(x.size()), r, lpc::kOrder);
    lpc::levinson(r, lpc::kOrder, est);
    for (int i = 0; i < lpc::kOrder; ++i) CHECK(std::fabs(est[i] - k[i]) < 0.03f);
}

static void testPitchDetector() {
    std::vector<float> x(400);
    for (int i = 0; i < 400; ++i) {
        const float ph = 6.2831853f * static_cast<float>(i) / 61.5f;  // 130 Hz at 8 kHz
        x[i] = std::sin(ph) + 0.5f * std::sin(2.f * ph) + 0.3f * std::sin(3.f * ph);
    }
    float corr = 0.f;
    const float p = lpc::detectPitch(x.data(), 400, 16, 160, 0.5f, &corr);
    CHECK(std::fabs(p - 61.5f) < 1.f);
    CHECK(corr > 0.9f);
}

// ---- word bank -------------------------------------------------------------

static void testEveryWordDecodesCleanly() {
    CHECK(kNumWords >= 20);
    for (int w = 0; w < kNumWords; ++w) {
        int eows = 0;
        const std::vector<float> a = speak(w, -1.f, 1.f, 1.f, &eows);
        CHECK(eows == 1);
        // Duration ≈ frames × 25 ms (the final frame fades into the stop).
        const float seconds = static_cast<float>(a.size()) / kRate;
        const float expected = static_cast<float>(kWords[w].frames) * 0.025f;
        CHECK(std::fabs(seconds - expected) < 0.03f);
        float peak = 0.f, energy = 0.f;
        bool finite = true;
        for (float s : a) {
            finite = finite && std::isfinite(s);
            peak = std::fmax(peak, std::fabs(s));
            energy += s * s;
        }
        CHECK(finite);
        CHECK(peak < 2.f);     // no filter blow-ups
        CHECK(energy > 1.f);   // actually audible
        if (!(peak < 2.f) || !(energy > 1.f)) std::printf("  word %s: peak %.2f\n", kWords[w].name,
                                                          static_cast<double>(peak));
    }
}

static void testSpeedChangesDurationNotPitch() {
    const int w = findWord("SYNTHESIZER");
    CHECK(w >= 0);
    const size_t normal = speak(w, -1.f, 1.f).size();
    const size_t fast = speak(w, -1.f, 2.f).size();
    const size_t slow = speak(w, -1.f, 0.5f).size();
    CHECK(std::fabs(static_cast<float>(fast) / static_cast<float>(normal) - 0.5f) < 0.05f);
    CHECK(std::fabs(static_cast<float>(slow) / static_cast<float>(normal) - 2.f) < 0.1f);
    // Pitch changes must not change duration.
    CHECK(speak(w, 1.f, 1.f).size() == normal);
}

// Monotone (intonation 0): the voiced pitch must follow V/Oct exactly.
static void testMonotonePitchFollowsVoct() {
    const int w = findWord("ZERO");
    CHECK(w >= 0);
    auto measuredHz = [w](float volts) {
        const std::vector<float> a = speak(w, volts, 1.f, 0.f);
        // Analyze 40 ms from the middle of the word (well inside the vowel).
        const int len = 1920;
        const int start = static_cast<int>(a.size()) / 2 - len / 2;
        float corr = 0.f;
        const float period = lpc::detectPitch(a.data() + start, len, 48, 960, 0.3f, &corr);
        return period > 0.f ? static_cast<float>(kRate) / period : 0.f;
    };
    const float c3 = measuredHz(-1.f);
    const float c4 = measuredHz(0.f);
    CHECK(std::fabs(c3 / 130.81f - 1.f) < 0.03f);
    CHECK(std::fabs(c4 / 261.63f - 1.f) < 0.03f);
    if (std::fabs(c3 / 130.81f - 1.f) >= 0.03f || std::fabs(c4 / 261.63f - 1.f) >= 0.03f)
        std::printf("  measured %.1f Hz and %.1f Hz\n", static_cast<double>(c3), static_cast<double>(c4));
}

// The excitation is normalized, so the Chirp amount changes tone, not level.
static void testChirpKeepsLoudness() {
    auto rmsDb = [](float chirp) {
        Voice v;
        v.setSampleRate(kRate);
        VoiceInputs in;
        in.word = findWord("ZERO");
        in.chirp = chirp;
        in.trig = true;
        VoiceOutputs outs;
        float sum = 0.f;
        int n = 0;
        do {
            float s;
            v.process(in, &s, 1, outs);
            in.trig = false;
            sum += s * s;
            ++n;
        } while (!outs.eow && n < 5 * kRate);
        return 10.f * std::log10(sum / static_cast<float>(n));
    };
    CHECK(std::fabs(rmsDb(0.f) - rmsDb(1.f)) < 4.f);
    CHECK(std::fabs(rmsDb(0.f) - rmsDb(0.5f)) < 2.5f);
}

// ---- voice behavior --------------------------------------------------------

static void testSilentUntilTriggered() {
    Voice v;
    v.setSampleRate(kRate);
    VoiceInputs in;
    VoiceOutputs outs;
    for (int i = 0; i < 1000; ++i) {
        float s;
        v.process(in, &s, 1, outs);
        CHECK(s == 0.f);
        CHECK(!outs.eow);
    }
    CHECK(!v.speaking());
    CHECK(v.currentWord() == -1);
}

// RESET only rewinds the phrase; it must not cut off the word being spoken.
static void testResetDoesNotCutWord() {
    Voice v;
    v.setSampleRate(kRate);
    VoiceInputs in;
    VoiceOutputs outs;
    float s;
    in.trig = true;
    v.process(in, &s, 1, outs);
    CHECK(v.speaking());
    in.trig = false;
    in.reset = true;
    v.process(in, &s, 1, outs);
    CHECK(v.speaking());
}

// ---- selection + sequencer -------------------------------------------------

static void testSelectIndex() {
    CHECK(selectIndex(0.f, 0.f, 30) == 0);
    CHECK(selectIndex(1.f, 0.f, 30) == 29);       // fully clockwise = last item
    CHECK(selectIndex(0.5f, 0.f, 30) == 15);
    CHECK(selectIndex(0.f, 1.f / 12.f, 30) == 1);  // one semitone = one item
    CHECK(selectIndex(0.f, 1.f, 30) == 12);        // one octave = twelve items
    CHECK(selectIndex(0.f, -1.f / 12.f, 30) == 29);  // wraps below zero
    CHECK(selectIndex(1.f, 1.f / 12.f, 30) == 0);    // wraps past the end
    CHECK(selectIndex(0.f, 0.04f, 30) == 0);         // CV snaps to the nearest semitone
    CHECK(selectIndex(0.f, 0.05f, 30) == 1);
    CHECK(selectIndex(0.7f, 3.f, 1) == 0);
    CHECK(selectIndex(0.5f, 0.f, 0) == 0);
}

static void testPhraseDataIsValid() {
    CHECK(kNumPhrases >= 1);
    for (int p = 0; p < kNumPhrases; ++p) {
        CHECK(kPhrases[p].length > 0);
        for (int i = 0; i < kPhrases[p].length; ++i) CHECK(kPhrases[p].words[i] < kNumWords);
    }
}

static const uint16_t kAbc[] = {5, 6, 7};
static const uint16_t kXy[] = {1, 2};
static const Phrase kTestPhrases[] = {{"A B C", kAbc, 3}, {"X Y", kXy, 2}};

static void testSequencerAdvancesAndWraps() {
    PhraseSequencer seq(kTestPhrases, 2);
    CHECK(seq.phrase() == -1);
    CHECK(seq.next(0) == 5);
    CHECK(seq.spokenPosition() == 0);
    CHECK(seq.nextPosition() == 1);
    CHECK(seq.next(0) == 6);
    CHECK(seq.next(0) == 7);
    CHECK(seq.nextPosition() == 0);
    CHECK(seq.next(0) == 5);  // wrapped
}

static void testSequencerReset() {
    PhraseSequencer seq(kTestPhrases, 2);
    seq.next(0);
    seq.next(0);
    seq.reset();
    CHECK(seq.spokenPosition() == -1);
    CHECK(seq.nextPosition() == 0);
    CHECK(seq.next(0) == 5);
}

static void testSequencerPhraseChangeStartsFromTop() {
    PhraseSequencer seq(kTestPhrases, 2);
    seq.next(0);
    seq.next(0);
    CHECK(seq.next(1) == 1);  // new phrase starts at its first word
    CHECK(seq.next(1) == 2);
    CHECK(seq.next(0) == 5);  // and switching back starts that one over too
    CHECK(seq.next(7) == 1);  // out-of-range phrase clamps to the last
}

// Phrase mode end to end through Voice, including RESET + TRIG in the same
// block (clock and reset on one step): reset applies first, so word 1 speaks.
static void testVoicePhraseMode() {
    Voice v;
    v.setSampleRate(kRate);
    VoiceInputs in;
    in.mode = Mode::Phrase;
    in.phrase = 0;
    VoiceOutputs outs;
    float s;
    const Phrase& p = kPhrases[0];
    for (int i = 0; i < p.length + 1; ++i) {
        in.trig = true;
        v.process(in, &s, 1, outs);
        CHECK(v.currentWord() == p.words[i % p.length]);
    }
    in.trig = true;
    in.reset = true;
    v.process(in, &s, 1, outs);
    CHECK(v.currentWord() == p.words[0]);
    CHECK(v.sequencer().spokenPosition() == 0);
}

// Word mode ignores the sequencer: RESET changes nothing, TRIG repeats the word.
static void testVoiceWordModeIgnoresReset() {
    Voice v;
    v.setSampleRate(kRate);
    VoiceInputs in;
    in.word = 4;
    VoiceOutputs outs;
    float s;
    in.trig = true;
    v.process(in, &s, 1, outs);
    in.reset = true;
    v.process(in, &s, 1, outs);
    CHECK(v.currentWord() == 4);
}

static void testRetriggerSwitchesWord() {
    Voice v;
    v.setSampleRate(kRate);
    VoiceInputs in;
    VoiceOutputs outs;
    float s;
    in.word = 0;
    in.trig = true;
    v.process(in, &s, 1, outs);
    CHECK(v.currentWord() == 0);
    in.word = 3;
    v.process(in, &s, 1, outs);
    CHECK(v.currentWord() == 3);
    CHECK(v.speaking());
    in.word = 999;  // out of range clamps to the last word
    v.process(in, &s, 1, outs);
    CHECK(v.currentWord() == kNumWords - 1);
}

// EOW patched back into TRIG (one-sample cable delay, as in Rack): after one
// kick-start trigger the voice must keep speaking word after word.
static void testEowToTrigLoopSustains() {
    Voice v;
    v.setSampleRate(kRate);
    VoiceInputs in;
    VoiceOutputs outs;
    in.trig = true;  // the SPEAK button press
    int eows = 0, silentSamples = 0;
    for (int i = 0; i < 3 * kRate; ++i) {
        float s;
        v.process(in, &s, 1, outs);
        in.trig = outs.eow;  // the patch cable
        eows += outs.eow;
        if (!v.speaking()) ++silentSamples;
    }
    const float wordSeconds = static_cast<float>(kWords[0].frames) * 0.025f;
    CHECK(eows >= static_cast<int>(3.f / wordSeconds) - 1);
    CHECK(silentSamples <= eows);  // at most one gap sample per word boundary
}

int main() {
    struct {
        const char* name;
        void (*fn)();
    } tests[] = {
        {"quantizers round-trip", testQuantizersRoundTrip},
        {"bitstream round-trip", testBitstreamRoundTrip},
        {"Levinson inverts lattice", testLevinsonInvertsLattice},
        {"pitch detector", testPitchDetector},
        {"every word decodes cleanly", testEveryWordDecodesCleanly},
        {"speed changes duration, not pitch", testSpeedChangesDurationNotPitch},
        {"monotone pitch follows V/Oct", testMonotonePitchFollowsVoct},
        {"chirp keeps loudness", testChirpKeepsLoudness},
        {"silent until triggered", testSilentUntilTriggered},
        {"reset does not cut word", testResetDoesNotCutWord},
        {"select index (knob + semitone CV)", testSelectIndex},
        {"phrase data is valid", testPhraseDataIsValid},
        {"sequencer advances and wraps", testSequencerAdvancesAndWraps},
        {"sequencer reset", testSequencerReset},
        {"sequencer phrase change starts from top", testSequencerPhraseChangeStartsFromTop},
        {"voice phrase mode (+ reset with trig)", testVoicePhraseMode},
        {"voice word mode ignores reset", testVoiceWordModeIgnoresReset},
        {"retrigger switches word", testRetriggerSwitchesWord},
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
