#include "AsidEditor.h"

#include "BinaryData.h"

namespace {
constexpr int kRowBox = 114;  // a full-width box holding one inline row (22 title + 84 + 8)

// The content area inside a titled box: sides in, and below the title.
juce::Rectangle<int> innerBox(juce::Rectangle<int> box) {
    return box.reduced(10, 0).withTrimmedTop(22).withTrimmedBottom(8);
}
// Distinct accent per SID voice, so the three plugin windows are colour-coded.
juce::Colour voiceColour(int v) {
    switch (v) {
        case 1:  return juce::Colour(0xfff05cc0);  // Voice 2 - magenta
        case 2:  return juce::Colour(0xffe8a03c);  // Voice 3 - amber
        default: return juce::Colour(0xff35d6d0);  // Voice 1 - cyan
    }
}
// The i-th of n equal columns spanning a row, for spreading controls to fill it.
juce::Rectangle<int> colOf(juce::Rectangle<int> row, int i, int n) {
    const int w = row.getWidth() / n;
    return {row.getX() + i * w, row.getY(), w, row.getHeight()};
}
// Every knob is drawn at this fixed size, so they all match regardless of how
// tall their section is (a taller section just leaves more margin around it).
constexpr int kKnob = 64;
// A knob with its caption, centred in a column both ways. The label+knob group is
// centred vertically so it lines up with the buttons in a mixed row.
void knobInCol(juce::Rectangle<int> col, juce::Slider& s, juce::Label& l) {
    const int side = juce::jmin(col.getWidth() - 8, kKnob);
    auto grp = col.withSizeKeepingCentre(col.getWidth(), 14 + side);
    l.setBounds(grp.removeFromTop(14));
    s.setBounds(grp.withSizeKeepingCentre(side, side));
}
// A toggle button centred in a column both ways.
void toggleInCol(juce::Rectangle<int> col, juce::ToggleButton& b) {
    b.setBounds(col.withSizeKeepingCentre(juce::jmin(col.getWidth() - 8, 96), 30));
}

// Standalone window chrome: paint the JUCE title bar dark (#191a1b) and draw the
// minimise/close glyphs in a neutral grey instead of the default yellow/red. Only
// the title bar and its buttons are overridden; everything else keeps the V4 look.
// Applied to the standalone DocumentWindow only (a DAW owns its own title bar).
struct WindowChromeLnF : juce::LookAndFeel_V4 {
    void drawDocumentWindowTitleBar(juce::DocumentWindow& window, juce::Graphics& g, int w, int h,
                                    int titleSpaceX, int titleSpaceW, const juce::Image* icon,
                                    bool drawTitleTextOnLeft) override {
        juce::ignoreUnused(w, icon);
        g.fillAll(juce::Colour(0xff191a1b));
        g.setColour(juce::Colour(0xffd8d9da));
        g.setFont(juce::Font(juce::FontOptions().withHeight(h * 0.5f)));
        g.drawText(window.getName(), titleSpaceX, 0, titleSpaceW, h,
                   drawTitleTextOnLeft ? juce::Justification::centredLeft : juce::Justification::centred, true);
    }
    juce::Button* createDocumentWindowButton(int buttonType) override {
        juce::Path shape;
        const float t = 0.09f;              // stroke thickness (fraction of the button)
        const float a = 0.32f, b = 0.68f;   // glyph inset, so it stays small and centred
        juce::String name;
        if (buttonType == juce::DocumentWindow::closeButton) {
            name = "close";
            shape.addLineSegment({a, a, b, b}, t);
            shape.addLineSegment({b, a, a, b}, t);
        } else if (buttonType == juce::DocumentWindow::minimiseButton) {
            name = "minimise";
            shape.addLineSegment({a, 0.5f, b, 0.5f}, t);
        } else {
            name = "maximise";
            shape.addLineSegment({a, a, b, a}, t);
            shape.addLineSegment({b, a, b, b}, t);
            shape.addLineSegment({b, b, a, b}, t);
            shape.addLineSegment({a, b, a, a}, t);
        }
        // Pin the path bounds to the whole button so the glyph is not scaled up to
        // fill it (these two points draw nothing, they just set the bounding box).
        shape.startNewSubPath(0.0f, 0.0f);
        shape.startNewSubPath(1.0f, 1.0f);
        const juce::Colour col(0xffbfc1c4);  // neutral grey, no traffic-light hues
        auto* btn = new juce::ShapeButton(name, col, col.brighter(0.4f), col.brighter(0.8f));
        btn->setShape(shape, false, true, false);
        return btn;
    }
};
WindowChromeLnF& windowChromeLnF() { static WindowChromeLnF lnf; return lnf; }
}  // namespace

void AsidEditor::setupKnob(juce::Component& parent, juce::Slider& s, juce::Label& l, const juce::String& name,
                           const juce::String& paramId, std::unique_ptr<SliderAtt>& att) {
    s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);  // value shown in a popup bubble instead
    s.setPopupDisplayEnabled(true, false, this);  // value bubble while turning
    parent.addAndMakeVisible(s);
    l.setText(name, juce::dontSendNotification);
    l.setJustificationType(juce::Justification::centred);
    l.setFont(juce::Font(juce::FontOptions().withHeight(12.0f)));
    parent.addAndMakeVisible(l);
    att = std::make_unique<SliderAtt>(state, paramId, s);
}

void AsidEditor::setupSwitch(juce::ToggleButton& segA, juce::ToggleButton& segB,
                             const juce::String& labelA, const juce::String& labelB,
                             const juce::String& paramId) {
    segA.setButtonText(labelA);
    segB.setButtonText(labelB);
    for (auto* b : {&segA, &segB}) {
        b->setClickingTogglesState(false);  // the parameter drives the state, not the click
        oscPage.addAndMakeVisible(*b);
    }
    // A 2-value switch: clicking either segment flips to the other value. State
    // syncs from the parameter in updateEnablement (and immediately here).
    auto flip = [this, paramId, &segA, &segB] {
        auto* rp = state.getRawParameterValue(paramId);
        const int next = (rp && juce::roundToInt(rp->load()) == 0) ? 1 : 0;
        if (auto* p = state.getParameter(paramId)) {
            p->beginChangeGesture();
            p->setValueNotifyingHost(p->convertTo0to1((float) next));
            p->endChangeGesture();
        }
        segA.setToggleState(next == 0, juce::dontSendNotification);
        segB.setToggleState(next == 1, juce::dontSendNotification);
        segA.repaint();
        segB.repaint();
    };
    segA.onClick = flip;
    segB.onClick = flip;
}

