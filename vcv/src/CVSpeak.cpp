// VCV Rack wrapper for the CVSpeak DSP core. Keep this thin: map params and
// ports onto cvspeak::VoiceInputs, detect edges and run the core. All sound
// logic belongs in ../dsp so the RP2350 firmware gets the same behavior.

#include "plugin.hpp"
#include "cvspeak/voice.h"

struct CVSpeak : Module {
    enum ParamId { PITCH_PARAM, PARAMS_LEN };
    enum InputId { TRIG_INPUT, RESET_INPUT, VOCT_INPUT, INPUTS_LEN };
    enum OutputId { AUDIO_OUTPUT, EOW_OUTPUT, OUTPUTS_LEN };
    enum LightId { SPEAKING_LIGHT, LIGHTS_LEN };

    cvspeak::Voice voice;
    dsp::SchmittTrigger trigIn, resetIn;
    dsp::PulseGenerator eowPulse;

    CVSpeak() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
        configParam(PITCH_PARAM, -2.f, 2.f, 0.f, "Pitch", " Hz", 2.f, dsp::FREQ_C4);
        configInput(TRIG_INPUT, "Trigger (speak next word)");
        configInput(RESET_INPUT, "Reset (back to first word)");
        configInput(VOCT_INPUT, "Pitch (1V/oct)");
        configOutput(AUDIO_OUTPUT, "Audio");
        configOutput(EOW_OUTPUT, "End of word trigger");
        onSampleRateChange();
    }

    void onSampleRateChange() override {
        voice.setSampleRate(APP->engine->getSampleRate());
    }

    void process(const ProcessArgs& args) override {
        cvspeak::VoiceInputs in;
        // Rack's convention: rising edge above ~1 V, re-armed below ~0.1 V.
        in.trig = trigIn.process(inputs[TRIG_INPUT].getVoltage(), 0.1f, 1.f);
        in.reset = resetIn.process(inputs[RESET_INPUT].getVoltage(), 0.1f, 1.f);
        in.pitch = params[PITCH_PARAM].getValue() + inputs[VOCT_INPUT].getVoltage();

        float sample;
        cvspeak::VoiceOutputs outs;
        voice.process(in, &sample, 1, outs);
        if (outs.eow) eowPulse.trigger(1e-3f);

        outputs[AUDIO_OUTPUT].setVoltage(5.f * sample);
        outputs[EOW_OUTPUT].setVoltage(eowPulse.process(args.sampleTime) ? 10.f : 0.f);
        lights[SPEAKING_LIGHT].setBrightness(voice.speaking() ? 1.f : 0.f);
    }
};

// Stand-in for the hardware's 128x64 OLED. Later this will blit a framebuffer
// rendered by the shared UI code in ../dsp, so menus look identical on both.
struct OledDisplay : LedDisplay {
    CVSpeak* module = nullptr;

    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer == 1) {
            std::shared_ptr<window::Font> font =
                APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
            if (font) {
                nvgFontFaceId(args.vg, font->handle);
                nvgFontSize(args.vg, 13.f);
                nvgFillColor(args.vg, nvgRGB(0x9f, 0xe7, 0xff));
                nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
                nvgText(args.vg, 6.f, 6.f, "CVSPEAK", nullptr);
                nvgText(args.vg, 6.f, 24.f, "MODE  WORD", nullptr);
                const bool speaking = module && module->voice.speaking();
                nvgText(args.vg, 6.f, 42.f, speaking ? "> SPEAKING" : "  idle", nullptr);
            }
        }
        LedDisplay::drawLayer(args, layer);
    }
};

// Layout in mm on a 14 HP (71.12 mm) panel. Placeholder until the Phase 3 panel.
struct CVSpeakWidget : ModuleWidget {
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

        addChild(createLightCentered<MediumLight<GreenLight>>(
            mm2px(Vec(60.f, 46.f)), module, CVSpeak::SPEAKING_LIGHT));
        addParam(createParamCentered<RoundBigBlackKnob>(
            mm2px(Vec(35.56f, 60.f)), module, CVSpeak::PITCH_PARAM));

        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(12.f, 100.f)), module, CVSpeak::TRIG_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(27.7f, 100.f)), module, CVSpeak::RESET_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(43.4f, 100.f)), module, CVSpeak::VOCT_INPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(43.4f, 114.f)), module, CVSpeak::EOW_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(59.1f, 114.f)), module, CVSpeak::AUDIO_OUTPUT));
    }
};

Model* modelCVSpeak = createModel<CVSpeak, CVSpeakWidget>("CVSpeak");
