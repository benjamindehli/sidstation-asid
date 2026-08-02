#include "AsidEditor.h"

AsidEditor::AsidEditor(AsidProcessor& p) : juce::AudioProcessorEditor(p), proc(p) {
    title.setFont(juce::Font(juce::FontOptions().withHeight(20.0f).withStyle("Bold")));
    addAndMakeVisible(title);
    addAndMakeVisible(outLabel);
    addAndMakeVisible(outputBox);
    addAndMakeVisible(refreshButton);
    addAndMakeVisible(voiceLabel);
    addAndMakeVisible(voiceBox);
    addAndMakeVisible(asidButton);
    addAndMakeVisible(help);

    outputBox.onChange = [this] {
        const int id = outputBox.getSelectedId();
        if (id >= 1 && id <= outDevices.size())
            proc.midi().openOutputByIdentifier(outDevices[id - 1].identifier);
    };
    refreshButton.onClick = [this] { refreshDevices(); };

    voiceBox.addItem("Voice 1", 1);
    voiceBox.addItem("Voice 2", 2);
    voiceBox.addItem("Voice 3", 3);
    voiceAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        proc.state(), "asidVoice", voiceBox);

    asidButton.setToggleState(proc.isAsidMode(), juce::dontSendNotification);
    asidButton.onClick = [this] { proc.setAsidMode(asidButton.getToggleState()); };

    help.setJustificationType(juce::Justification::topLeft);
    help.setMinimumHorizontalScale(1.0f);
    help.setText("Put one instance on each track and set a different voice per track. "
                 "Enable ASID mode on the SidStation front panel, pick the MIDI output "
                 "here, then turn on ASID Play and the notes on this track drive the "
                 "chosen voice.",
                 juce::dontSendNotification);

    refreshDevices();
    setSize(460, 260);
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
}

void AsidEditor::resized() {
    auto r = getLocalBounds().reduced(14);
    title.setBounds(r.removeFromTop(30));
    r.removeFromTop(8);

    auto row1 = r.removeFromTop(26);
    outLabel.setBounds(row1.removeFromLeft(70));
    refreshButton.setBounds(row1.removeFromRight(80));
    row1.removeFromRight(8);
    outputBox.setBounds(row1);

    r.removeFromTop(10);
    auto row2 = r.removeFromTop(26);
    voiceLabel.setBounds(row2.removeFromLeft(70));
    voiceBox.setBounds(row2.removeFromLeft(140));

    r.removeFromTop(10);
    asidButton.setBounds(r.removeFromTop(26));

    r.removeFromTop(12);
    help.setBounds(r);
}
