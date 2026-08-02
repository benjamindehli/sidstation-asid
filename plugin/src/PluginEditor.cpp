#include "PluginEditor.h"

using namespace sidstation;

// ---------------------------------------------------------------------------
// LibrarianComponent
// ---------------------------------------------------------------------------
LibrarianComponent::LibrarianComponent(SidStationAudioProcessor& p) : proc(p) {
    addAndMakeVisible(outLabel);
    addAndMakeVisible(inLabel);
    addAndMakeVisible(outputBox);
    addAndMakeVisible(inputBox);
    addAndMakeVisible(refreshButton);
    addAndMakeVisible(folderLabel);
    addAndMakeVisible(chooseFolderButton);
    addAndMakeVisible(patchList);
    addAndMakeVisible(sendSelectedButton);
    addAndMakeVisible(sendEditorButton);
    addAndMakeVisible(saveReceivedButton);
    addAndMakeVisible(statusLabel);

    outputBox.onChange = [this] {
        const int id = outputBox.getSelectedId();
        if (id >= 1 && id <= outDevices.size())
            proc.midi().openOutputByIdentifier(outDevices[id - 1].identifier);
    };
    inputBox.onChange = [this] {
        const int id = inputBox.getSelectedId();
        if (id >= 1 && id <= inDevices.size())
            proc.midi().openInputByIdentifier(inDevices[id - 1].identifier);
    };
    refreshButton.onClick = [this] { refreshDevices(); };
    chooseFolderButton.onClick = [this] { chooseFolder(); };
    sendSelectedButton.onClick = [this] { sendSelected(); };
    sendEditorButton.onClick = [this] { proc.sendAllParameters(); };
    saveReceivedButton.onClick = [this] { saveReceived(); };
    saveReceivedButton.setEnabled(false);

    patchList.setModel(this);
    patchList.setRowHeight(22);

    folderLabel.setColour(juce::Label::backgroundColourId,
                          juce::Colours::black.withAlpha(0.15f));
    statusLabel.setText("No patch received yet.", juce::dontSendNotification);

    currentFolder = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                        .getChildFile("SidStation Patches");
    folderLabel.setText(currentFolder.getFullPathName(), juce::dontSendNotification);

    refreshDevices();
    refreshList();
    startTimerHz(10);
}

LibrarianComponent::~LibrarianComponent() { stopTimer(); }

void LibrarianComponent::refreshDevices() {
    outDevices = MidiHub::availableOutputs();
    inDevices = MidiHub::availableInputs();

    outputBox.clear(juce::dontSendNotification);
    for (int i = 0; i < outDevices.size(); ++i)
        outputBox.addItem(outDevices[i].name, i + 1);
    inputBox.clear(juce::dontSendNotification);
    for (int i = 0; i < inDevices.size(); ++i)
        inputBox.addItem(inDevices[i].name, i + 1);

    // Reflect any device already open.
    const auto outId = proc.midi().outputIdentifier();
    for (int i = 0; i < outDevices.size(); ++i)
        if (outDevices[i].identifier == outId)
            outputBox.setSelectedId(i + 1, juce::dontSendNotification);
    const auto inId = proc.midi().inputIdentifier();
    for (int i = 0; i < inDevices.size(); ++i)
        if (inDevices[i].identifier == inId)
            inputBox.setSelectedId(i + 1, juce::dontSendNotification);
}

void LibrarianComponent::refreshList() {
    entries = scanPatchFolder(currentFolder.getFullPathName().toStdString());
    patchList.updateContent();
    patchList.repaint();
}

void LibrarianComponent::timerCallback() {
    if (auto rp = proc.takeReceivedPatch()) {
        hasReceived = true;
        receivedRaw = rp->raw;
        receivedName = juce::String(rp->patch.name().c_str());
        saveReceivedButton.setEnabled(true);
        statusLabel.setText("Received patch: \"" + receivedName + "\"",
                            juce::dontSendNotification);
    }
}

int LibrarianComponent::getNumRows() { return static_cast<int>(entries.size()); }

void LibrarianComponent::paintListBoxItem(int row, juce::Graphics& g, int w, int h,
                                          bool selected) {
    if (row < 0 || row >= static_cast<int>(entries.size())) return;
    if (selected) g.fillAll(juce::Colours::steelblue.withAlpha(0.4f));
    const auto& e = entries[static_cast<std::size_t>(row)];
    g.setColour(e.valid ? juce::Colours::white : juce::Colours::grey);
    auto text = juce::String(e.name.c_str());
    if (!e.valid) text += "  (not a patch)";
    g.drawText(text, 6, 0, w - 12, h, juce::Justification::centredLeft);
}

