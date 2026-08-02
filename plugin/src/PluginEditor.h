// SidStation Editor - plugin editor.
//
// A tabbed UI. The Parameters tab is the custom knob and dropdown editor
// (EditorControls.h). The Librarian tab handles MIDI device selection, patch
// dump, receive, save and send, and the three voice play toggle.
#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include <memory>
#include <vector>

#include "PluginProcessor.h"
#include "sidstation/SyxFile.h"

// The Librarian panel: MIDI device pickers + .syx folder library + transfer.
class LibrarianComponent : public juce::Component,
                           private juce::Timer,
                           private juce::ListBoxModel {
public:
    explicit LibrarianComponent(SidStationAudioProcessor&);
    ~LibrarianComponent() override;

    void resized() override;

private:
    void refreshDevices();
    void refreshList();
    void timerCallback() override;

    // ListBoxModel
    int getNumRows() override;
    void paintListBoxItem(int row, juce::Graphics&, int w, int h, bool selected) override;
    void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override;

    void chooseFolder();
    void saveReceived();
    void sendSelected();

    SidStationAudioProcessor& proc;

    juce::Label   outLabel{{}, "MIDI Out:"}, inLabel{{}, "MIDI In:"};
    juce::ComboBox outputBox, inputBox;
    juce::TextButton refreshButton{"Refresh"};

    juce::Label   folderLabel;
    juce::TextButton chooseFolderButton{"Choose Folder"};
    juce::ListBox patchList;

    juce::TextButton sendSelectedButton{"Send Selected -> Unit"};
    juce::TextButton sendEditorButton{"Send Editor -> Unit"};
    juce::TextButton saveReceivedButton{"Save Received..."};
    juce::Label   statusLabel;

    juce::ToggleButton voicePlayButton{"3-Voice Play (MIDI ch 1/2/3 to Osc 1/2/3)"};

    // One selectable patch in the library (flattened across all .syx files in
    // the folder, so a bank of 100 shows as 100 sendable patches).
    struct LibItem {
        juce::String       display;  // "file : patchname"
        sidstation::Bytes  message;  // the single patch-dump SysEx
    };

    juce::Array<juce::MidiDeviceInfo> outDevices, inDevices;
    juce::File currentFolder;
    std::vector<LibItem> items;

    std::unique_ptr<juce::FileChooser> chooser;
    bool hasReceived = false;
    sidstation::Bytes receivedRaw;
    juce::String receivedName;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LibrarianComponent)
};

class SidStationEditor : public juce::AudioProcessorEditor {
public:
    explicit SidStationEditor(SidStationAudioProcessor&);
    void resized() override;

private:
    juce::TabbedComponent tabs{juce::TabbedButtonBar::TabsAtTop};
    LibrarianComponent librarian;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SidStationEditor)
};
