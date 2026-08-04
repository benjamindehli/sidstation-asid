#include "AsidEditor.h"

void AsidEditor::setupKnob(juce::Slider& s, juce::Label& l, const juce::String& name,
                           const juce::String& paramId, std::unique_ptr<SliderAtt>& att) {
    s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 58, 16);
    addAndMakeVisible(s);
    l.setText(name, juce::dontSendNotification);
    l.setJustificationType(juce::Justification::centred);
    l.setFont(juce::Font(juce::FontOptions().withHeight(12.0f)));
    addAndMakeVisible(l);
    att = std::make_unique<SliderAtt>(state, paramId, s);
}

void AsidEditor::setupLfo(LfoControls& u, const juce::String& prefix) {
    addAndMakeVisible(u.enableButton);
    u.enableAtt = std::make_unique<ButtonAtt>(state, prefix + "On", u.enableButton);

    addAndMakeVisible(u.shapeBox);
    for (const char* s : {"Sine", "Triangle", "Saw Up", "Saw Down", "Square", "Sample & Hold", "Random"})
        u.shapeBox.addItem(s, u.shapeBox.getNumItems() + 1);
    u.shapeAtt = std::make_unique<ComboAtt>(state, prefix + "Shape", u.shapeBox);

    addAndMakeVisible(u.syncButton);
    u.syncAtt = std::make_unique<ButtonAtt>(state, prefix + "Sync", u.syncButton);

    addAndMakeVisible(u.divLabel);
    addAndMakeVisible(u.divBox);
    for (const char* d : {"1/1", "1/2", "1/4", "1/4T", "1/8", "1/8T", "1/16", "1/16T"})
        u.divBox.addItem(d, u.divBox.getNumItems() + 1);
    u.divAtt = std::make_unique<ComboAtt>(state, prefix + "Div", u.divBox);

    addAndMakeVisible(u.updateLabel);
    addAndMakeVisible(u.updateBox);
    for (const char* up : {"Eco 25 Hz", "PAL 50 Hz", "NTSC 60 Hz", "Smooth 100 Hz"})
        u.updateBox.addItem(up, u.updateBox.getNumItems() + 1);
    u.updateAtt = std::make_unique<ComboAtt>(state, prefix + "Update", u.updateBox);

    setupKnob(u.rateKnob, u.rateLabel, "Rate Hz", prefix + "Rate", u.rateAtt);
    setupKnob(u.depthKnob, u.depthLabel, "Depth", prefix + "Depth", u.depthAtt);
}

void AsidEditor::layoutLfo(LfoControls& u, juce::Rectangle<int> area) {
    auto row1 = area.removeFromTop(24);
    u.enableButton.setBounds(row1.removeFromLeft(52));
    u.shapeBox.setBounds(row1.removeFromLeft(140));
    row1.removeFromLeft(8);
    u.syncButton.setBounds(row1);

    area.removeFromTop(6);
    auto knobs = area.removeFromTop(78);
    auto rateCell = knobs.removeFromLeft(74);
    u.rateLabel.setBounds(rateCell.removeFromTop(14));
    u.rateKnob.setBounds(rateCell);
    knobs.removeFromLeft(4);
    auto depthCell = knobs.removeFromLeft(74);
    u.depthLabel.setBounds(depthCell.removeFromTop(14));
    u.depthKnob.setBounds(depthCell);
    knobs.removeFromLeft(10);
    auto divRow = knobs.removeFromTop(24);
    u.divLabel.setBounds(divRow.removeFromLeft(40));
    u.divBox.setBounds(divRow);
    knobs.removeFromTop(6);
    auto updRow = knobs.removeFromTop(24);
    u.updateLabel.setBounds(updRow.removeFromLeft(52));
    u.updateBox.setBounds(updRow);
}

