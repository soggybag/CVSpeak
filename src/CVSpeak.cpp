// VCV Rack wrapper for the CVSpeak DSP core. Keep this thin: map params and
// ports onto cvspeak::VoiceInputs, detect edges and run the core. All sound
// and menu logic belongs in dsp/ so the RP2350 firmware behaves the same.

#include <atomic>
#include <cstring>

#include "plugin.hpp"
#include "cvspeak/framebuffer.h"
#include "cvspeak/sequencer.h"
#include "cvspeak/ui.h"
#include "cvspeak/voice.h"

struct CVSpeak : Module {
    // Append new ids at the end of each enum: saved patches store them by index.
    enum ParamId {
        PITCH_PARAM,
        SPEAK_PARAM,
        SELECT_PARAM,      // legacy (v2.0.x knob); the encoder selects now
        SPEED_PARAM,
        INTONATION_PARAM,  // legacy; now a menu setting
        CHIRP_PARAM,       // legacy; now a menu setting
        MODE_PARAM,        // legacy; now a menu setting
        FORMANT_PARAM,
        BEND_PARAM,
        BLEND_PARAM,
        BACK_PARAM,
        PARAMS_LEN
    };
    enum InputId {
        TRIG_INPUT,
        RESET_INPUT,
        VOCT_INPUT,
        SELECT_INPUT,
        SPEED_INPUT,
        FORMANT_INPUT,
        BEND_INPUT,
        CARRIER_INPUT,
        INPUTS_LEN
    };
    enum OutputId { AUDIO_OUTPUT, EOW_OUTPUT, OUTPUTS_LEN };
    enum LightId { SPEAKING_LIGHT, LIGHTS_LEN };

    cvspeak::Voice voice;
    cvspeak::Ui ui;
    cvspeak::SelectCv selectCv;
    dsp::SchmittTrigger trigIn, resetIn, speakButton, backButton;
    dsp::PulseGenerator eowPulse;

    // Encoder events from the GUI thread, applied in process() so the UI
    // state only changes on the audio thread (as on the hardware's UI core).
    std::atomic<int> encoderTurns{0};
    std::atomic<int> encoderPushes{0};

    // Written by the audio thread, read by the display.
    cvspeak::UiStatus status;

    // View option: draw the display at the real 1.3" OLED size.
    bool hardwareDisplay = false;

    CVSpeak() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
        configParam(PITCH_PARAM, -2.f, 2.f, -1.f, "Pitch", " Hz", 2.f, dsp::FREQ_C4);
        configButton(SPEAK_PARAM, "Speak (manual trigger)");
        configParam(SPEED_PARAM, -2.f, 2.f, 0.f, "Speed", "x", 2.f);
        configParam(FORMANT_PARAM, -1.f, 1.f, 0.f, "Formant (vocal tract size)", "x", 2.f);
        configParam(BEND_PARAM, 0.f, 1.f, 0.f, "Bend (circuit-bent glitches)", "%", 0.f, 100.f);
        configParam(BLEND_PARAM, 0.f, 1.f, 1.f, "Blend (internal buzz to Carrier In)", "%", 0.f, 100.f);
        configButton(BACK_PARAM, "Back (main screen: toggle Word / Phrase mode)");

        // Legacy params: kept so old patches can be migrated (see fromJson).
        // They have no widgets and aren't randomized.
        configParam(SELECT_PARAM, 0.f, 1.f, 0.f, "Select (legacy)");
        configParam(INTONATION_PARAM, 0.f, 1.f, 1.f, "Intonation (legacy)");
        configParam(CHIRP_PARAM, 0.f, 1.f, 0.5f, "Chirp (legacy)");
        configParam(MODE_PARAM, 0.f, 1.f, 0.f, "Mode (legacy)");
        for (int id : {SELECT_PARAM, INTONATION_PARAM, CHIRP_PARAM, MODE_PARAM})
            getParamQuantity(id)->randomizeEnabled = false;

