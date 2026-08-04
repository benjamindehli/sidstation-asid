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
    heading(voiceHeading);
    heading(sharedHeading);
    heading(lfoHeading);

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

    setupKnob(attackKnob, attackLabel, "Attack", "attack", attackAtt);
    setupKnob(decayKnob, decayLabel, "Decay", "decay", decayAtt);
    setupKnob(sustainKnob, sustainLabel, "Sustain", "sustain", sustainAtt);
    setupKnob(releaseKnob, releaseLabel, "Release", "release", releaseAtt);
    setupKnob(pwKnob, pwLabel, "Pulse W", "pulseWidth", pwAtt);
    setupKnob(restartKnob, restartLabel, "Restart", "hardRestart", restartAtt);
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

    addAndMakeVisible(lfoTargetLabel);
    addAndMakeVisible(lfoTargetBox);
    for (const char* t : {"Off", "Pulse Width", "Pitch", "Cutoff"})
        lfoTargetBox.addItem(t, lfoTargetBox.getNumItems() + 1);
    lfoTargetAtt = std::make_unique<ComboAtt>(state, "lfoTarget", lfoTargetBox);

    addAndMakeVisible(lfoShapeLabel);
    addAndMakeVisible(lfoShapeBox);
    for (const char* s : {"Sine", "Triangle", "Saw Up", "Saw Down", "Square", "Sample & Hold", "Random"})
        lfoShapeBox.addItem(s, lfoShapeBox.getNumItems() + 1);
    lfoShapeAtt = std::make_unique<ComboAtt>(state, "lfoShape", lfoShapeBox);

    addAndMakeVisible(lfoUpdateLabel);
    addAndMakeVisible(lfoUpdateBox);
    for (const char* u : {"Eco 25 Hz", "PAL 50 Hz", "NTSC 60 Hz", "Smooth 100 Hz"})
        lfoUpdateBox.addItem(u, lfoUpdateBox.getNumItems() + 1);
    lfoUpdateAtt = std::make_unique<ComboAtt>(state, "lfoUpdate", lfoUpdateBox);

    addAndMakeVisible(lfoSyncButton);
    lfoSyncAtt = std::make_unique<ButtonAtt>(state, "lfoSync", lfoSyncButton);

    addAndMakeVisible(lfoDivLabel);
    addAndMakeVisible(lfoDivBox);
    for (const char* d : {"1/1", "1/2", "1/4", "1/4T", "1/8", "1/8T", "1/16", "1/16T"})
        lfoDivBox.addItem(d, lfoDivBox.getNumItems() + 1);
    lfoDivAtt = std::make_unique<ComboAtt>(state, "lfoDivision", lfoDivBox);

    setupKnob(lfoRateKnob, lfoRateLabel, "Rate Hz", "lfoRate", lfoRateAtt);
    setupKnob(lfoDepthKnob, lfoDepthLabel, "Depth", "lfoDepth", lfoDepthAtt);

    diagLabel.setFont(juce::Font(juce::FontOptions().withHeight(11.0f)));
    diagLabel.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.55f));
    diagLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(diagLabel);

    refreshDevices();
    updateEnablement();
    setSize(560, 610);
    startTimerHz(10);
}

AsidEditor::~AsidEditor() { stopTimer(); }

void AsidEditor::updateEnablement() {
    // Pulse width only matters on a pulse wave. The LFO stays enabled since it
    // can also target pitch and cutoff.
    bool pulse = false;
    if (auto* p = state.getRawParameterValue("waveform"))
        pulse = juce::roundToInt(p->load()) == 2;  // Pulse
    pwKnob.setEnabled(pulse);
    pwLabel.setEnabled(pulse);
}

void AsidEditor::timerCallback() {
    updateEnablement();
    const auto d = proc.diag();
    diagLabel.setText(juce::String(d.playing ? "running" : "stopped") + "   play: "
                          + juce::String(d.playheadSec, 2) + " s   align delay: "
                          + juce::String(juce::roundToInt(d.alignMs)) + " ms",
                      juce::dontSendNotification);
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
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
    // A faint rule under each section heading.
    g.setColour(juce::Colours::white.withAlpha(0.15f));
    for (const auto* l : {&voiceHeading, &sharedHeading}) {
        auto b = l->getBounds();
        g.fillRect(b.getX(), b.getBottom() - 1, getWidth() - b.getX() - 14, 1);
    }
}