void AsidEditor::setupLfo(juce::Component& parent, LfoControls& u, const juce::String& prefix) {
    u.prefix = prefix;

    // Shape doubles as on/off: "Off" (id 1) disables the LFO, each waveform (id 2+)
    // enables it and selects that shape. Managed by hand since one control drives
    // two parameters (On and Shape).
    parent.addAndMakeVisible(u.shapeBox);
    u.shapeBox.addItem("Off", 1);
    for (const char* s : {"Sine", "Triangle", "Saw Up", "Saw Down", "Square", "Sample & Hold", "Random"})
        u.shapeBox.addItem(s, u.shapeBox.getNumItems() + 1);
    u.shapeBox.onChange = [this, &u] {
        const int sel = u.shapeBox.getSelectedId();
        const bool on = sel > 1;
        auto set = [this](const juce::String& id, float raw) {
            if (auto* p = state.getParameter(id)) {
                p->beginChangeGesture();
                p->setValueNotifyingHost(p->convertTo0to1(raw));
                p->endChangeGesture();
            }
        };
        set(u.prefix + "On", on ? 1.0f : 0.0f);
        if (on) set(u.prefix + "Shape", (float) (sel - 2));  // 0-based shape index
    };

    parent.addAndMakeVisible(u.syncButton);
    u.syncAtt = std::make_unique<ButtonAtt>(state, prefix + "Sync", u.syncButton);
    parent.addAndMakeVisible(u.wheelButton);
    u.wheelAtt = std::make_unique<ButtonAtt>(state, prefix + "Wheel", u.wheelButton);

    // Rate knob: set up the knob shell, then bind it (free Hz by default, or the
    // stepped tempo division when Tempo Sync is on - see configureRateKnob).
    setupKnob(parent, u.rateKnob, u.rateLabel, "Rate", prefix + "Rate", u.rateAtt);
    configureRateKnob(u, false);
    setupKnob(parent, u.depthKnob, u.depthLabel, "Depth", prefix + "Depth", u.depthAtt);
    setupKnob(parent, u.delayKnob, u.delayLabel, "Delay", prefix + "Delay", u.delayAtt);
}

void AsidEditor::configureRateKnob(LfoControls& u, bool synced) {
    u.rateAtt.reset();
    if (synced) {  // bind to the tempo Division choice: stepped, shows 1/4 etc.
        u.rateAtt = std::make_unique<SliderAtt>(state, u.prefix + "Div", u.rateKnob);
    } else {       // bind to the free Hz rate, shown to 3 decimals
        u.rateAtt = std::make_unique<SliderAtt>(state, u.prefix + "Rate", u.rateKnob);
        u.rateKnob.textFromValueFunction = [](double v) { return juce::String(v, 3); };
    }
    u.rateKnob.updateText();
    u.rateMode = synced ? 1 : 0;
}

void AsidEditor::layoutLfo(LfoControls& u, juce::Rectangle<int> area) {
    // One row: Shape (with its Off item), then the Rate, Depth and Delay knobs.
    // Rate carries a small Sync toggle and Depth a small Mod toggle, right beneath
    // them, since each toggle only changes what its own knob does. (Rate becomes
    // the stepped tempo division when Sync is on.)
    const int side = kKnob, togH = 22, togGap = 4, togW = 124;
    const int stackH = 14 + side + togGap + togH;  // label + knob + toggle
    auto row = area.withSizeKeepingCentre(area.getWidth(), stackH);
    // Shape selector, centred against the label + knob band so it lines up.
    u.shapeBox.setBounds(juce::Rectangle<int>(colOf(row, 0, 4).getX(), row.getY(),
                                              colOf(row, 0, 4).getWidth(), 14 + side)
                             .withSizeKeepingCentre(juce::jmin(colOf(row, 0, 4).getWidth() - 8, 160), 30));
    // A knob with its caption above and, optionally, a small toggle below.
    auto place = [&](int i, juce::Slider& s, juce::Label& l, juce::ToggleButton* t) {
        auto col = colOf(row, i, 4);
        l.setBounds(col.getX(), col.getY(), col.getWidth(), 14);
        s.setBounds(juce::Rectangle<int>(col.getX(), col.getY() + 14, col.getWidth(), side)
                        .withSizeKeepingCentre(side, side));
        if (t != nullptr)
            t->setBounds(juce::Rectangle<int>(col.getX(), col.getY() + 14 + side + togGap,
                                              col.getWidth(), togH)
                             .withSizeKeepingCentre(togW, togH));
    };
    place(1, u.rateKnob, u.rateLabel, &u.syncButton);
    place(2, u.depthKnob, u.depthLabel, &u.wheelButton);
    place(3, u.delayKnob, u.delayLabel, nullptr);
}

