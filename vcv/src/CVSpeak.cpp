// VCV Rack wrapper for the CVSpeak DSP core. Keep this thin: map params and
// ports onto cvspeak::VoiceInputs, detect edges and run the core. All sound
// logic belongs in ../dsp so the RP2350 firmware gets the same behavior.

#include "plugin.hpp"
#include "cvspeak/sequencer.h"
#include "cvspeak/voice.h"

// Shows the selected word or phrase name in the Select knob's tooltip.
struct SelectQuantity : ParamQuantity {
    std::string getDisplayValueString() override;
};

struct CVSpeak : Module {
    // Append new ids at the end of each enum: saved patches store them by index.
    enum ParamId {
        PITCH_PARAM,
        SPEAK_PARAM,
        SELECT_PARAM,  // word (Word mode) or phrase (Phrase mode), 0..1
        SPEED_PARAM,
        INTONATION_PARAM,
        CHIRP_PARAM,
        MODE_PARAM,
        PARAMS_LEN
    };
    enum InputId { TRIG_INPUT, RESET_INPUT, VOCT_INPUT, SELECT_INPUT, INPUTS_LEN };
    enum OutputId { AUDIO_OUTPUT, EOW_OUTPUT, OUTPUTS_LEN };
    enum LightId { SPEAKING_LIGHT, LIGHTS_LEN };

    cvspeak::Voice voice;
    dsp::SchmittTrigger trigIn, resetIn, speakButton;
    dsp::PulseGenerator eowPulse;

    // Written by the audio thread, read by the display.
    cvspeak::Mode mode = cvspeak::Mode::Word;
    int selected = 0;

    CVSpeak() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
        configParam(PITCH_PARAM, -2.f, 2.f, -1.f, "Pitch", " Hz", 2.f, dsp::FREQ_C4);
        configButton(SPEAK_PARAM, "Speak (manual trigger)");
        configParam<SelectQuantity>(SELECT_PARAM, 0.f, 1.f, 0.f, "Select");
        configParam(SPEED_PARAM, -2.f, 2.f, 0.f, "Speed", "x", 2.f);
        configParam(INTONATION_PARAM, 0.f, 1.f, 1.f, "Intonation (0 = monotone)", "%", 0.f, 100.f);
        configParam(CHIRP_PARAM, 0.f, 1.f, 0.5f, "Chirp (excitation character)", "%", 0.f, 100.f);
        configSwitch(MODE_PARAM, 0.f, 1.f, 0.f, "Mode", {"Word", "Phrase"});
        configInput(TRIG_INPUT, "Trigger (speak / next word)");
        configInput(RESET_INPUT, "Reset (back to the phrase's first word)");
        configInput(VOCT_INPUT, "Pitch (1V/oct)");
        configInput(SELECT_INPUT, "Select CV (one word or phrase per semitone)");
        configOutput(AUDIO_OUTPUT, "Audio");
        configOutput(EOW_OUTPUT, "End of word trigger");
        onSampleRateChange();
    }

    void onSampleRateChange() override {
        voice.setSampleRate(APP->engine->getSampleRate());
    }

    int itemCount(cvspeak::Mode m) const {
        return m == cvspeak::Mode::Phrase ? cvspeak::kNumPhrases : cvspeak::kNumWords;
    }

    const char* itemName(cvspeak::Mode m, int index) const {
        if (m == cvspeak::Mode::Phrase) return cvspeak::kPhrases[index].text;
        return cvspeak::kWords[index].name;
    }

    void process(const ProcessArgs& args) override {
        mode = params[MODE_PARAM].getValue() > 0.5f ? cvspeak::Mode::Phrase : cvspeak::Mode::Word;
        selected = cvspeak::selectIndex(params[SELECT_PARAM].getValue(), inputs[SELECT_INPUT].getVoltage(),
                                        itemCount(mode));

        cvspeak::VoiceInputs in;
        // Rack's convention: rising edge above ~1 V, re-armed below ~0.1 V.
        // The button ORs with the jack, so it can kick-start an EOW -> TRIG loop.
        const bool jackTrig = trigIn.process(inputs[TRIG_INPUT].getVoltage(), 0.1f, 1.f);
        const bool buttonTrig = speakButton.process(params[SPEAK_PARAM].getValue());
        in.trig = jackTrig || buttonTrig;
        in.reset = resetIn.process(inputs[RESET_INPUT].getVoltage(), 0.1f, 1.f);
        in.mode = mode;
        in.word = selected;
        in.phrase = selected;
        in.pitch = params[PITCH_PARAM].getValue() + inputs[VOCT_INPUT].getVoltage();
        in.speed = std::exp2(params[SPEED_PARAM].getValue());
        in.intonation = params[INTONATION_PARAM].getValue();
        in.chirp = params[CHIRP_PARAM].getValue();

        float sample;
        cvspeak::VoiceOutputs outs;
        voice.process(in, &sample, 1, outs);
        if (outs.eow) eowPulse.trigger(1e-3f);

        outputs[AUDIO_OUTPUT].setVoltage(5.f * sample);
        outputs[EOW_OUTPUT].setVoltage(eowPulse.process(args.sampleTime) ? 10.f : 0.f);
        lights[SPEAKING_LIGHT].setBrightness(voice.speaking() ? 1.f : 0.f);
    }
};