        configInput(TRIG_INPUT, "Trigger (speak / next word)");
        configInput(RESET_INPUT, "Reset (back to the phrase's first word)");
        configInput(VOCT_INPUT, "Pitch (1V/oct)");
        configInput(SELECT_INPUT, "Select CV (one word or phrase per semitone)");
        configInput(SPEED_INPUT, "Speed CV (+/-5 V sweeps the knob range)");
        configInput(FORMANT_INPUT, "Formant CV (+/-5 V sweeps the knob range)");
        configInput(BEND_INPUT, "Bend CV (0-10 V)");
        configInput(CARRIER_INPUT, "Carrier (audio to speak through)");
        configOutput(AUDIO_OUTPUT, "Audio");
        configOutput(EOW_OUTPUT, "End of word trigger");
        onSampleRateChange();
    }

    void onSampleRateChange() override {
        voice.setSampleRate(APP->engine->getSampleRate());
    }

    void onReset(const ResetEvent& e) override {
        Module::onReset(e);
        ui = cvspeak::Ui{};
    }

    void process(const ProcessArgs& args) override {
        if (encoderTurns.load(std::memory_order_relaxed)) ui.turn(encoderTurns.exchange(0));
        if (encoderPushes.load(std::memory_order_relaxed))
            for (int n = encoderPushes.exchange(0); n > 0; --n) ui.push();
        if (backButton.process(params[BACK_PARAM].getValue())) ui.back();

        const cvspeak::Settings& s = ui.settings();
        const bool phrase = s.mode == cvspeak::Mode::Phrase;
        const int count = phrase ? cvspeak::kNumPhrases : cvspeak::kNumWords;
        const int cvOffset =
            inputs[SELECT_INPUT].isConnected() ? selectCv.process(inputs[SELECT_INPUT].getVoltage()) : 0;
        const int selected = cvspeak::wrapIndex((phrase ? s.phrase : s.word) + cvOffset, count);

        cvspeak::VoiceInputs in;
        // Rack's convention: rising edge above ~1 V, re-armed below ~0.1 V.
        // The button ORs with the jack, so it can kick-start an EOW -> TRIG loop.
        const bool jackTrig = trigIn.process(inputs[TRIG_INPUT].getVoltage(), 0.1f, 1.f);
        const bool buttonTrig = speakButton.process(params[SPEAK_PARAM].getValue());
        in.trig = jackTrig || buttonTrig;
        in.reset = resetIn.process(inputs[RESET_INPUT].getVoltage(), 0.1f, 1.f);
        in.mode = s.mode;
        in.word = selected;
        in.phrase = selected;
        in.pitch = params[PITCH_PARAM].getValue() + inputs[VOCT_INPUT].getVoltage();
        // CV: +/-5 V sweeps a bipolar knob's whole range; Bend takes 0-10 V.
        in.speed = std::exp2(params[SPEED_PARAM].getValue() + 0.4f * inputs[SPEED_INPUT].getVoltage());
        in.intonation = s.intonation;
        in.chirp = s.chirp;
        in.formant = params[FORMANT_PARAM].getValue() + 0.2f * inputs[FORMANT_INPUT].getVoltage();
        in.bend = params[BEND_PARAM].getValue() + 0.1f * inputs[BEND_INPUT].getVoltage();
        float carrier = inputs[CARRIER_INPUT].getVoltage() / 5.f;
        in.carrier = inputs[CARRIER_INPUT].isConnected() ? &carrier : nullptr;
        in.blend = params[BLEND_PARAM].getValue();

        float sample;
        cvspeak::VoiceOutputs outs;
        voice.process(in, &sample, 1, outs);
        if (outs.eow) eowPulse.trigger(1e-3f);

        outputs[AUDIO_OUTPUT].setVoltage(5.f * sample);
        outputs[EOW_OUTPUT].setVoltage(eowPulse.process(args.sampleTime) ? 10.f : 0.f);
        lights[SPEAKING_LIGHT].setBrightness(voice.speaking() ? 1.f : 0.f);

        status.selected = selected;
        status.cvOffset = cvOffset;
        status.speaking = voice.speaking();
        status.speakingWord = voice.currentWord();
        status.seqPhrase = voice.sequencer().phrase();
        status.spokenPos = voice.sequencer().spokenPosition();
        status.nextPos = voice.sequencer().nextPosition();
    }

    // Settings are saved by name where possible, so patches survive
    // vocabulary edits that shift the word and phrase indices.
    json_t* dataToJson() override {
        const cvspeak::Settings& s = ui.settings();
        json_t* rootJ = json_object();
        json_object_set_new(rootJ, "mode", json_string(s.mode == cvspeak::Mode::Phrase ? "phrase" : "word"));
        if (cvspeak::kNumWords > 0) json_object_set_new(rootJ, "word", json_string(cvspeak::kWords[s.word].name));
        if (cvspeak::kNumPhrases > 0)
            json_object_set_new(rootJ, "phrase", json_string(cvspeak::kPhrases[s.phrase].text));
        json_object_set_new(rootJ, "wordIndex", json_integer(s.word));
        json_object_set_new(rootJ, "phraseIndex", json_integer(s.phrase));
        json_object_set_new(rootJ, "intonation", json_real(s.intonation));
        json_object_set_new(rootJ, "chirp", json_real(s.chirp));
        json_object_set_new(rootJ, "hardwareDisplay", json_boolean(hardwareDisplay));
        return rootJ;
    }

    void dataFromJson(json_t* rootJ) override {
        cvspeak::Settings& s = ui.settings();
        if (json_t* j = json_object_get(rootJ, "mode"))
            s.mode = std::strcmp(json_string_value(j) ? json_string_value(j) : "", "phrase") == 0
                         ? cvspeak::Mode::Phrase
                         : cvspeak::Mode::Word;
        if (json_t* j = json_object_get(rootJ, "wordIndex")) s.word = json_integer_value(j);
        if (json_t* j = json_object_get(rootJ, "phraseIndex")) s.phrase = json_integer_value(j);
        if (const char* name = json_string_value(json_object_get(rootJ, "word"))) {
            for (int i = 0; i < cvspeak::kNumWords; ++i)
                if (std::strcmp(cvspeak::kWords[i].name, name) == 0) s.word = i;
        }
        if (const char* text = json_string_value(json_object_get(rootJ, "phrase"))) {
            for (int i = 0; i < cvspeak::kNumPhrases; ++i)
                if (std::strcmp(cvspeak::kPhrases[i].text, text) == 0) s.phrase = i;
        }
        if (json_t* j = json_object_get(rootJ, "intonation")) s.intonation = json_number_value(j);
        if (json_t* j = json_object_get(rootJ, "chirp")) s.chirp = json_number_value(j);
        if (json_t* j = json_object_get(rootJ, "hardwareDisplay")) hardwareDisplay = json_boolean_value(j);
        ui.clampSettings();
    }

    // Patches from v2.0.x have no "data": take the settings from the old
    // Select knob, Mode switch and Intonation / Chirp knobs.
    void fromJson(json_t* rootJ) override {
        Module::fromJson(rootJ);
        if (json_object_get(rootJ, "data")) return;
        cvspeak::Settings& s = ui.settings();
        const float knob = params[SELECT_PARAM].getValue();
        s.mode = params[MODE_PARAM].getValue() > 0.5f ? cvspeak::Mode::Phrase : cvspeak::Mode::Word;
        s.word = cvspeak::selectIndex(knob, 0.f, cvspeak::kNumWords);
        s.phrase = cvspeak::selectIndex(knob, 0.f, cvspeak::kNumPhrases);
        s.intonation = params[INTONATION_PARAM].getValue();
        s.chirp = params[CHIRP_PARAM].getValue();
        ui.clampSettings();
    }
};