AsidEditor::AsidEditor(AsidProcessor& p)
    : juce::AudioProcessorEditor(p), proc(p), state(p.state()) {
    setLookAndFeel(&laf);
    laf.setAccent(voiceColour(currentVoice()));  // colour-code this voice's window
    logo = juce::Drawable::createFromImageData(BinaryData::DehliMusikkLogoInverseHorizontal_svg,
                                               BinaryData::DehliMusikkLogoInverseHorizontal_svgSize);
    // Optional decorative overlays (embedded if the PNGs are present in assets/).
    // Looked up by name so missing files just leave an invalid, unused image.
    auto loadImage = [](const juce::String& resName) {
        int sz = 0;
        if (const char* d = BinaryData::getNamedResource(resName.toRawUTF8(), sz))
            return juce::ImageFileFormat::loadFrom(d, (size_t) sz);
        return juce::Image();
    };
    overlayShared = loadImage("Overlay_png");
    for (int i = 0; i < 3; ++i)
        overlay[i] = loadImage("OverlayVoice" + juce::String(i + 1) + "_png");
    title.setFont(SidLookAndFeel::mono(26.0f, true));  // match the VOICE N size
    title.setColour(juce::Label::textColourId, juce::Colour(SidLookAndFeel::kHot));
    title.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(title);

    addAndMakeVisible(oscPage);
    addChildComponent(ampModPage);  // hidden until its tab is chosen
    addChildComponent(sharedPage);
    addChildComponent(wtPage);

    // Tab bar: a radio row of buttons that switch which page shows.
    auto tab = [this](juce::TextButton& b, int index) {
        b.setClickingTogglesState(true);
        b.setRadioGroupId(1);
        b.setColour(juce::TextButton::buttonOnColourId, juce::Colour(SidLookAndFeel::kFg));
        b.setColour(juce::TextButton::textColourOnId, juce::Colour(SidLookAndFeel::kBg));
        b.onClick = [this, index] { setTab(index); };
        addAndMakeVisible(b);
    };
    tab(oscTabBtn, 0);     // VOICE
    tab(waveTabBtn, 1);    // WAVETABLE
    tab(ampModTabBtn, 2);  // MODULATION
    tab(sharedTabBtn, 3);  // GLOBAL

    // Group boxes go into their page first, so the frames sit behind the controls.
    auto group = [](juce::Component& page, juce::GroupComponent& g, const juce::String& t) {
        g.setText(t);
        g.setInterceptsMouseClicks(false, false);  // decorative frame only
        page.addAndMakeVisible(g);
    };
    group(oscPage, oscGroup, "OSCILLATOR");
    group(oscPage, glideGroup, "TUNING");
    group(oscPage, ampGroup, "AMP ENVELOPE");
    group(ampModPage, pitchModGroup, "PITCH MODULATION");
    group(ampModPage, pwModGroup, "PULSE WIDTH MODULATION");
    group(sharedPage, filterGroup, "FILTER");
    group(sharedPage, cutModGroup, "CUTOFF MODULATION");
    group(sharedPage, masterGroup, "MASTER");
    group(wtPage, wtConfigGroup, "WAVETABLE");
    group(wtPage, wtStepsGroup, "STEPS");

    // Global header (above the tabs): MIDI out and SID voice apply to the whole
    // instance, so they sit outside the tabbed pages.
    addAndMakeVisible(outLabel);
    addAndMakeVisible(outputBox);
    addAndMakeVisible(refreshButton);
    // Tempo field (replaces the old SID Voice selector; voice moved to the top-right
    // switch). A hidden slider holds the "bpm" param; bpmField is the visible number.
    addAndMakeVisible(bpmLabel);
    addChildComponent(bpmSlider);
    bpmAtt = std::make_unique<SliderAtt>(state, "bpm", bpmSlider);
    bpmField.setJustificationType(juce::Justification::centred);
    bpmField.setFont(juce::Font(juce::FontOptions().withHeight(14.0f)));
    bpmField.getProperties().set("sidField", true);  // draw as a bordered field
    bpmField.setColour(juce::Label::backgroundColourId, juce::Colour(SidLookAndFeel::kPanel));
    bpmField.setColour(juce::Label::textColourId, juce::Colour(SidLookAndFeel::kHot));
    bpmField.setEditable(true, true, false);  // standalone: click to type (toggled off in a DAW)
    addAndMakeVisible(bpmField);
    bpmField.onTextChange = [this] {
        bpmSlider.setValue(juce::jlimit(20, 300, bpmField.getText().getIntValue()),
                           juce::sendNotificationSync);
    };
    bpmSlider.onValueChange = [this] {
        if (proc.hostBpm() <= 0.0 && !bpmField.isBeingEdited())
            bpmField.setText(juce::String((int) bpmSlider.getValue()), juce::dontSendNotification);
    };

    // Voice selector, top right: three coloured cells, click to switch this
    // instance's SID voice. Recolours the whole window (see updateEnablement).
    for (int i = 0; i < 3; ++i) voiceSwitch.colours[i] = voiceColour(i);
    voiceSwitch.selected = currentVoice();
    voiceSwitch.onSelect = [this](int v) {
        if (auto* vp = state.getParameter("asidVoice")) {
            vp->beginChangeGesture();
            vp->setValueNotifyingHost(vp->convertTo0to1((float) v));
            vp->endChangeGesture();
        }
        voiceSwitch.selected = v;  // light the cell now; updateEnablement recolours the rest
        voiceSwitch.repaint();
    };
    addAndMakeVisible(voiceSwitch);
    voiceCaption.setJustificationType(juce::Justification::centredRight);
    voiceCaption.setColour(juce::Label::textColourId, juce::Colour(SidLookAndFeel::kHot));  // white
    voiceCaption.setFont(SidLookAndFeel::mono(24.0f, true));
    addAndMakeVisible(voiceCaption);
    addAndMakeVisible(midiLoadLabel);
    addAndMakeVisible(modRateLabel);
    addAndMakeVisible(modRateBox);
    for (const char* r : {"Eco 25 Hz", "PAL 50 Hz", "NTSC 60 Hz", "Smooth 100 Hz"})
        modRateBox.addItem(r, modRateBox.getNumItems() + 1);
    modRateAtt = std::make_unique<ComboAtt>(state, "modRate", modRateBox);

    // ---- OSC page: Oscillator, Glide ---- (waveform toggles are self-labelled
    // buttons now, so no "Waveform" caption).
    for (auto* b : {&waveTriButton, &waveSawButton, &wavePulseButton, &waveNoiseButton})
        oscPage.addAndMakeVisible(*b);
    oscPage.addAndMakeVisible(syncButton);
    oscPage.addAndMakeVisible(ringButton);
    oscPage.addAndMakeVisible(testButton);
    oscPage.addAndMakeVisible(oscDiv1);
    oscPage.addAndMakeVisible(oscDiv2);

    outputBox.onChange = [this] {
        const int id = outputBox.getSelectedId();
        if (id >= 1 && id <= outDevices.size()) {
            proc.midi().openOutputByIdentifier(outDevices[id - 1].identifier);
            // Shared output: make every instance re-push its voice to the new device.
            AsidShared::get().outGeneration.fetch_add(1);
        }
    };
    refreshButton.onClick = [this] { refreshDevices(); };

    waveTriAtt = std::make_unique<ButtonAtt>(state, "waveTri", waveTriButton);
    waveSawAtt = std::make_unique<ButtonAtt>(state, "waveSaw", waveSawButton);
    wavePulseAtt = std::make_unique<ButtonAtt>(state, "wavePulse", wavePulseButton);
    waveNoiseAtt = std::make_unique<ButtonAtt>(state, "waveNoise", waveNoiseButton);

    setupKnob(oscPage, pwKnob, pwLabel, "Pulse Width", "pulseWidth", pwAtt);
    setupKnob(oscPage, coarseKnob, coarseLabel, "Coarse", "coarse", coarseAtt);
    setupKnob(oscPage, fineKnob, fineLabel, "Fine", "fine", fineAtt);
    // These have a natural centre default, so their arcs fill out from the centre.
    for (auto* k : {&pwKnob, &coarseKnob, &fineKnob}) k->getProperties().set("sidBipolar", true);
    setupKnob(oscPage, bendKnob, bendLabel, "Bend Range", "pitchBendRange", bendAtt);
    syncAtt = std::make_unique<ButtonAtt>(state, "sync", syncButton);
    ringAtt = std::make_unique<ButtonAtt>(state, "ring", ringButton);
    testAtt = std::make_unique<ButtonAtt>(state, "test", testButton);

    setupKnob(oscPage, portaKnob, portaLabel, "Glide time", "portaTime", portaAtt);
    setupSwitch(portaTrigBtns[0], portaTrigBtns[1], "Legato", "Always", "portaTrigger");
    setupSwitch(portaTypeBtns[0], portaTypeBtns[1], "Smooth", "Stepped", "portaType");
    oscPage.addAndMakeVisible(tuningDiv1);
    oscPage.addAndMakeVisible(tuningDiv2);

    // ---- AMP+MOD page: Amp envelope, Pitch Mod, PW Mod ----
    setupKnob(oscPage, attackKnob, attackLabel, "Attack", "attack", attackAtt);
    setupKnob(oscPage, decayKnob, decayLabel, "Decay", "decay", decayAtt);
    setupKnob(oscPage, sustainKnob, sustainLabel, "Sustain", "sustain", sustainAtt);
    setupKnob(oscPage, releaseKnob, releaseLabel, "Release", "release", releaseAtt);
    setupLfo(ampModPage, pitchLfoUi, "pitchLfo");
    setupLfo(ampModPage, pwLfoUi, "pwLfo");

    // ---- SHARED page: Filter (with Cutoff Mod) and Master ----
    // Filter Active: one checkbox per voice, all shared, so any instance can
    // route any voice. Filter Mode: LP/BP/HP, combinable.
    const char* voiceNames[3] = {"V1", "V2", "V3"};  // self-labelled, no caption
    const char* filtIds[3] = {"filt1", "filt2", "filt3"};
    for (int i = 0; i < 3; ++i) {
        filtButtons[i].setButtonText(voiceNames[i]);
        sharedPage.addAndMakeVisible(filtButtons[i]);
        filtAtts[i] = std::make_unique<ButtonAtt>(state, filtIds[i], filtButtons[i]);
    }
    const char* modeNames[3] = {"LP", "BP", "HP"};
    const char* modeIds[3] = {"modeLP", "modeBP", "modeHP"};
    for (int i = 0; i < 3; ++i) {
        modeButtons[i].setButtonText(modeNames[i]);
        sharedPage.addAndMakeVisible(modeButtons[i]);
        modeAtts[i] = std::make_unique<ButtonAtt>(state, modeIds[i], modeButtons[i]);
    }
    filtExtButton.setButtonText("Ext");
    sharedPage.addAndMakeVisible(filtExtButton);
    filtExtAtt = std::make_unique<ButtonAtt>(state, "filtExt", filtExtButton);
    sharedPage.addAndMakeVisible(filtDiv1);
    sharedPage.addAndMakeVisible(filtDiv2);
    setupKnob(sharedPage, cutoffKnob, cutoffLabel, "Cutoff", "cutoff", cutoffAtt);
    setupKnob(sharedPage, resKnob, resLabel, "Resonance", "resonance", resAtt);
    setupLfo(sharedPage, cutLfoUi, "cutLfo");
    setupKnob(sharedPage, volumeKnob, volumeLabel, "Volume", "volume", volumeAtt);
    setupKnob(sharedPage, latencyKnob, latencyLabel, "Latency", "latency", latencyAtt);
    sharedPage.addAndMakeVisible(voice3offButton);
    voice3offAtt = std::make_unique<ButtonAtt>(state, "voice3off", voice3offButton);

    // ---- WAVE page: wavetable config and the per-step rows ----
    wtPage.addAndMakeVisible(wtOnButton);
    wtOnAtt = std::make_unique<ButtonAtt>(state, "wtOn", wtOnButton);
    setupKnob(wtPage, wtSpeedKnob, wtSpeedLabel, "Speed", "wtSpeed", wtSpeedAtt);
    setupKnob(wtPage, wtLengthKnob, wtLengthLabel, "Length", "wtLength", wtLengthAtt);
    setupKnob(wtPage, wtLoopKnob, wtLoopLabel, "Loop", "wtLoop", wtLoopAtt);
    const char* wtHeads[4] = {"Triangle", "Sawtooth", "Pulse", "Noise"};
    const char* wtIds[4] = {"wtTri", "wtSaw", "wtPulse", "wtNoise"};
    for (int w = 0; w < 4; ++w) {
        wtWaveHead[w].setText(wtHeads[w], juce::dontSendNotification);
        wtWaveHead[w].setJustificationType(juce::Justification::centred);
        wtPage.addAndMakeVisible(wtWaveHead[w]);
    }
    for (auto* h : {&wtSyncHead, &wtRingHead, &wtPwHead, &wtArpHead}) {
        h->setJustificationType(juce::Justification::centred);
        wtPage.addAndMakeVisible(*h);
    }
    for (int i = 0; i < AsidProcessor::kWtSteps; ++i) {
        wtStepInd[i].number = i + 1;
        wtPage.addAndMakeVisible(wtStepInd[i]);
        for (int w = 0; w < 4; ++w) {
            wtPage.addAndMakeVisible(wtWaveTog[i][w]);
            wtWaveTogAtt[i][w] = std::make_unique<ButtonAtt>(
                state, juce::String(wtIds[w]) + juce::String(i), wtWaveTog[i][w]);
        }
        wtPage.addAndMakeVisible(wtSyncTog[i]);
        wtSyncAtt[i] = std::make_unique<ButtonAtt>(state, "wtSync" + juce::String(i), wtSyncTog[i]);
        wtPage.addAndMakeVisible(wtRingTog[i]);
        wtRingAtt[i] = std::make_unique<ButtonAtt>(state, "wtRing" + juce::String(i), wtRingTog[i]);
        wtPwKnob[i].setSliderStyle(juce::Slider::LinearHorizontal);
        wtPwKnob[i].setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        wtPwKnob[i].setPopupDisplayEnabled(true, false, this);  // value bubble while dragging
        wtPwKnob[i].getProperties().set("sidBipolar", true);  // fill from the centre default
        wtPage.addAndMakeVisible(wtPwKnob[i]);
        wtPwAtt[i] = std::make_unique<SliderAtt>(state, "wtPw" + juce::String(i), wtPwKnob[i]);
        // Hidden slider: the value model / APVTS binding. UI is the field + buttons.
        wtPage.addChildComponent(wtArpSlider[i]);
        wtArpAtt[i] = std::make_unique<SliderAtt>(state, "wtArp" + juce::String(i), wtArpSlider[i]);
        wtArpValue[i].setJustificationType(juce::Justification::centred);
        wtArpValue[i].setFont(juce::Font(juce::FontOptions().withHeight(13.0f)));
        wtArpValue[i].getProperties().set("sidField", true);  // draw as a bordered field
        wtArpValue[i].setColour(juce::Label::backgroundColourId, juce::Colour(SidLookAndFeel::kPanel));
        wtArpValue[i].setColour(juce::Label::textColourId, juce::Colour(SidLookAndFeel::kHot));
        wtPage.addAndMakeVisible(wtArpValue[i]);
        wtArpSlider[i].onValueChange = [this, i] {
            wtArpValue[i].setText(juce::String((int) wtArpSlider[i].getValue()), juce::dontSendNotification);
        };
        wtArpValue[i].setText(juce::String((int) wtArpSlider[i].getValue()), juce::dontSendNotification);
        wtArpDec[i].setButtonText("-");
        wtArpInc[i].setButtonText("+");
        wtPage.addAndMakeVisible(wtArpDec[i]);
        wtPage.addAndMakeVisible(wtArpInc[i]);
        wtArpDec[i].onClick = [this, i] {
            wtArpSlider[i].setValue(wtArpSlider[i].getValue() - 1, juce::sendNotificationSync);
        };
        wtArpInc[i].onClick = [this, i] {
            wtArpSlider[i].setValue(wtArpSlider[i].getValue() + 1, juce::sendNotificationSync);
        };
    }

    refreshDevices();
    updateEnablement();
    setTab(0);
    setSize(720, 540);  // 4:3, wide boxes stacked one per row
    startTimerHz(10);  // drives updateEnablement (waveform / sustain / LFO gating)
}