std::string SelectQuantity::getDisplayValueString() {
    auto* m = dynamic_cast<CVSpeak*>(module);
    if (!m) return ParamQuantity::getDisplayValueString();
    const int i = cvspeak::selectIndex(getValue(), 0.f, m->itemCount(m->mode));
    return string::f("%d: %s", i + 1, m->itemName(m->mode, i));
}

// Stand-in for the hardware's 128x64 OLED. Later this will blit a framebuffer
// rendered by the shared UI code in ../dsp, so menus look identical on both.
struct OledDisplay : LedDisplay {
    CVSpeak* module = nullptr;

    const NVGcolor fg = nvgRGB(0x9f, 0xe7, 0xff);
    const NVGcolor bg = nvgRGB(0x0b, 0x17, 0x1c);

    void drawWordMode(NVGcontext* vg) {
        const int selected = module ? module->selected : 0;
        nvgFontSize(vg, 11.f);
        nvgText(vg, 6.f, 5.f, string::f("WORD  %02d/%02d", selected + 1, cvspeak::kNumWords).c_str(), nullptr);
        nvgFontSize(vg, 20.f);
        nvgText(vg, 6.f, 20.f, cvspeak::kWords[selected].name, nullptr);
        const int current = module ? module->voice.currentWord() : -1;
        if (module && module->voice.speaking() && current >= 0) {
            nvgFontSize(vg, 11.f);
            nvgText(vg, 6.f, 60.f, string::f("> %s", cvspeak::kWords[current].name).c_str(), nullptr);
        }
    }

    // The selected phrase, word-wrapped. The word being spoken is inverted and
    // the word the next TRIG will speak is underlined.
    void drawPhraseMode(NVGcontext* vg) {
        const int selected = module ? module->selected : 0;
        const cvspeak::Phrase& phrase = cvspeak::kPhrases[selected];
        int spoken = -1, next = 0;
        if (module && module->voice.sequencer().phrase() == selected) {
            spoken = module->voice.speaking() ? module->voice.sequencer().spokenPosition() : -1;
            next = module->voice.sequencer().nextPosition();
        }

        nvgFontSize(vg, 11.f);
        nvgText(vg, 6.f, 5.f,
                string::f("PHRASE %02d/%02d  %d/%d", selected + 1, cvspeak::kNumPhrases,
                          (spoken >= 0 ? spoken : next) + 1, phrase.length)
                    .c_str(),
                nullptr);

        // Lay out words into lines.
        const float left = 6.f, right = box.size.x - 6.f, top = 19.f, lineH = 12.f;
        const float space = 5.5f;
        struct Placed {
            float x;
            int line;
            float w;
        };
        std::vector<Placed> placed;
        float x = left;
        int line = 0;
        for (int i = 0; i < phrase.length; ++i) {
            const char* word = cvspeak::kWords[phrase.words[i]].name;
            float bounds[4];
            nvgTextBounds(vg, 0.f, 0.f, word, nullptr, bounds);
            const float w = bounds[2] - bounds[0];
            if (x > left && x + w > right) {
                x = left;
                ++line;
            }
            placed.push_back({x, line, w});
            x += w + space;
        }

        // Scroll so the active word's line stays visible (5 lines fit).
        const int focus = spoken >= 0 ? spoken : next;
        const int firstLine = std::max(0, placed[focus].line - 3);

        for (int i = 0; i < phrase.length; ++i) {
            const int l = placed[i].line - firstLine;
            if (l < 0 || l > 4) continue;
            const float px = placed[i].x, py = top + lineH * static_cast<float>(l);
            const char* word = cvspeak::kWords[phrase.words[i]].name;
            if (i == spoken) {
                nvgBeginPath(vg);
                nvgRect(vg, px - 1.5f, py - 1.f, placed[i].w + 3.f, lineH - 0.5f);
                nvgFillColor(vg, fg);
                nvgFill(vg);
                nvgFillColor(vg, bg);
                nvgText(vg, px, py, word, nullptr);
                nvgFillColor(vg, fg);
            } else {
                nvgText(vg, px, py, word, nullptr);
            }
            if (i == next && i != spoken) {
                nvgBeginPath(vg);
                nvgRect(vg, px, py + lineH - 2.5f, placed[i].w, 1.f);
                nvgFill(vg);
            }
        }
    }

    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer == 1) {
            std::shared_ptr<window::Font> font =
                APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
            if (font) {
                nvgFontFaceId(args.vg, font->handle);
                nvgFillColor(args.vg, fg);
                nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
                if (module && module->mode == cvspeak::Mode::Phrase && cvspeak::kNumPhrases > 0) {
                    drawPhraseMode(args.vg);
                } else {
                    drawWordMode(args.vg);
                }
            }
        }
        LedDisplay::drawLayer(args, layer);
    }
};