// Millimetres of the display's active area: a 1.3" 128x64 OLED (SH1106).
static const Vec kOledMm(29.42f, 14.71f);
static const float kEnlarged = 1.5f;  // default VCV size: 1 OLED pixel ~ 1 screen pixel at 100% zoom
static const Vec kDisplayCenterMm(35.56f, 20.5f);

// Draws the shared UI's framebuffer. The pixels are the same ones the
// hardware sends to its OLED.
struct OledDisplay : Widget {
    CVSpeak* module = nullptr;
    cvspeak::Framebuffer fb;

    const NVGcolor lit = nvgRGB(0x9f, 0xe7, 0xff);
    const NVGcolor glass = nvgRGB(0x05, 0x08, 0x0a);

    void step() override {
        const bool hw = module && module->hardwareDisplay;
        const Vec size = mm2px(hw ? kOledMm : kOledMm.mult(kEnlarged));
        box.size = size;
        box.pos = mm2px(kDisplayCenterMm).minus(size.div(2.f));
        Widget::step();
    }

    void draw(const DrawArgs& args) override {
        // Glass around the active area.
        const float m = mm2px(1.5f);
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, -m, -m, box.size.x + 2 * m, box.size.y + 2 * m, mm2px(0.8f));
        nvgFillColor(args.vg, glass);
        nvgFill(args.vg);
    }

    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) return Widget::drawLayer(args, layer);
        static const cvspeak::Ui previewUi;  // module browser
        if (module) {
            module->ui.render(fb, module->status);
        } else {
            previewUi.render(fb, cvspeak::UiStatus{});
        }

        // One rect per horizontal run of lit pixels.
        const float px = box.size.x / cvspeak::Framebuffer::kWidth;
        const float py = box.size.y / cvspeak::Framebuffer::kHeight;
        nvgSave(args.vg);
        nvgShapeAntiAlias(args.vg, false);  // no seams between rows
        nvgBeginPath(args.vg);
        for (int y = 0; y < cvspeak::Framebuffer::kHeight; ++y) {
            int x = 0;
            while (x < cvspeak::Framebuffer::kWidth) {
                if (!fb.pixel(x, y)) {
                    ++x;
                    continue;
                }
                const int start = x;
                while (x < cvspeak::Framebuffer::kWidth && fb.pixel(x, y)) ++x;
                nvgRect(args.vg, start * px, y * py, (x - start) * px, py);
            }
        }
        nvgFillColor(args.vg, lit);
        nvgFill(args.vg);
        nvgRestore(args.vg);
        Widget::drawLayer(args, layer);
    }
};