AsidEditor::~AsidEditor() {
    stopTimer();
    // Detach our chrome look-and-feel from the standalone window before it (or we)
    // go away, so the window is not left pointing at a destroyed editor's context.
    if (auto* dw = findParentComponentOfClass<juce::DocumentWindow>())
        if (&dw->getLookAndFeel() == &windowChromeLnF())
            dw->setLookAndFeel(nullptr);
    setLookAndFeel(nullptr);
}

void AsidEditor::setTab(int t) {
    currentTab = t;
    oscPage.setVisible(t == 0);     // VOICE
    wtPage.setVisible(t == 1);      // WAVETABLE
    ampModPage.setVisible(t == 2);  // MODULATION
    sharedPage.setVisible(t == 3);  // GLOBAL
    oscTabBtn.setToggleState(t == 0, juce::dontSendNotification);
    waveTabBtn.setToggleState(t == 1, juce::dontSendNotification);
    ampModTabBtn.setToggleState(t == 2, juce::dontSendNotification);
    sharedTabBtn.setToggleState(t == 3, juce::dontSendNotification);
}

void AsidEditor::updateEnablement() {
    auto boolParam = [this](const char* id) {
        auto* p = state.getRawParameterValue(id);
        return p && p->load() > 0.5f;
    };
    auto intParam = [this](const char* id) {
        auto* p = state.getRawParameterValue(id);
        return p ? juce::roundToInt(p->load()) : 0;
    };

    // Noise locks the other waveforms on the 6581, so it is exclusive: when it is
    // on, grey out the other three (they keep their state but are ignored).
    const bool noise = intParam("waveNoise") != 0;
    waveTriButton.setEnabled(!noise);
    waveSawButton.setEnabled(!noise);
    wavePulseButton.setEnabled(!noise);

    // Pulse width only matters when the pulse wave actually sounds.
    const bool pulse = intParam("wavePulse") != 0 && !noise;
    pwKnob.setEnabled(pulse);
    pwLabel.setEnabled(pulse);

    // Hard sync is meaningless on a noise-only voice; ring mod needs the triangle.
    syncButton.setEnabled(!noise);
    ringButton.setEnabled(intParam("waveTri") != 0 && !noise);

    // Glide trigger and type only matter when portamento time is up. Reflect the
    // choice value in each 2-segment switch (which segment is lit).
    const bool porta = intParam("portaTime") > 0;
    auto syncSwitch = [porta](juce::ToggleButton& a, juce::ToggleButton& b, int val) {
        a.setToggleState(val == 0, juce::dontSendNotification);
        b.setToggleState(val == 1, juce::dontSendNotification);
        a.setEnabled(porta);
        b.setEnabled(porta);
    };
    syncSwitch(portaTrigBtns[0], portaTrigBtns[1], intParam("portaTrigger"));
    syncSwitch(portaTypeBtns[0], portaTypeBtns[1], intParam("portaType"));

    // The wavetable's config and steps are live only when it is on. Steps beyond
    // the table length are greyed; the loop point and the playing step are marked
    // on the per-step indicators.
    const bool wtOn = boolParam("wtOn");
    for (auto* s : {&wtSpeedKnob, &wtLengthKnob, &wtLoopKnob})
        s->setEnabled(wtOn);
    for (auto& h : wtWaveHead) h.setEnabled(wtOn);
    for (auto* h : {&wtSyncHead, &wtRingHead, &wtPwHead, &wtArpHead}) h->setEnabled(wtOn);
    const int wtLen = intParam("wtLength");
    const int wtLoopPt = intParam("wtLoop");
    const int wtPlaying = proc.wtStep();
    const auto acc = voiceColour(currentVoice());
    for (int i = 0; i < AsidProcessor::kWtSteps; ++i) {
        const bool rowActive = wtOn && i < wtLen;
        // Noise is exclusive per step, same as the oscillator: grey the other three.
        const bool stepNoise = intParam((juce::String("wtNoise") + juce::String(i)).toRawUTF8()) != 0;
        for (int w = 0; w < 4; ++w)
            wtWaveTog[i][w].setEnabled(rowActive && (w == 3 || !stepNoise));
        wtSyncTog[i].setEnabled(rowActive);
        wtRingTog[i].setEnabled(rowActive);
        wtPwKnob[i].setEnabled(rowActive);
        wtArpValue[i].setEnabled(rowActive);
        wtArpDec[i].setEnabled(rowActive);
        wtArpInc[i].setEnabled(rowActive);
        auto& ind = wtStepInd[i];
        const bool loop = wtOn && i == wtLoopPt, playing = i == wtPlaying;
        if (ind.active != rowActive || ind.loop != loop || ind.playing != playing || ind.accent != acc) {
            ind.active = rowActive; ind.loop = loop; ind.playing = playing; ind.accent = acc;
            ind.repaint();
        }
    }

    // Re-bind each LFO's rate knob when its Tempo Sync toggles (free Hz vs the
    // stepped tempo division). Only on change, so it does not thrash every tick.
    for (auto* u : {&pitchLfoUi, &pwLfoUi, &cutLfoUi}) {
        const bool synced = boolParam((u->prefix + "Sync").toRawUTF8());
        if ((synced ? 1 : 0) != u->rateMode) configureRateKnob(*u, synced);
    }

    // The filter checkboxes are shared across instances; mark this instance's own
    // voice so you can see which of the three it drives. Only touch it on change.
    const int myVoice = juce::jlimit(0, 2, intParam("asidVoice"));
    if (myVoice != highlightedVoice) {
        highlightedVoice = myVoice;
        laf.setAccent(voiceColour(myVoice));  // recolour the whole window for the new voice
        voiceSwitch.selected = myVoice;       // light the selected cell in the top-right switch
        for (int i = 0; i < 3; ++i)
            filtButtons[i].getProperties().set("sidHighlight", i == myVoice);
        repaint();  // border, voice switch, and every control pick up the new accent
    }

    // Tempo field: a DAW drives it (host BPM, read-only); standalone lets the user
    // edit the BPM parameter. Toggle editability only when the source changes.
    const double hb = proc.hostBpm();
    const bool hostDriven = hb > 0.0;
    if (hostDriven != bpmHostDriven) {
        bpmHostDriven = hostDriven;
        bpmField.setEditable(!hostDriven, !hostDriven, false);
    }
    if (hostDriven) {
        const int r = juce::roundToInt(hb);
        const bool whole = hb - r < 0.05 && r - hb < 0.05;  // show "120", not "120.0"
        bpmField.setText(whole ? juce::String(r) : juce::String(hb, 1), juce::dontSendNotification);
    } else if (!bpmField.isBeingEdited()) {
        bpmField.setText(juce::String((int) bpmSlider.getValue()), juce::dontSendNotification);
    }

    // Decay is inaudible at full sustain and only feeds the ADSR bug there, so
    // disable it and pin it to 0 when sustain is 15.
    const bool sustainMax = intParam("sustain") == 15;
    decayKnob.setEnabled(!sustainMax);
    decayLabel.setEnabled(!sustainMax);
    if (sustainMax && decayKnob.getValue() != 0.0)
        decayKnob.setValue(0.0, juce::sendNotificationSync);

    // Shape doubles as on/off, so reflect each LFO's On + Shape into its selector.
    // Never grey the selector itself, or the LFO could not be switched back on.
    for (auto* u : {&pitchLfoUi, &pwLfoUi, &cutLfoUi}) {
        const bool on = boolParam((u->prefix + "On").toRawUTF8());
        const int wantId = on ? intParam((u->prefix + "Shape").toRawUTF8()) + 2 : 1;
        if (u->shapeBox.getSelectedId() != wantId)
            u->shapeBox.setSelectedId(wantId, juce::dontSendNotification);
    }
    // Grey the rest of each LFO's controls when it is off. The rate knob works in
    // either mode (free Hz or the stepped tempo division), so it follows the LFO.
    auto applyLfo = [](LfoControls& u, bool on) {
        u.syncButton.setEnabled(on);
        u.wheelButton.setEnabled(on);
        u.depthKnob.setEnabled(on);
        u.depthLabel.setEnabled(on);
        u.rateKnob.setEnabled(on);
        u.rateLabel.setEnabled(on);
        u.delayKnob.setEnabled(on);
        u.delayLabel.setEnabled(on);
    };
    applyLfo(pitchLfoUi, boolParam("pitchLfoOn"));
    applyLfo(cutLfoUi, boolParam("cutLfoOn"));
    // The PW LFO only works on a pulse wave, so its Shape selector greys out too.
    pwLfoUi.shapeBox.setEnabled(pulse);
    applyLfo(pwLfoUi, pulse && boolParam("pwLfoOn"));
}