AsidEditor::AsidEditor(AsidProcessor& p)
    : juce::AudioProcessorEditor(p), proc(p), state(p.state()) {
    title.setFont(juce::Font(juce::FontOptions().withHeight(20.0f).withStyle("Bold")));
    addAndMakeVisible(title);
    addAndMakeVisible(outLabel);
    addAndMakeVisible(outputBox);
    addAndMakeVisible(refreshButton);
    addAndMakeVisible(voiceLabel);
    addAndMakeVisible(voiceBox);
    auto heading = [this](juce::Label& l) {
        l.setFont(juce::Font(juce::FontOptions().withHeight(13.0f).withStyle("Bold")));
        l.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.65f));
        addAndMakeVisible(l);
    };
    heading(settingsHeading);
    heading(oscHeading);
    heading(ampHeading);
    heading(sharedHeading);
    heading(pitchLfoHeading);
    heading(pwLfoHeading);
    heading(cutLfoHeading);

    addAndMakeVisible(waveLabel);
    addAndMakeVisible(waveformBox);
    addAndMakeVisible(syncButton);
    addAndMakeVisible(ringButton);
    addAndMakeVisible(routeButton);
    addAndMakeVisible(modeLabel);
    addAndMakeVisible(filterModeBox);

    outputBox.onChange = [this] {
        const int id = outputBox.getSelectedId();
        if (id >= 1 && id <= outDevices.size()) {
            proc.midi().openOutputByIdentifier(outDevices[id - 1].identifier);
            proc.requestReinit();  // push the current state to the newly opened device
        }
    };
    refreshButton.onClick = [this] { refreshDevices(); };

    voiceBox.addItem("Voice 1", 1);
    voiceBox.addItem("Voice 2", 2);
    voiceBox.addItem("Voice 3", 3);
    voiceAtt = std::make_unique<ComboAtt>(state, "asidVoice", voiceBox);

    waveformBox.addItem("Triangle", 1);
    waveformBox.addItem("Sawtooth", 2);
    waveformBox.addItem("Pulse", 3);
    waveformBox.addItem("Noise", 4);
    waveformAtt = std::make_unique<ComboAtt>(state, "waveform", waveformBox);

    setupKnob(pwKnob, pwLabel, "Pulse W", "pulseWidth", pwAtt);
    setupKnob(coarseKnob, coarseLabel, "Coarse", "coarse", coarseAtt);
    setupKnob(fineKnob, fineLabel, "Fine", "fine", fineAtt);
    setupKnob(attackKnob, attackLabel, "Attack", "attack", attackAtt);
    setupKnob(decayKnob, decayLabel, "Decay", "decay", decayAtt);
    setupKnob(sustainKnob, sustainLabel, "Sustain", "sustain", sustainAtt);
    setupKnob(releaseKnob, releaseLabel, "Release", "release", releaseAtt);
    setupKnob(cutoffKnob, cutoffLabel, "Cutoff", "cutoff", cutoffAtt);
    setupKnob(resKnob, resLabel, "Reso", "resonance", resAtt);
    setupKnob(volumeKnob, volumeLabel, "Volume", "volume", volumeAtt);
    setupKnob(latencyKnob, latencyLabel, "Lat ms", "latency", latencyAtt);

    syncAtt = std::make_unique<ButtonAtt>(state, "sync", syncButton);
    ringAtt = std::make_unique<ButtonAtt>(state, "ring", ringButton);
    routeAtt = std::make_unique<ButtonAtt>(state, "filterRoute", routeButton);

    filterModeBox.addItem("Low", 1);
    filterModeBox.addItem("Band", 2);
    filterModeBox.addItem("High", 3);
    filterModeAtt = std::make_unique<ComboAtt>(state, "filterMode", filterModeBox);

    setupLfo(pitchLfoUi, "pitchLfo");
    setupLfo(pwLfoUi, "pwLfo");
    setupLfo(cutLfoUi, "cutLfo");

    refreshDevices();
    updateEnablement();
    setSize(720, 620);
    startTimerHz(10);  // drives updateEnablement (waveform / sustain / LFO gating)
}

AsidEditor::~AsidEditor() { stopTimer(); }