void LibrarianComponent::listBoxItemDoubleClicked(int, const juce::MouseEvent&) {
    sendSelected();
}

void LibrarianComponent::chooseFolder() {
    chooser = std::make_unique<juce::FileChooser>("Choose patch folder", currentFolder);
    chooser->launchAsync(juce::FileBrowserComponent::openMode |
                             juce::FileBrowserComponent::canSelectDirectories,
                         [this](const juce::FileChooser& fc) {
                             auto f = fc.getResult();
                             if (f.isDirectory()) {
                                 currentFolder = f;
                                 folderLabel.setText(f.getFullPathName(),
                                                     juce::dontSendNotification);
                                 refreshList();
                             }
                         });
}

void LibrarianComponent::saveReceived() {
    if (!hasReceived) return;
    if (!currentFolder.exists()) currentFolder.createDirectory();
    auto name = receivedName.isNotEmpty() ? receivedName : juce::String("patch");
    auto suggested = currentFolder.getChildFile(name + ".syx");
    chooser = std::make_unique<juce::FileChooser>("Save received patch", suggested, "*.syx");
    auto raw = receivedRaw;  // capture a copy for the async callback
    chooser->launchAsync(juce::FileBrowserComponent::saveMode |
                             juce::FileBrowserComponent::canSelectFiles |
                             juce::FileBrowserComponent::warnAboutOverwriting,
                         [this, raw](const juce::FileChooser& fc) {
                             auto f = fc.getResult();
                             if (f != juce::File{}) {
                                 writeSyxFile(f.getFullPathName().toStdString(), raw);
                                 refreshList();
                             }
                         });
}

void LibrarianComponent::sendSelected() {
    const int row = patchList.getSelectedRow();
    if (row < 0 || row >= static_cast<int>(entries.size())) return;
    if (auto data = readSyxFile(entries[static_cast<std::size_t>(row)].path)) {
        proc.sendSyxToUnit(*data);
        statusLabel.setText("Sent \"" +
                                juce::String(entries[static_cast<std::size_t>(row)].name.c_str()) +
                                "\" to unit.",
                            juce::dontSendNotification);
    }
}

void LibrarianComponent::resized() {
    auto r = getLocalBounds().reduced(10);

    auto row1 = r.removeFromTop(26);
    outLabel.setBounds(row1.removeFromLeft(70));
    outputBox.setBounds(row1.removeFromLeft(220));
    row1.removeFromLeft(10);
    inLabel.setBounds(row1.removeFromLeft(60));
    inputBox.setBounds(row1.removeFromLeft(220));
    row1.removeFromLeft(10);
    refreshButton.setBounds(row1.removeFromLeft(80));

    r.removeFromTop(10);
    auto row2 = r.removeFromTop(26);
    chooseFolderButton.setBounds(row2.removeFromLeft(120));
    row2.removeFromLeft(10);
    folderLabel.setBounds(row2);

    r.removeFromTop(10);
    auto buttons = r.removeFromBottom(30);
    sendSelectedButton.setBounds(buttons.removeFromLeft(160));
    buttons.removeFromLeft(8);
    sendEditorButton.setBounds(buttons.removeFromLeft(150));
    buttons.removeFromLeft(8);
    saveReceivedButton.setBounds(buttons.removeFromLeft(140));

    r.removeFromBottom(8);
    statusLabel.setBounds(r.removeFromBottom(24));
    r.removeFromBottom(6);
    patchList.setBounds(r);
}

// ---------------------------------------------------------------------------
// SidStationEditor
// ---------------------------------------------------------------------------
SidStationEditor::SidStationEditor(SidStationAudioProcessor& p)
    : juce::AudioProcessorEditor(p), proc(p), librarian(p) {
    tabs.addTab("Librarian", juce::Colours::darkgrey, &librarian, false);
    tabs.addTab("Parameters", juce::Colours::darkgrey,
                new juce::GenericAudioProcessorEditor(p), true);
    addAndMakeVisible(tabs);
    setResizable(true, true);
    setSize(780, 560);
}

void SidStationEditor::resized() { tabs.setBounds(getLocalBounds()); }