void AsidEditor::timerCallback() {
    updateEnablement();

    // MIDI load: bytes sent (all instances) since the last poll, over the ceiling.
    const long long now = AsidShared::get().bytesSent.load(std::memory_order_relaxed);
    const double nowMs = juce::Time::getMillisecondCounterHiRes();
    if (lastBytesMs > 0.0) {
        const double dt = (nowMs - lastBytesMs) / 1000.0;
        if (dt > 0.0) {
            const double rate = static_cast<double>(now - lastBytes) / dt;  // bytes/sec
            const float load = static_cast<float>(rate / AsidShared::kMidiBytesPerSec);
            midiLoad = 0.6f * midiLoad + 0.4f * load;  // smooth the reading
            repaint(meterArea);
        }
    }
    lastBytes = now;
    lastBytesMs = nowMs;
}

void AsidEditor::refreshDevices() {
    outDevices = MidiHub::availableOutputs();
    outputBox.clear(juce::dontSendNotification);
    for (int i = 0; i < outDevices.size(); ++i)
        outputBox.addItem(outDevices[i].name, i + 1);
    const auto openId = proc.midi().outputIdentifier();
    for (int i = 0; i < outDevices.size(); ++i)
        if (outDevices[i].identifier == openId)
            outputBox.setSelectedId(i + 1, juce::dontSendNotification);
}

