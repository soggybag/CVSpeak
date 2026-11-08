#include "controls.h"

#include <cstdint>
#include <initializer_list>

#include "hardware/adc.h"
#include "hardware/gpio.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"
#include "pins.h"

namespace controls {

namespace {

// ADC: 12 bits over ADC_VREF (3V3A).
constexpr float kVref = 3.3f;
constexpr float kAdcScale = kVref / 4096.f;

// Nominal input stages (hardware/docs/Pin map.md); replace with calibration.
// CV:    ADC volts = 1.65 - 0.165 * jack volts   (+/-10 V)
// V/OCT: ADC volts = 1.65 - 0.33  * jack volts   (+/-5 V)
constexpr float kCvBias = 1.65f, kCvGain = 0.165f, kVoctGain = 0.33f;

constexpr float kButtonThreshold = 1.0f;   // mux buttons: below this = pressed
constexpr float kPotSmoothing = 0.1f;      // one-pole, per 1 ms scan
constexpr float kCvSmoothing = 0.5f;
constexpr int kVoctSamples = 16;           // averaged per scan, then smoothed
constexpr int kStepsPerDetent = 4;         // EC11: 4 quadrature steps per click
constexpr int kEncoderDirection = 1;       // flip to -1 if clockwise counts down

volatile int encoderSteps = 0;
uint8_t encoderState = 0;

void onEncoderEdge(uint gpio, uint32_t events) {
    static const int8_t kTable[16] = {0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0};
    const uint8_t ab = static_cast<uint8_t>((gpio_get(pins::kEncA) << 1) | gpio_get(pins::kEncB));
    encoderState = static_cast<uint8_t>(((encoderState << 2) | ab) & 0x0F);
    encoderSteps += kTable[encoderState];
}

#ifdef CVSPEAK_BREADBOARD
constexpr unsigned kBreadboardBack = 15, kBreadboardSpeak = 20, kBreadboardPush = 22;
#else
float readAdc(unsigned input) {
    adc_select_input(input);
    return static_cast<float>(adc_read()) * kAdcScale;
}

float cvVolts(float adcVolts, float gain) { return (kCvBias - adcVolts) / gain; }

void smooth(float& state, float target, float k) { state += (target - state) * k; }
#endif

}  // namespace

void init() {
    for (unsigned p : {pins::kEncA, pins::kEncB}) {
        gpio_init(p);
        gpio_set_dir(p, GPIO_IN);
        gpio_pull_up(p);
    }
    gpio_set_irq_enabled_with_callback(pins::kEncA, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true, onEncoderEdge);
    gpio_set_irq_enabled(pins::kEncB, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true);

#ifdef CVSPEAK_BREADBOARD
    for (unsigned p : {kBreadboardBack, kBreadboardSpeak, kBreadboardPush}) {
        gpio_init(p);
        gpio_set_dir(p, GPIO_IN);
        gpio_pull_up(p);
    }
#else
    for (unsigned p : {pins::kMuxS0, pins::kMuxS1, pins::kMuxS2}) {
        gpio_init(p);
        gpio_set_dir(p, GPIO_OUT);
    }
    adc_init();
    adc_gpio_init(pins::kAdcVoct);  // also disables the digital input (RP2350-E9)
    adc_gpio_init(pins::kAdcMuxA);
    adc_gpio_init(pins::kAdcMuxB);
#endif
}

int takeEncoderDetents() {
    const uint32_t irq = save_and_disable_interrupts();
    const int detents = encoderSteps / kStepsPerDetent;
    encoderSteps -= detents * kStepsPerDetent;
    restore_interrupts(irq);
    return detents * kEncoderDirection;
}

void scan(Readings& r) {
#ifdef CVSPEAK_BREADBOARD
    r.back = !gpio_get(kBreadboardBack);
    r.speak = !gpio_get(kBreadboardSpeak);
    r.encoderPush = !gpio_get(kBreadboardPush);
#else
    float a[8], b[8];
    for (int ch = 0; ch < 8; ++ch) {
        gpio_put(pins::kMuxS0, ch & 1);
        gpio_put(pins::kMuxS1, (ch >> 1) & 1);
        gpio_put(pins::kMuxS2, (ch >> 2) & 1);
        busy_wait_us(5);  // mux and RC settling
        a[ch] = readAdc(pins::kAdcInMuxA);
        b[ch] = readAdc(pins::kAdcInMuxB);
    }
    namespace A = pins::muxA;
    namespace B = pins::muxB;
    smooth(r.pitch, a[A::kPitch] / kVref, kPotSmoothing);
    smooth(r.speed, a[A::kSpeed] / kVref, kPotSmoothing);
    smooth(r.formant, a[A::kFormant] / kVref, kPotSmoothing);
    smooth(r.bend, a[A::kBend] / kVref, kPotSmoothing);
    smooth(r.blend, a[A::kBlend] / kVref, kPotSmoothing);
    smooth(r.intonation, a[A::kIntonation] / kVref, kPotSmoothing);
    r.back = a[A::kBack] < kButtonThreshold;
    r.encoderPush = a[A::kEncPush] < kButtonThreshold;

    smooth(r.selectCv, cvVolts(b[B::kSelectCv], kCvGain), kCvSmoothing);
    smooth(r.speedCv, cvVolts(b[B::kSpeedCv], kCvGain), kCvSmoothing);
    smooth(r.formantCv, cvVolts(b[B::kFormantCv], kCvGain), kCvSmoothing);
    smooth(r.bendCv, cvVolts(b[B::kBendCv], kCvGain), kCvSmoothing);
    r.speak = b[B::kSpeak] < kButtonThreshold;
    // TODO: depends on the Carrier-detect circuit (hardware/docs/Hardware Decisions.md).
    r.carrierPatched = b[B::kCarrierDetect] > 2.5f;

    float sum = 0.f;
    for (int i = 0; i < kVoctSamples; ++i) sum += readAdc(pins::kAdcInVoct);
    smooth(r.voct, cvVolts(sum / kVoctSamples, kVoctGain), kCvSmoothing);
#endif
}

}  // namespace controls