// Temporary text labels until the Phase 3 panel artwork (Rack's SVG renderer
// ignores <text>, so final labels will be paths in the SVG).
struct PanelLabel : Widget {
    std::string text;
    void draw(const DrawArgs& args) override {
        std::shared_ptr<window::Font> font =
            APP->window->loadFont(asset::system("res/fonts/DejaVuSans.ttf"));
        if (!font) return;
        nvgFontFaceId(args.vg, font->handle);
        nvgFontSize(args.vg, 8.f);
        nvgFillColor(args.vg, nvgRGB(0x33, 0x33, 0x33));
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_BOTTOM);
        nvgText(args.vg, 0.f, 0.f, text.c_str(), nullptr);
    }
};

// Layout in mm on a 14 HP (71.12 mm) panel. Placeholder until the Phase 3 panel.
struct CVSpeakWidget : ModuleWidget {
    void label(float xMm, float yMm, const char* text) {
        auto* l = createWidget<PanelLabel>(mm2px(Vec(xMm, yMm)));
        l->text = text;
        addChild(l);
    }

    explicit CVSpeakWidget(CVSpeak* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/CVSpeak.svg")));

        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild(createWidget<ScrewSilver>(
            Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        auto* display = createWidget<OledDisplay>(mm2px(Vec(8.06f, 12.f)));
        display->box.size = mm2px(Vec(55.f, 27.5f));  // 2:1, like a 128x64 OLED
        display->module = module;
        addChild(display);
        addChild(createLightCentered<SmallLight<GreenLight>>(
            mm2px(Vec(60.5f, 36.5f)), module, CVSpeak::SPEAKING_LIGHT));

        // Knobs and switch
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(14.f, 54.f)), module, CVSpeak::SELECT_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(35.56f, 54.f)), module, CVSpeak::PITCH_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(57.12f, 54.f)), module, CVSpeak::SPEED_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(14.f, 76.f)), module, CVSpeak::INTONATION_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(35.56f, 76.f)), module, CVSpeak::CHIRP_PARAM));
        addParam(createParamCentered<CKSS>(mm2px(Vec(57.12f, 76.f)), module, CVSpeak::MODE_PARAM));
        label(14.f, 47.f, "SELECT");
        label(35.56f, 47.f, "PITCH");
        label(57.12f, 47.f, "SPEED");
        label(14.f, 69.f, "INTONE");
        label(35.56f, 69.f, "CHIRP");
        label(57.12f, 69.f, "MODE");

        // Jacks
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(12.f, 100.f)), module, CVSpeak::TRIG_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(27.7f, 100.f)), module, CVSpeak::RESET_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(43.4f, 100.f)), module, CVSpeak::VOCT_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(59.1f, 100.f)), module, CVSpeak::SELECT_INPUT));
        addParam(createParamCentered<VCVButton>(mm2px(Vec(12.f, 114.f)), module, CVSpeak::SPEAK_PARAM));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(43.4f, 114.f)), module, CVSpeak::EOW_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(59.1f, 114.f)), module, CVSpeak::AUDIO_OUTPUT));
        label(12.f, 95.f, "TRIG");
        label(27.7f, 95.f, "RESET");
        label(43.4f, 95.f, "V/OCT");
        label(59.1f, 95.f, "SEL CV");
        label(12.f, 109.5f, "SPEAK");
        label(43.4f, 109.f, "EOW");
        label(59.1f, 109.f, "OUT");
    }
};

Model* modelCVSpeak = createModel<CVSpeak, CVSpeakWidget>("CVSpeak");