void AsidEditor::paint(juce::Graphics& g) {
    // C64 screen: the neutral light-blue border framing the darker screen. The
    // per-voice colour lives in the accent (arcs, active buttons) and the voice
    // switch (top right), which are components drawn over this.
    g.fillAll(juce::Colour(SidLookAndFeel::kFg));
    g.setColour(juce::Colour(SidLookAndFeel::kBg));
    g.fillRect(getLocalBounds().reduced(kBorder));

    // Top row: Dehli Musikk logo left, product title centred (the Label), and the
    // voice caption + switch right (components, positioned in resized()).
    auto titleRow = getLocalBounds().reduced(kBorder + 6).removeFromTop(34);
    if (logo != nullptr)
        logo->drawWithin(g, titleRow.removeFromLeft(150).reduced(0, 3).toFloat(),
                         juce::RectanglePlacement(juce::RectanglePlacement::xLeft
                                                  | juce::RectanglePlacement::yMid), 1.0f);

    // MIDI load meter: segmented blocks lit to the current fraction. The top
    // fifth turns white as a warning; over 100% every block is white (overload).
    if (!meterArea.isEmpty()) {
        // Segments sit inside the 2px border, so the frame matches the other fields.
        auto inner = meterArea.reduced(2);
        const int segs = 20;
        const int gap = 2;
        const int segW = (inner.getWidth() - (segs - 1) * gap) / segs;
        const bool over = midiLoad >= 1.0f;
        for (int i = 0; i < segs; ++i) {
            juce::Rectangle<int> seg(inner.getX() + i * (segW + gap), inner.getY(),
                                     segW, inner.getHeight());
            const bool lit = midiLoad > static_cast<float>(i) / segs;
            juce::Colour col;
            if (!lit) col = juce::Colour(SidLookAndFeel::kPanel);
            else if (over || i >= 16) col = juce::Colour(SidLookAndFeel::kHot);  // warning / overload
            else col = juce::Colour(SidLookAndFeel::kFg);
            g.setColour(col);
            g.fillRect(seg);
        }
        g.setColour(juce::Colour(SidLookAndFeel::kFg));
        g.drawRect(meterArea, 2);
    }
}

void AsidEditor::paintOverChildren(juce::Graphics& g) {
    // Drawn after every child, so the overlay sits on top of the controls but,
    // being pure paint, leaves all mouse handling untouched. Per-voice image if
    // one was embedded, else the shared overlay; nothing if neither is present.
    const int v = currentVoice();
    const juce::Image& img = (v >= 0 && v < 3 && overlay[v].isValid()) ? overlay[v] : overlayShared;
    if (img.isValid()) {
        auto b = getLocalBounds();
        g.drawImageWithin(img, b.getX(), b.getY(), b.getWidth(), b.getHeight(),
                          juce::RectanglePlacement::stretchToFit);
    }
}

void AsidEditor::parentHierarchyChanged() {
    // Standalone only: recolour the host window's title bar and drop the yellow/red
    // from its buttons. In a DAW the host owns the title bar, so leave it untouched.
    if (proc.wrapperType != juce::AudioProcessor::wrapperType_Standalone) return;
    // Do it asynchronously and preserve the window bounds: swapping a
    // DocumentWindow's look-and-feel while it is still mid-setup (editor not yet
    // sized) makes it fit to a zero-size content and collapse to a tiny square.
    juce::Component::SafePointer<AsidEditor> self(this);
    juce::MessageManager::callAsync([self] {
        if (self == nullptr) return;
        auto* dw = self->findParentComponentOfClass<juce::DocumentWindow>();
        if (dw == nullptr || &dw->getLookAndFeel() == &windowChromeLnF()) return;
        const auto keep = dw->getBounds();
        dw->setLookAndFeel(&windowChromeLnF());
        if (dw->getBounds() != keep) dw->setBounds(keep);  // undo any collapse
    });
}