// Endless push encoder. Drag up/down or scroll to turn; click to push.
struct EncoderKnob : widget::OpaqueWidget {
    CVSpeak* module = nullptr;
    std::shared_ptr<window::Svg> bg, fg;
    float angle = 0.f;
    float dragPx = 0.f, scrollPx = 0.f;
    bool moved = false;

    static constexpr float kDragPxPerDetent = 12.f;
    static constexpr float kScrollPerDetent = 50.f;
    static constexpr int kDetentsPerTurn = 20;

    EncoderKnob() {
        bg = window::Svg::load(asset::system("res/ComponentLibrary/RoundLargeBlackKnob_bg.svg"));
        fg = window::Svg::load(asset::system("res/ComponentLibrary/RoundLargeBlackKnob.svg"));
        box.size = fg ? fg->getSize() : mm2px(Vec(12.f, 12.f));
    }

    void turn(int detents) {
        if (!detents) return;
        angle += detents * 2.f * M_PI / kDetentsPerTurn;
        moved = true;
        if (module) module->encoderTurns += detents;
    }

    void draw(const DrawArgs& args) override {
        if (bg) window::svgDraw(args.vg, bg->handle);
        if (!fg) return;
        nvgSave(args.vg);
        nvgTranslate(args.vg, box.size.x / 2, box.size.y / 2);
        nvgRotate(args.vg, angle);
        nvgTranslate(args.vg, -box.size.x / 2, -box.size.y / 2);
        window::svgDraw(args.vg, fg->handle);
        nvgRestore(args.vg);
    }

    // Leave right-clicks to the module's context menu.
    void onButton(const ButtonEvent& e) override {
        if (e.button == GLFW_MOUSE_BUTTON_LEFT) OpaqueWidget::onButton(e);
    }

    void onDragStart(const DragStartEvent& e) override {
        if (e.button != GLFW_MOUSE_BUTTON_LEFT) return;
        dragPx = 0.f;
        moved = false;
        if (settings::allowCursorLock) APP->window->cursorLock();
    }

    void onDragMove(const DragMoveEvent& e) override {
        if (e.button != GLFW_MOUSE_BUTTON_LEFT) return;
        dragPx += -e.mouseDelta.y;
        const int detents = static_cast<int>(dragPx / kDragPxPerDetent);
        dragPx -= detents * kDragPxPerDetent;
        turn(detents);
    }

