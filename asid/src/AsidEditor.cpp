#include "AsidEditor.h"

namespace {
constexpr int kRowBox = 114;  // a full-width box holding one inline row (22 title + 84 + 8)

void knobCell(juce::Rectangle<int>& row, juce::Slider& s, juce::Label& l) {
    auto cell = row.removeFromLeft(74);
    l.setBounds(cell.removeFromTop(14));
    s.setBounds(cell);
    row.removeFromLeft(6);
}
// The content area inside a titled box: sides in, and below the title.
juce::Rectangle<int> innerBox(juce::Rectangle<int> box) {
    return box.reduced(10, 0).withTrimmedTop(22).withTrimmedBottom(8);
}
// A label stacked above its control, within a cell. Label 14, control 24.
void labeledOver(juce::Rectangle<int> cell, juce::Label& l, juce::Component& c) {
    l.setBounds(cell.removeFromTop(14));
    c.setBounds(cell.removeFromTop(24));
}
// A toggle placed inline, aligned with the control row of labels-over neighbours.
void toggleInline(juce::Rectangle<int> cell, juce::ToggleButton& b) {
    b.setBounds(cell.getX(), cell.getY() + 14, cell.getWidth(), 24);
}
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

void AsidEditor::setupLfo(juce::Component& parent, LfoControls& u, const juce::String& prefix) {
    parent.addAndMakeVisible(u.enableButton);
    u.enableAtt = std::make_unique<ButtonAtt>(state, prefix + "On", u.enableButton);

    u.shapeLabel.setText("Shape", juce::dontSendNotification);
    parent.addAndMakeVisible(u.shapeLabel);
    parent.addAndMakeVisible(u.shapeBox);
    for (const char* s : {"Sine", "Triangle", "Saw Up", "Saw Down", "Square", "Sample & Hold", "Random"})
        u.shapeBox.addItem(s, u.shapeBox.getNumItems() + 1);
    u.shapeAtt = std::make_unique<ComboAtt>(state, prefix + "Shape", u.shapeBox);

    parent.addAndMakeVisible(u.syncButton);
    u.syncAtt = std::make_unique<ButtonAtt>(state, prefix + "Sync", u.syncButton);

    parent.addAndMakeVisible(u.divLabel);
    parent.addAndMakeVisible(u.divBox);
    for (const char* d : {"1/1", "1/2", "1/4", "1/4T", "1/8", "1/8T", "1/16", "1/16T"})
        u.divBox.addItem(d, u.divBox.getNumItems() + 1);
    u.divAtt = std::make_unique<ComboAtt>(state, prefix + "Div", u.divBox);

    setupKnob(parent, u.rateKnob, u.rateLabel, "Rate Hz", prefix + "Rate", u.rateAtt);
    // The attachment sets the slider's text from the parameter, overriding
    // setNumDecimalPlacesToDisplay, so format here (after the attachment) instead.
    u.rateKnob.textFromValueFunction = [](double v) { return juce::String(v, 3); };
    u.rateKnob.updateText();
    setupKnob(parent, u.depthKnob, u.depthLabel, "Depth", prefix + "Depth", u.depthAtt);
}

void AsidEditor::layoutLfo(LfoControls& u, juce::Rectangle<int> area) {
    // One wide inline row: on, shape, sync, rate, depth, division.
    auto next = [&area](int w) { auto c = area.removeFromLeft(w); area.removeFromLeft(8); return c; };
    toggleInline(next(46), u.enableButton);
    labeledOver(next(116), u.shapeLabel, u.shapeBox);
    toggleInline(next(104), u.syncButton);
    { auto k = next(74); u.rateLabel.setBounds(k.removeFromTop(14)); u.rateKnob.setBounds(k); }
    { auto k = next(74); u.depthLabel.setBounds(k.removeFromTop(14)); u.depthKnob.setBounds(k); }
    labeledOver(next(64), u.divLabel, u.divBox);
}

AsidEditor::AsidEditor(AsidProcessor& p)
    : juce::AudioProcessorEditor(p), proc(p), state(p.state()) {
    setLookAndFeel(&laf);
    title.setFont(SidLookAndFeel::mono(18.0f, true));
    title.setColour(juce::Label::textColourId, juce::Colour(SidLookAndFeel::kHot));
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
    addAndMakeVisible(voiceLabel);
    addAndMakeVisible(voiceBox);
    addAndMakeVisible(midiLoadLabel);
    addAndMakeVisible(modRateLabel);
    addAndMakeVisible(modRateBox);
    for (const char* r : {"Eco 25 Hz", "PAL 50 Hz", "NTSC 60 Hz", "Smooth 100 Hz"})
        modRateBox.addItem(r, modRateBox.getNumItems() + 1);
    modRateAtt = std::make_unique<ComboAtt>(state, "modRate", modRateBox);

    // ---- OSC page: Oscillator, Glide ----
    oscPage.addAndMakeVisible(waveLabel);
    for (auto* b : {&waveTriButton, &waveSawButton, &wavePulseButton, &waveNoiseButton})
        oscPage.addAndMakeVisible(*b);
    oscPage.addAndMakeVisible(syncButton);
    oscPage.addAndMakeVisible(ringButton);

    outputBox.onChange = [this] {
        const int id = outputBox.getSelectedId();
        if (id >= 1 && id <= outDevices.size()) {
            proc.midi().openOutputByIdentifier(outDevices[id - 1].identifier);
            // Shared output: make every instance re-push its voice to the new device.
            AsidShared::get().outGeneration.fetch_add(1);
        }
    };
    refreshButton.onClick = [this] { refreshDevices(); };

    voiceBox.addItem("Voice 1", 1);
    voiceBox.addItem("Voice 2", 2);
    voiceBox.addItem("Voice 3", 3);
    voiceAtt = std::make_unique<ComboAtt>(state, "asidVoice", voiceBox);

    waveTriAtt = std::make_unique<ButtonAtt>(state, "waveTri", waveTriButton);
    waveSawAtt = std::make_unique<ButtonAtt>(state, "waveSaw", waveSawButton);
    wavePulseAtt = std::make_unique<ButtonAtt>(state, "wavePulse", wavePulseButton);
    waveNoiseAtt = std::make_unique<ButtonAtt>(state, "waveNoise", waveNoiseButton);

    setupKnob(oscPage, pwKnob, pwLabel, "Pulse W", "pulseWidth", pwAtt);
    setupKnob(oscPage, coarseKnob, coarseLabel, "Coarse", "coarse", coarseAtt);
    setupKnob(oscPage, fineKnob, fineLabel, "Fine", "fine", fineAtt);
    syncAtt = std::make_unique<ButtonAtt>(state, "sync", syncButton);
    ringAtt = std::make_unique<ButtonAtt>(state, "ring", ringButton);

    setupKnob(oscPage, portaKnob, portaLabel, "Glide time", "portaTime", portaAtt);
    oscPage.addAndMakeVisible(portaTrigLabel);
    oscPage.addAndMakeVisible(portaTrigBox);
    portaTrigBox.addItem("Legato", 1);
    portaTrigBox.addItem("Always", 2);
    portaTrigAtt = std::make_unique<ComboAtt>(state, "portaTrigger", portaTrigBox);
    oscPage.addAndMakeVisible(portaTypeLabel);
    oscPage.addAndMakeVisible(portaTypeBox);
    portaTypeBox.addItem("Smooth", 1);
    portaTypeBox.addItem("Stepped", 2);
    portaTypeAtt = std::make_unique<ComboAtt>(state, "portaType", portaTypeBox);

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
    sharedPage.addAndMakeVisible(filterActiveLabel);
    const char* voiceNames[3] = {"1", "2", "3"};
    const char* filtIds[3] = {"filt1", "filt2", "filt3"};
    for (int i = 0; i < 3; ++i) {
        filtButtons[i].setButtonText(voiceNames[i]);
        sharedPage.addAndMakeVisible(filtButtons[i]);
        filtAtts[i] = std::make_unique<ButtonAtt>(state, filtIds[i], filtButtons[i]);
    }
    sharedPage.addAndMakeVisible(filterModeLabel);
    const char* modeNames[3] = {"LP", "BP", "HP"};
    const char* modeIds[3] = {"modeLP", "modeBP", "modeHP"};
    for (int i = 0; i < 3; ++i) {
        modeButtons[i].setButtonText(modeNames[i]);
        sharedPage.addAndMakeVisible(modeButtons[i]);
        modeAtts[i] = std::make_unique<ButtonAtt>(state, modeIds[i], modeButtons[i]);
    }
    setupKnob(sharedPage, cutoffKnob, cutoffLabel, "Cutoff", "cutoff", cutoffAtt);
    setupKnob(sharedPage, resKnob, resLabel, "Reso", "resonance", resAtt);
    setupLfo(sharedPage, cutLfoUi, "cutLfo");
    setupKnob(sharedPage, volumeKnob, volumeLabel, "Volume", "volume", volumeAtt);
    setupKnob(sharedPage, latencyKnob, latencyLabel, "Lat ms", "latency", latencyAtt);

    // ---- WAVE page: wavetable config and the per-step rows ----
    wtPage.addAndMakeVisible(wtOnButton);
    wtOnAtt = std::make_unique<ButtonAtt>(state, "wtOn", wtOnButton);
    setupKnob(wtPage, wtSpeedKnob, wtSpeedLabel, "Speed", "wtSpeed", wtSpeedAtt);
    setupKnob(wtPage, wtLengthKnob, wtLengthLabel, "Length", "wtLength", wtLengthAtt);
    setupKnob(wtPage, wtLoopKnob, wtLoopLabel, "Loop", "wtLoop", wtLoopAtt);
    const char* wtHeads[4] = {"Tri", "Saw", "Pulse", "Noise"};
    const char* wtIds[4] = {"wtTri", "wtSaw", "wtPulse", "wtNoise"};
    for (int w = 0; w < 4; ++w) {
        wtWaveHead[w].setText(wtHeads[w], juce::dontSendNotification);
        wtWaveHead[w].setFont(juce::Font(juce::FontOptions().withHeight(12.0f)));
        wtPage.addAndMakeVisible(wtWaveHead[w]);
    }
    for (int i = 0; i < AsidProcessor::kWtSteps; ++i) {
        wtStepNum[i].setText(juce::String(i + 1), juce::dontSendNotification);
        wtStepNum[i].setJustificationType(juce::Justification::centredRight);
        wtPage.addAndMakeVisible(wtStepNum[i]);
        for (int w = 0; w < 4; ++w) {
            wtPage.addAndMakeVisible(wtWaveTog[i][w]);
            wtWaveTogAtt[i][w] = std::make_unique<ButtonAtt>(
                state, juce::String(wtIds[w]) + juce::String(i), wtWaveTog[i][w]);
        }
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

AsidEditor::~AsidEditor() { stopTimer(); setLookAndFeel(nullptr); }

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

    // Glide trigger and type only matter when portamento time is up.
    const bool porta = intParam("portaTime") > 0;
    portaTrigBox.setEnabled(porta);
    portaTrigLabel.setEnabled(porta);
    portaTypeBox.setEnabled(porta);
    portaTypeLabel.setEnabled(porta);

    // The wavetable's config and steps are live only when it is on.
    const bool wtOn = boolParam("wtOn");
    for (auto* s : {&wtSpeedKnob, &wtLengthKnob, &wtLoopKnob})
        s->setEnabled(wtOn);
    for (auto& h : wtWaveHead) h.setEnabled(wtOn);
    for (int i = 0; i < AsidProcessor::kWtSteps; ++i) {
        // Noise is exclusive per step, same as the oscillator: grey the other three.
        const bool stepNoise = intParam((juce::String("wtNoise") + juce::String(i)).toRawUTF8()) != 0;
        for (int w = 0; w < 4; ++w)
            wtWaveTog[i][w].setEnabled(wtOn && (w == 3 || !stepNoise));
        wtArpValue[i].setEnabled(wtOn);
        wtArpDec[i].setEnabled(wtOn);
        wtArpInc[i].setEnabled(wtOn);
        wtStepNum[i].setEnabled(wtOn);
    }

    // The filter checkboxes are shared across instances; mark this instance's own
    // voice so you can see which of the three it drives. Only touch it on change.
    const int myVoice = juce::jlimit(0, 2, intParam("asidVoice"));
    if (myVoice != highlightedVoice) {
        highlightedVoice = myVoice;
        for (int i = 0; i < 3; ++i) {
            filtButtons[i].getProperties().set("sidHighlight", i == myVoice);
            filtButtons[i].repaint();
        }
    }

    // Decay is inaudible at full sustain and only feeds the ADSR bug there, so
    // disable it and pin it to 0 when sustain is 15.
    const bool sustainMax = intParam("sustain") == 15;
    decayKnob.setEnabled(!sustainMax);
    decayLabel.setEnabled(!sustainMax);
    if (sustainMax && decayKnob.getValue() != 0.0)
        decayKnob.setValue(0.0, juce::sendNotificationSync);

    // Grey each LFO's controls when it is off, and show the rate as either the
    // free Hz knob or the sync division, per its Tempo Sync toggle.
    auto applyLfo = [](LfoControls& u, bool on, bool sync) {
        u.shapeBox.setEnabled(on);
        u.syncButton.setEnabled(on);
        u.depthKnob.setEnabled(on);
        u.depthLabel.setEnabled(on);
        u.rateKnob.setEnabled(on && !sync);
        u.rateLabel.setEnabled(on && !sync);
        u.divBox.setEnabled(on && sync);
        u.divLabel.setEnabled(on && sync);
    };
    applyLfo(pitchLfoUi, boolParam("pitchLfoOn"), boolParam("pitchLfoSync"));
    // The PW LFO only works on a pulse wave, so its On toggle greys out too.
    pwLfoUi.enableButton.setEnabled(pulse);
    applyLfo(pwLfoUi, pulse && boolParam("pwLfoOn"), boolParam("pwLfoSync"));
    applyLfo(cutLfoUi, boolParam("cutLfoOn"), boolParam("cutLfoSync"));
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
    // C64 screen: a light-blue border framing the darker screen. The section
    // boxes and the tab bar draw their own frames.
    g.fillAll(juce::Colour(SidLookAndFeel::kFg));
    g.setColour(juce::Colour(SidLookAndFeel::kBg));
    g.fillRect(getLocalBounds().reduced(kBorder));

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

void AsidEditor::resized() {
    auto area = getLocalBounds().reduced(kBorder + 6);  // inside the C64 border
    title.setBounds(area.removeFromTop(26));
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
    auto voice = header.removeFromLeft(108);
    voiceLabel.setBounds(voice.removeFromTop(14));
    voiceBox.setBounds(voice.removeFromTop(24));
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
    {  // OSCILLATOR (one wide row)
        auto box = area.removeFromTop(kRowBox);
        oscGroup.setBounds(box);
        auto c = innerBox(box);
        {  // Waveform: label over a 2x2 grid of combinable toggles.
            auto cell = c.removeFromLeft(150);
            waveLabel.setBounds(cell.removeFromTop(14));
            auto r1 = cell.removeFromTop(24);
            waveTriButton.setBounds(r1.removeFromLeft(75));
            waveSawButton.setBounds(r1.removeFromLeft(75));
            cell.removeFromTop(6);  // gap so the full-height boxes don't touch
            auto r2 = cell.removeFromTop(24);
            wavePulseButton.setBounds(r2.removeFromLeft(75));
            waveNoiseButton.setBounds(r2.removeFromLeft(75));
        }
        c.removeFromLeft(14);
        toggleInline(c.removeFromLeft(64), syncButton);
        c.removeFromLeft(6);
        toggleInline(c.removeFromLeft(60), ringButton);
        c.removeFromLeft(16);
        knobCell(c, pwKnob, pwLabel);
    }
    area.removeFromTop(10);
    {  // TUNING (one wide row): coarse, fine, glide time, glide trigger/type
        auto box = area.removeFromTop(kRowBox);
        glideGroup.setBounds(box);
        auto c = innerBox(box);
        knobCell(c, coarseKnob, coarseLabel);
        knobCell(c, fineKnob, fineLabel);
        knobCell(c, portaKnob, portaLabel);
        c.removeFromLeft(14);
        labeledOver(c.removeFromLeft(120), portaTrigLabel, portaTrigBox);
        c.removeFromLeft(14);
        labeledOver(c.removeFromLeft(120), portaTypeLabel, portaTypeBox);
    }
    area.removeFromTop(10);
    {  // AMP ENVELOPE (one wide row)
        auto box = area.removeFromTop(kRowBox);
        ampGroup.setBounds(box);
        auto c = innerBox(box);
        knobCell(c, attackKnob, attackLabel);
        knobCell(c, decayKnob, decayLabel);
        knobCell(c, sustainKnob, sustainLabel);
        knobCell(c, releaseKnob, releaseLabel);
    }
}

void AsidEditor::layoutAmpModPage(juce::Rectangle<int> area) {
    {  // PITCH MODULATION
        auto pm = area.removeFromTop(kRowBox);
        pitchModGroup.setBounds(pm);
        layoutLfo(pitchLfoUi, innerBox(pm));
    }
    area.removeFromTop(10);
    {  // PULSE WIDTH MODULATION
        auto pw = area.removeFromTop(kRowBox);
        pwModGroup.setBounds(pw);
        layoutLfo(pwLfoUi, innerBox(pw));
    }
}

void AsidEditor::layoutSharedPage(juce::Rectangle<int> area) {
    {  // FILTER (one wide row): active, mode, cutoff, resonance
        auto box = area.removeFromTop(kRowBox);
        filterGroup.setBounds(box);
        auto c = innerBox(box);
        auto activeCell = c.removeFromLeft(150);
        filterActiveLabel.setBounds(activeCell.removeFromTop(14));
        auto activeRow = activeCell.removeFromTop(24);
        for (auto& b : filtButtons) { b.setBounds(activeRow.removeFromLeft(42)); activeRow.removeFromLeft(6); }
        c.removeFromLeft(10);
        auto modeCell = c.removeFromLeft(196);
        filterModeLabel.setBounds(modeCell.removeFromTop(14));
        auto modeRow = modeCell.removeFromTop(24);
        for (auto& b : modeButtons) { b.setBounds(modeRow.removeFromLeft(56)); modeRow.removeFromLeft(6); }
        c.removeFromLeft(16);
        knobCell(c, cutoffKnob, cutoffLabel);
        knobCell(c, resKnob, resLabel);
    }
    area.removeFromTop(10);
    {  // CUTOFF MOD (its own box now)
        auto cm = area.removeFromTop(kRowBox);
        cutModGroup.setBounds(cm);
        layoutLfo(cutLfoUi, innerBox(cm));
    }
    area.removeFromTop(10);
    {  // MASTER (one wide row): volume, latency
        auto box = area.removeFromTop(kRowBox);
        masterGroup.setBounds(box);
        auto c = innerBox(box);
        knobCell(c, volumeKnob, volumeLabel);
        knobCell(c, latencyKnob, latencyLabel);
    }
}

void AsidEditor::layoutWavePage(juce::Rectangle<int> area) {
    {  // WAVETABLE config: On, Speed, Length, Loop
        auto box = area.removeFromTop(kRowBox);
        wtConfigGroup.setBounds(box);
        auto c = innerBox(box);
        toggleInline(c.removeFromLeft(56), wtOnButton);
        c.removeFromLeft(12);
        knobCell(c, wtSpeedKnob, wtSpeedLabel);
        knobCell(c, wtLengthKnob, wtLengthLabel);
        knobCell(c, wtLoopKnob, wtLoopLabel);
    }
    area.removeFromTop(10);
    {  // STEPS: a column-header row, then one row per step (number, four waveform
       // toggles under the headers, arpeggio).
        const int steps = AsidProcessor::kWtSteps;
        auto box = area.removeFromTop(22 + 18 + steps * 26 + 8);
        wtStepsGroup.setBounds(box);
        auto c = innerBox(box);
        const int numW = 22, gap = 8, colW = 46, arpGap = 12, arpW = 88;  // 24 + 40 value + 24
        // Left edge of waveform column w (0..3), or the arp column (w == 4), so the
        // headers and every step row share one set of x positions.
        auto colX = [&](juce::Rectangle<int> row, int w) {
            return row.getX() + numW + gap + w * colW + (w == 4 ? arpGap : 0);
        };
        auto head = c.removeFromTop(16);
        for (int w = 0; w < 4; ++w)
            wtWaveHead[w].setBounds(colX(head, w), head.getY(), colW, head.getHeight());
        c.removeFromTop(2);
        for (int i = 0; i < steps; ++i) {
            auto row = c.removeFromTop(24);
            wtStepNum[i].setBounds(row.getX(), row.getY(), numW, row.getHeight());
            for (int w = 0; w < 4; ++w)
                wtWaveTog[i][w].setBounds(colX(row, w), row.getY(), colW, row.getHeight());
            // Arp stepper: [-] value [+], square buttons matching the field height.
            auto arp = juce::Rectangle<int>(colX(row, 4), row.getY(), arpW, row.getHeight());
            const int bw = row.getHeight();  // square buttons
            wtArpDec[i].setBounds(arp.removeFromLeft(bw));
            wtArpInc[i].setBounds(arp.removeFromRight(bw));
            wtArpValue[i].setBounds(arp);
            c.removeFromTop(2);
        }
    }
}
