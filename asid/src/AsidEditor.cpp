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
    addAndMakeVisible(waveLabel);
    addAndMakeVisible(waveformBox);
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
    setupKnob(cutoffKnob, cutoffLabel, "Cutoff", "cutoff", cutoffAtt);
    setupKnob(resKnob, resLabel, "Reso", "resonance", resAtt);

    routeAtt = std::make_unique<ButtonAtt>(state, "filterRoute", routeButton);

    filterModeBox.addItem("Low", 1);
    filterModeBox.addItem("Band", 2);
    filterModeBox.addItem("High", 3);
    filterModeAtt = std::make_unique<ComboAtt>(state, "filterMode", filterModeBox);

    refreshDevices();
    setSize(560, 470);
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
    g.setColour(juce::Colours::white.withAlpha(0.6f));
    g.setFont(juce::Font(juce::FontOptions().withHeight(13.0f).withStyle("Bold")));
    g.drawText("VOICE", 14, 150, 200, 18, juce::Justification::centredLeft);
    g.drawText("FILTER", 14, 320, 200, 18, juce::Justification::centredLeft);
}

void AsidEditor::resized() {
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

    // Voice section.
    r.removeFromTop(28);  // leave room for the "VOICE" heading drawn in paint()
    auto waveRow = r.removeFromTop(26);
    waveLabel.setBounds(waveRow.removeFromLeft(74));
    waveformBox.setBounds(waveRow.removeFromLeft(130));

    r.removeFromTop(6);
    auto knobs = r.removeFromTop(84);
    auto knobCell = [&](juce::Rectangle<int>& row, juce::Slider& s, juce::Label& l) {
        auto cell = row.removeFromLeft(72);
        l.setBounds(cell.removeFromTop(14));
        s.setBounds(cell);
        row.removeFromLeft(4);
    };
    knobCell(knobs, attackKnob, attackLabel);
    knobCell(knobs, decayKnob, decayLabel);
    knobCell(knobs, sustainKnob, sustainLabel);
    knobCell(knobs, releaseKnob, releaseLabel);
    knobCell(knobs, pwKnob, pwLabel);

    // Filter section.
    r.removeFromTop(24);  // "FILTER" heading
    auto filterRow = r.removeFromTop(84);
    knobCell(filterRow, cutoffKnob, cutoffLabel);
    knobCell(filterRow, resKnob, resLabel);
    auto rest = filterRow;
    routeButton.setBounds(rest.removeFromTop(24));
    rest.removeFromTop(6);
    auto modeRow = rest.removeFromTop(24);
    modeLabel.setBounds(modeRow.removeFromLeft(46));
    filterModeBox.setBounds(modeRow.removeFromLeft(110));
}