void AsidEditor::updateEnablement() {
    auto boolParam = [this](const char* id) {
        auto* p = state.getRawParameterValue(id);
        return p && p->load() > 0.5f;
    };
    auto intParam = [this](const char* id) {
        auto* p = state.getRawParameterValue(id);
        return p ? juce::roundToInt(p->load()) : 0;
    };

    // Pulse width only matters on a pulse wave.
    const bool pulse = intParam("waveform") == 2;
    pwKnob.setEnabled(pulse);
    pwLabel.setEnabled(pulse);

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
        u.updateBox.setEnabled(on);
        u.updateLabel.setEnabled(on);
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

void AsidEditor::timerCallback() { updateEnablement(); }

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
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
    // A faint rule under each section heading.
    g.setColour(juce::Colours::white.withAlpha(0.15f));
    for (const auto* l : {&settingsHeading, &oscHeading, &ampHeading, &sharedHeading,
                          &pitchLfoHeading, &pwLfoHeading, &cutLfoHeading}) {
        auto b = l->getBounds();
        g.fillRect(b.getX(), b.getBottom() - 1, b.getWidth(), 1);
    }
}

void AsidEditor::resized() {
    auto knobCell = [](juce::Rectangle<int>& row, juce::Slider& s, juce::Label& l) {
        auto cell = row.removeFromLeft(74);
        l.setBounds(cell.removeFromTop(14));
        s.setBounds(cell);
        row.removeFromLeft(4);
    };
    auto sectionHeading = [](juce::Rectangle<int>& col, juce::Label& h) {
        col.removeFromTop(12);
        h.setBounds(col.removeFromTop(18));
        col.removeFromTop(6);
    };

    auto area = getLocalBounds().reduced(14);
    title.setBounds(area.removeFromTop(30));
    area.removeFromTop(4);

    auto left = area.removeFromLeft(area.getWidth() / 2 - 8);
    area.removeFromLeft(16);
    auto right = area;

    // Left column: Settings, Oscillator, Pitch Mod, PW Mod.
    sectionHeading(left, settingsHeading);
    auto midiRow = left.removeFromTop(26);
    outLabel.setBounds(midiRow.removeFromLeft(64));
    refreshButton.setBounds(midiRow.removeFromRight(70));
    midiRow.removeFromRight(6);
    outputBox.setBounds(midiRow);
    left.removeFromTop(6);
    auto voiceRow = left.removeFromTop(26);
    voiceLabel.setBounds(voiceRow.removeFromLeft(64));
    voiceBox.setBounds(voiceRow.removeFromLeft(120));

    sectionHeading(left, oscHeading);
    auto waveRow = left.removeFromTop(26);
    waveLabel.setBounds(waveRow.removeFromLeft(70));
    waveformBox.setBounds(waveRow.removeFromLeft(108));
    waveRow.removeFromLeft(8);
    syncButton.setBounds(waveRow.removeFromLeft(64));
    ringButton.setBounds(waveRow.removeFromLeft(60));
    left.removeFromTop(6);
    auto oscKnobs = left.removeFromTop(84);
    knobCell(oscKnobs, pwKnob, pwLabel);
    knobCell(oscKnobs, coarseKnob, coarseLabel);
    knobCell(oscKnobs, fineKnob, fineLabel);

    sectionHeading(left, pitchLfoHeading);
    layoutLfo(pitchLfoUi, left.removeFromTop(108));

    sectionHeading(left, pwLfoHeading);
    layoutLfo(pwLfoUi, left.removeFromTop(108));

    // Right column: Amp, Shared, Cutoff Mod.
    sectionHeading(right, ampHeading);
    auto ampKnobs = right.removeFromTop(84);
    knobCell(ampKnobs, attackKnob, attackLabel);
    knobCell(ampKnobs, decayKnob, decayLabel);
    knobCell(ampKnobs, sustainKnob, sustainLabel);
    knobCell(ampKnobs, releaseKnob, releaseLabel);

    sectionHeading(right, sharedHeading);
    auto filtRow = right.removeFromTop(26);
    routeButton.setBounds(filtRow.removeFromLeft(150));
    filtRow.removeFromLeft(8);
    modeLabel.setBounds(filtRow.removeFromLeft(44));
    filterModeBox.setBounds(filtRow.removeFromLeft(100));
    right.removeFromTop(6);
    auto sharedKnobs = right.removeFromTop(84);
    knobCell(sharedKnobs, cutoffKnob, cutoffLabel);
    knobCell(sharedKnobs, resKnob, resLabel);
    knobCell(sharedKnobs, volumeKnob, volumeLabel);
    knobCell(sharedKnobs, latencyKnob, latencyLabel);

    sectionHeading(right, cutLfoHeading);
    layoutLfo(cutLfoUi, right.removeFromTop(108));
}