    void onDragEnd(const DragEndEvent& e) override {
        if (e.button != GLFW_MOUSE_BUTTON_LEFT) return;
        if (settings::allowCursorLock) APP->window->cursorUnlock();
        if (!moved && module) module->encoderPushes += 1;
    }

    void onHoverScroll(const HoverScrollEvent& e) override {
        scrollPx += e.scrollDelta.y;
        const int detents = static_cast<int>(scrollPx / kScrollPerDetent);
        scrollPx -= detents * kScrollPerDetent;
        turn(detents);
        e.consume(this);
    }
};

// Temporary text labels until the Phase 3c panel artwork (Rack's SVG renderer
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

// Layout in mm on a 14 HP (71.12 mm) panel. Placeholder until Phase 3c.
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

        auto* display = createWidget<OledDisplay>(Vec());
        display->module = module;
        display->step();  // size and place it before the first frame
        addChild(display);
        addChild(createLightCentered<SmallLight<GreenLight>>(
            mm2px(Vec(64.5f, kDisplayCenterMm.y)), module, CVSpeak::SPEAKING_LIGHT));

        // Encoder between Back and Speak, like the hardware's control row.
        auto* encoder = createWidgetCentered<EncoderKnob>(mm2px(Vec(35.56f, 44.f)));
        encoder->module = module;
        addChild(encoder);
        label(35.56f, 37.3f, "SELECT / MENU");
        addParam(createParamCentered<VCVButton>(mm2px(Vec(14.f, 44.f)), module, CVSpeak::BACK_PARAM));
        label(14.f, 39.4f, "BACK");
        addParam(createParamCentered<VCVButton>(mm2px(Vec(57.12f, 44.f)), module, CVSpeak::SPEAK_PARAM));
        label(57.12f, 39.4f, "SPEAK");

        const float kx[3] = {14.f, 35.56f, 57.12f};
        const float jx[4] = {10.5f, 27.2f, 43.9f, 60.6f};
        auto knob = [&](float x, float y, int param, const char* name) {
            addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(x, y)), module, param));
            label(x, y - 6.f, name);
        };
        auto in = [&](int col, float y, int input, const char* name) {
            addInput(createInputCentered<PJ301MPort>(mm2px(Vec(jx[col], y)), module, input));
            label(jx[col], y - 4.6f, name);
        };

        knob(kx[0], 60.f, CVSpeak::PITCH_PARAM, "PITCH");
        knob(kx[1], 60.f, CVSpeak::SPEED_PARAM, "SPEED");
        knob(kx[2], 60.f, CVSpeak::FORMANT_PARAM, "FORMANT");
        knob(24.78f, 76.f, CVSpeak::BEND_PARAM, "BEND");
        knob(46.34f, 76.f, CVSpeak::BLEND_PARAM, "BLEND");

        in(0, 94.f, CVSpeak::TRIG_INPUT, "TRIG");
        in(1, 94.f, CVSpeak::RESET_INPUT, "RESET");
        in(2, 94.f, CVSpeak::VOCT_INPUT, "V/OCT");
        in(3, 94.f, CVSpeak::SELECT_INPUT, "SEL CV");
        in(0, 105.f, CVSpeak::SPEED_INPUT, "SPEED");
        in(1, 105.f, CVSpeak::FORMANT_INPUT, "FORM");
        in(2, 105.f, CVSpeak::BEND_INPUT, "BEND");
        in(3, 105.f, CVSpeak::CARRIER_INPUT, "CARRIER");

        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(jx[2], 116.f)), module, CVSpeak::EOW_OUTPUT));
        label(jx[2], 111.4f, "EOW");
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(jx[3], 116.f)), module, CVSpeak::AUDIO_OUTPUT));
        label(jx[3], 111.4f, "OUT");
    }

    void appendContextMenu(Menu* menu) override {
        auto* module = getModule<CVSpeak>();
        if (!module) return;
        menu->addChild(new MenuSeparator);
        menu->addChild(createBoolPtrMenuItem("Hardware-size display (1.3\" OLED)", "", &module->hardwareDisplay));
    }
};

Model* modelCVSpeak = createModel<CVSpeak, CVSpeakWidget>("CVSpeak");