void AsidEditor::resized() {
    auto area = getLocalBounds().reduced(kBorder + 6);  // inside the C64 border
    auto titleRow = area.removeFromTop(34);
    title.setBounds(titleRow);  // full width, text centred
    {   // Voice selector in the top-right: "VOICE" caption then the 3-cell switch.
        auto sw = titleRow.removeFromRight(96);
        voiceSwitch.setBounds(sw.withSizeKeepingCentre(96, 28));
        titleRow.removeFromRight(8);
        voiceCaption.setBounds(titleRow.removeFromRight(84));
    }
    area.removeFromTop(6);

    // Global header (above the tabs): MIDI out (+ Refresh) and SID voice, laid
    // horizontally with labels over the controls.
    auto header = area.removeFromTop(38);
    auto midi = header.removeFromLeft(200);
    outLabel.setBounds(midi.removeFromTop(14));
    refreshButton.setBounds(midi.removeFromRight(58));
    midi.removeFromRight(6);
    outputBox.setBounds(midi);
    header.removeFromLeft(12);
    auto tempo = header.removeFromLeft(108);
    bpmLabel.setBounds(tempo.removeFromTop(14));
    bpmField.setBounds(tempo.removeFromTop(24));
    header.removeFromLeft(12);
    auto mod = header.removeFromLeft(128);
    modRateLabel.setBounds(mod.removeFromTop(14));
    modRateBox.setBounds(mod.removeFromTop(24));
    header.removeFromLeft(12);
    midiLoadLabel.setBounds(header.removeFromTop(14));
    meterArea = header.removeFromTop(24);
    area.removeFromTop(8);

    auto tabs = area.removeFromTop(26);
    const int tw = (tabs.getWidth() - 12) / 4;
    oscTabBtn.setBounds(tabs.removeFromLeft(tw));    tabs.removeFromLeft(4);  // VOICE
    waveTabBtn.setBounds(tabs.removeFromLeft(tw));   tabs.removeFromLeft(4);  // WAVETABLE
    ampModTabBtn.setBounds(tabs.removeFromLeft(tw)); tabs.removeFromLeft(4);  // MODULATION
    sharedTabBtn.setBounds(tabs.removeFromLeft(tw));                          // GLOBAL
    area.removeFromTop(8);

    // All pages fill the same area; each lays out in its own local coords.
    oscPage.setBounds(area);
    ampModPage.setBounds(area);
    sharedPage.setBounds(area);
    wtPage.setBounds(area);
    layoutOscPage(oscPage.getLocalBounds());
    layoutAmpModPage(ampModPage.getLocalBounds());
    layoutSharedPage(sharedPage.getLocalBounds());
    layoutWavePage(wtPage.getLocalBounds());
}

void AsidEditor::layoutOscPage(juce::Rectangle<int> area) {
    const int gap = 10;
    const int rowH = (area.getHeight() - 2 * gap) / 3;
    {  // OSCILLATOR in three groups: waveform | Sync/Ring | Pulse Width.
        auto box = area.removeFromTop(rowH);
        oscGroup.setBounds(box);
        auto c = innerBox(box);
        const int gGap = 34;
        // Group 1: 2x2 self-labelled waveform buttons, vertically centred.
        {
            auto cell = c.removeFromLeft(190).withSizeKeepingCentre(190, 2 * 30 + 8);
            auto r1 = cell.removeFromTop(30);
            waveTriButton.setBounds(r1.removeFromLeft(88)); r1.removeFromLeft(10);
            waveSawButton.setBounds(r1.removeFromLeft(88));
            cell.removeFromTop(8);
            auto r2 = cell.removeFromTop(30);
            wavePulseButton.setBounds(r2.removeFromLeft(88)); r2.removeFromLeft(10);
            waveNoiseButton.setBounds(r2.removeFromLeft(88));
        }
        placeDivider(oscDiv1, c.removeFromLeft(gGap));
        // Group 2: Sync + Ring + Test, centred as a row.
        {
            const int bw = 58, bg = 10, groupW = 3 * bw + 2 * bg;
            auto row = c.removeFromLeft(groupW + 16).withSizeKeepingCentre(groupW, 30);
            syncButton.setBounds(row.removeFromLeft(bw)); row.removeFromLeft(bg);
            ringButton.setBounds(row.removeFromLeft(bw)); row.removeFromLeft(bg);
            testButton.setBounds(row.removeFromLeft(bw));
        }
        placeDivider(oscDiv2, c.removeFromLeft(gGap));
        // Group 3: Pulse Width (rest).
        knobInCol(c, pwKnob, pwLabel);
    }
    area.removeFromTop(gap);
    {  // TUNING in three groups: (Coarse, Fine) | (Bend Range) | (Glide time +
       // stacked trigger/type switches).
        auto box = area.removeFromTop(rowH);
        glideGroup.setBounds(box);
        auto c = innerBox(box);
        const int gGap = 34;  // gap between groups (wider than within a group)
        // A 2-segment switch: the second segment overlaps the first by 2px so the
        // shared divider is a single 2px line, matching the outer border.
        auto laySwitch = [](juce::Rectangle<int> row, juce::ToggleButton& a, juce::ToggleButton& b) {
            const int half = row.getWidth() / 2;
            a.setBounds(row.getX(), row.getY(), half, row.getHeight());
            b.setBounds(row.getX() + half - 2, row.getY(), row.getWidth() - half + 2, row.getHeight());
        };
        // Group 1: Coarse, Fine.
        auto g1 = c.removeFromLeft(168);
        knobInCol(colOf(g1, 0, 2), coarseKnob, coarseLabel);
        knobInCol(colOf(g1, 1, 2), fineKnob, fineLabel);
        placeDivider(tuningDiv1, c.removeFromLeft(gGap));
        // Group 2: Bend Range.
        knobInCol(c.removeFromLeft(96), bendKnob, bendLabel);
        placeDivider(tuningDiv2, c.removeFromLeft(gGap));
        // Group 3: Glide time knob + the two stacked switches, kept close together.
        const int knobW = 96, swW = 172, innerGap = 8;
        auto g3 = c.withSizeKeepingCentre(knobW + innerGap + swW, c.getHeight());
        knobInCol(g3.removeFromLeft(knobW), portaKnob, portaLabel);
        g3.removeFromLeft(innerGap);
        auto stack = g3.withSizeKeepingCentre(swW, 2 * 28 + 8);
        laySwitch(stack.removeFromTop(28), portaTrigBtns[0], portaTrigBtns[1]);
        stack.removeFromTop(8);
        laySwitch(stack.removeFromTop(28), portaTypeBtns[0], portaTypeBtns[1]);
    }
    area.removeFromTop(gap);
    {  // AMP ENVELOPE
        auto box = area.removeFromTop(rowH);
        ampGroup.setBounds(box);
        auto c = innerBox(box);
        knobInCol(colOf(c, 0, 4), attackKnob, attackLabel);
        knobInCol(colOf(c, 1, 4), decayKnob, decayLabel);
        knobInCol(colOf(c, 2, 4), sustainKnob, sustainLabel);
        knobInCol(colOf(c, 3, 4), releaseKnob, releaseLabel);
    }
}