void AsidEditor::resized() {
    auto knobCell = [](juce::Rectangle<int>& row, juce::Slider& s, juce::Label& l) {
        auto cell = row.removeFromLeft(74);
        l.setBounds(cell.removeFromTop(14));
        s.setBounds(cell);
        row.removeFromLeft(4);
    };

    auto r = getLocalBounds().reduced(14);
    title.setBounds(r.removeFromTop(30));
    r.removeFromTop(6);

    auto row1 = r.removeFromTop(26);
    outLabel.setBounds(row1.removeFromLeft(66));
    refreshButton.setBounds(row1.removeFromRight(78));
    row1.removeFromRight(8);
    outputBox.setBounds(row1);

    r.removeFromTop(8);
    auto row2 = r.removeFromTop(26);
    voiceLabel.setBounds(row2.removeFromLeft(66));
    voiceBox.setBounds(row2.removeFromLeft(130));

    // Per-voice section.
    r.removeFromTop(14);
    voiceHeading.setBounds(r.removeFromTop(18));
    r.removeFromTop(6);
    auto waveRow = r.removeFromTop(26);
    waveLabel.setBounds(waveRow.removeFromLeft(74));
    waveformBox.setBounds(waveRow.removeFromLeft(128));
    waveRow.removeFromLeft(16);
    syncButton.setBounds(waveRow.removeFromLeft(72));
    ringButton.setBounds(waveRow.removeFromLeft(72));

    r.removeFromTop(6);
    auto voiceKnobs = r.removeFromTop(84);
    knobCell(voiceKnobs, attackKnob, attackLabel);
    knobCell(voiceKnobs, decayKnob, decayLabel);
    knobCell(voiceKnobs, sustainKnob, sustainLabel);
    knobCell(voiceKnobs, releaseKnob, releaseLabel);
    knobCell(voiceKnobs, pwKnob, pwLabel);
    knobCell(voiceKnobs, restartKnob, restartLabel);

    r.removeFromTop(4);
    routeButton.setBounds(r.removeFromTop(24).removeFromLeft(180));

    // Shared section.
    r.removeFromTop(14);
    sharedHeading.setBounds(r.removeFromTop(18));
    r.removeFromTop(6);
    auto sharedKnobs = r.removeFromTop(84);
    knobCell(sharedKnobs, cutoffKnob, cutoffLabel);
    knobCell(sharedKnobs, resKnob, resLabel);
    knobCell(sharedKnobs, volumeKnob, volumeLabel);
    knobCell(sharedKnobs, latencyKnob, latencyLabel);
    sharedKnobs.removeFromLeft(12);
    auto modeCol = sharedKnobs.removeFromLeft(120);
    modeLabel.setBounds(modeCol.removeFromTop(16));
    filterModeBox.setBounds(modeCol.removeFromTop(26));

    // LFO section.
    r.removeFromTop(14);
    lfoHeading.setBounds(r.removeFromTop(18));
    r.removeFromTop(6);
    auto lfoRow = r.removeFromTop(26);
    lfoTargetLabel.setBounds(lfoRow.removeFromLeft(52));
    lfoTargetBox.setBounds(lfoRow.removeFromLeft(110));
    lfoRow.removeFromLeft(12);
    lfoShapeLabel.setBounds(lfoRow.removeFromLeft(48));
    lfoShapeBox.setBounds(lfoRow.removeFromLeft(110));
    lfoRow.removeFromLeft(12);
    lfoSyncButton.setBounds(lfoRow.removeFromLeft(120));

    r.removeFromTop(6);
    auto lfoKnobs = r.removeFromTop(84);
    knobCell(lfoKnobs, lfoRateKnob, lfoRateLabel);
    knobCell(lfoKnobs, lfoDepthKnob, lfoDepthLabel);
    lfoKnobs.removeFromLeft(12);
    auto rightCol = lfoKnobs;
    auto divRow = rightCol.removeFromTop(26);
    lfoDivLabel.setBounds(divRow.removeFromLeft(52));
    lfoDivBox.setBounds(divRow.removeFromLeft(96));
    rightCol.removeFromTop(6);
    auto updRow = rightCol.removeFromTop(26);
    lfoUpdateLabel.setBounds(updRow.removeFromLeft(52));
    lfoUpdateBox.setBounds(updRow.removeFromLeft(120));

    r.removeFromTop(8);
    diagLabel.setBounds(r.removeFromTop(16));
}