void AsidEditor::layoutAmpModPage(juce::Rectangle<int> area) {
    const int gap = 10;
    const int rowH = (area.getHeight() - gap) / 2;  // two sections fill the height
    {  // PITCH MODULATION
        auto pm = area.removeFromTop(rowH);
        pitchModGroup.setBounds(pm);
        layoutLfo(pitchLfoUi, innerBox(pm));
    }
    area.removeFromTop(gap);
    {  // PULSE WIDTH MODULATION
        auto pw = area.removeFromTop(rowH);
        pwModGroup.setBounds(pw);
        layoutLfo(pwLfoUi, innerBox(pw));
    }
}

void AsidEditor::layoutSharedPage(juce::Rectangle<int> area) {
    const int gap = 10;
    const int lfoH = 136;  // the LFO stack (knob + toggle below) is a touch taller
    const int sideH = (area.getHeight() - 2 * gap - lfoH) / 2;  // Filter and Master split the rest
    {  // FILTER in three groups: voices (V1/V2/V3) | modes (LP/BP/HP) | Cutoff+Reso.
        auto box = area.removeFromTop(sideH);
        filterGroup.setBounds(box);
        auto c = innerBox(box);
        const int gGap = 34, segW = 46, segGap = 8;
        // Group 1: routing - the three voices plus the external input.
        {
            const int gw = 4 * segW + 3 * segGap;
            auto g = c.removeFromLeft(gw).withSizeKeepingCentre(gw, 30);
            for (auto& b : filtButtons) { b.setBounds(g.removeFromLeft(segW)); g.removeFromLeft(segGap); }
            filtExtButton.setBounds(g.removeFromLeft(segW));
        }
        placeDivider(filtDiv1, c.removeFromLeft(gGap));
        // Group 2: modes (LP/BP/HP).
        {
            const int gw = 3 * segW + 2 * segGap;
            auto g = c.removeFromLeft(gw).withSizeKeepingCentre(gw, 30);
            for (auto& b : modeButtons) { b.setBounds(g.removeFromLeft(segW)); g.removeFromLeft(segGap); }
        }
        placeDivider(filtDiv2, c.removeFromLeft(gGap));
        knobInCol(colOf(c, 0, 2), cutoffKnob, cutoffLabel);  // Group 3: cutoff, reso
        knobInCol(colOf(c, 1, 2), resKnob, resLabel);
    }
    area.removeFromTop(gap);
    {  // CUTOFF MODULATION
        auto cm = area.removeFromTop(lfoH);
        cutModGroup.setBounds(cm);
        layoutLfo(cutLfoUi, innerBox(cm));
    }
    area.removeFromTop(gap);
    {  // MASTER: Volume, Latency and the Voice 3 output toggle, grouped centred.
        auto box = area.removeFromTop(sideH);
        masterGroup.setBounds(box);
        const int knobW = 96, togW = 150, gap = 24, groupW = 2 * knobW + togW + 2 * gap;
        auto g = innerBox(box).withSizeKeepingCentre(groupW, innerBox(box).getHeight());
        knobInCol(g.removeFromLeft(knobW), volumeKnob, volumeLabel); g.removeFromLeft(gap);
        knobInCol(g.removeFromLeft(knobW), latencyKnob, latencyLabel); g.removeFromLeft(gap);
        voice3offButton.setBounds(g.removeFromLeft(togW).withSizeKeepingCentre(togW, 30));
    }
}

void AsidEditor::layoutWavePage(juce::Rectangle<int> area) {
    {  // WAVETABLE config: On, Speed, Length, Loop spread across the width
        auto box = area.removeFromTop(kRowBox);
        wtConfigGroup.setBounds(box);
        auto c = innerBox(box);
        toggleInCol(colOf(c, 0, 4), wtOnButton);
        knobInCol(colOf(c, 1, 4), wtSpeedKnob, wtSpeedLabel);
        knobInCol(colOf(c, 2, 4), wtLengthKnob, wtLengthLabel);
        knobInCol(colOf(c, 3, 4), wtLoopKnob, wtLoopLabel);
    }
    area.removeFromTop(10);
    {  // STEPS: header + 8 rows. Columns: number, 4 waveforms, Sync, Ring, PW, Arp.
        const int steps = AsidProcessor::kWtSteps;
        wtStepsGroup.setBounds(area);  // fill the rest of the page
        auto c = innerBox(area);
        const int numW = 24, gap = 10, waveW = 60, togW = 46, pwW = 96, arpGap = 16, arpW = 88;
        const int gridW = numW + gap + 4 * waveW + 2 * togW + pwW + arpGap + arpW;
        c.removeFromLeft(juce::jmax(0, (c.getWidth() - gridW) / 2));  // centre the grid
        // Column left-edge x for a given row (0..3 waveform, 4 sync, 5 ring, 6 pw, 7 arp).
        auto colX = [&](juce::Rectangle<int> row, int col) {
            int x = row.getX() + numW + gap;
            for (int k = 0; k < col; ++k)
                x += (k < 4 ? waveW : k < 6 ? togW : pwW);  // waveform | sync,ring | pw
            if (col >= 7) x += arpGap;  // small gap before the arp stepper
            return x;
        };
        auto head = c.removeFromTop(16);
        for (int w = 0; w < 4; ++w)
            wtWaveHead[w].setBounds(colX(head, w), head.getY(), waveW, head.getHeight());
        wtSyncHead.setBounds(colX(head, 4), head.getY(), togW, head.getHeight());
        wtRingHead.setBounds(colX(head, 5), head.getY(), togW, head.getHeight());
        wtPwHead.setBounds(colX(head, 6), head.getY(), pwW, head.getHeight());
        wtArpHead.setBounds(colX(head, 7), head.getY(), arpW, head.getHeight());
        c.removeFromTop(2);
        const int rowH = juce::jmax(26, c.getHeight() / steps);  // spread rows over the height
        auto cell = [](int x, int y, int w, int side) {
            return juce::Rectangle<int>(x, y, w, 24).withSizeKeepingCentre(side, 24);
        };
        for (int i = 0; i < steps; ++i) {
            auto full = c.removeFromTop(rowH);
            auto line = full.withSizeKeepingCentre(full.getWidth(), 24);  // 24 band, centred vertically
            const int y = line.getY();
            wtStepInd[i].setBounds(line.getX(), y, numW, 24);
            for (int w = 0; w < 4; ++w) wtWaveTog[i][w].setBounds(cell(colX(line, w), y, waveW, 28));
            wtSyncTog[i].setBounds(cell(colX(line, 4), y, togW, 28));
            wtRingTog[i].setBounds(cell(colX(line, 5), y, togW, 28));
            wtPwKnob[i].setBounds(colX(line, 6) + 4, y, pwW - 8, 24);  // horizontal fader
            // Arp stepper: [-] value [+], segments overlapped 2px for single dividers.
            const int ax = colX(line, 7), bw = 24;
            wtArpDec[i].setBounds(ax, y, bw, 24);
            wtArpInc[i].setBounds(ax + arpW - bw, y, bw, 24);
            wtArpValue[i].setBounds(ax + bw - 2, y, arpW - 2 * bw + 4, 24);
        }
    }
}
