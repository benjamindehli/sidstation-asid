// SidStation ASID - plugin editor.
//
// Pick the MIDI output and which SID voice this instance drives, and shape the
// sound: per-voice waveform, envelope, pulse width, sync and ring, plus the
// shared filter, volume and output latency.
#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include <map>
#include <memory>

#include "AsidProcessor.h"
#include "SidLookAndFeel.h"

class AsidEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit AsidEditor(AsidProcessor&);
    ~AsidEditor() override;
    void paint(juce::Graphics&) override;
    void parentHierarchyChanged() override;            // brand the standalone window's title bar
    void resized() override;

private:
    void timerCallback() override;

    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAtt = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    // This instance's SID voice (0..2), read from the shared parameter tree.
    int currentVoice() const {
        auto* p = state.getRawParameterValue("asidVoice");
        return p ? juce::jlimit(0, 2, juce::roundToInt(p->load())) : 0;
    }
    void refreshDevices();
    // Enable/disable controls that only apply in some states (pulse-wave-only
    // pulse width, sync-vs-free rate). Driven from the timer.
    void updateEnablement();
    void setupKnob(juce::Component& parent, juce::Slider&, juce::Label&, const juce::String& name,
                   const juce::String& paramId, std::unique_ptr<SliderAtt>&);
    void setTab(int t);                                // show one page, hide the others
    void refreshPresets(const juce::String& selectName = {});  // rebuild the preset list
    void cyclePreset(int delta);                       // step to the previous/next preset
    void layoutOscPage(juce::Rectangle<int> area);
    void layoutAmpModPage(juce::Rectangle<int> area);
    void layoutSharedPage(juce::Rectangle<int> area);
    void layoutWavePage(juce::Rectangle<int> area);

    // One reusable block of controls for a single LFO target.
    struct LfoControls {
        // The Shape selector doubles as the on/off: its first item "Off" disables
        // the LFO and any waveform enables it, so there is no separate On button.
        juce::ComboBox shapeBox;
        juce::ToggleButton syncButton{"BPM Sync"};  // under Rate: free Hz vs tempo division
        juce::ToggleButton wheelButton{"Mod Wheel"};   // under Depth: mod wheel scales the depth
        juce::Slider rateKnob, depthKnob, delayKnob;
        juce::Label rateLabel, depthLabel, delayLabel;
        std::unique_ptr<ButtonAtt> syncAtt, wheelAtt;
        std::unique_ptr<SliderAtt> rateAtt, depthAtt, delayAtt;
        juce::String prefix;  // parameter prefix, for re-binding the rate knob
        int rateMode = -1;    // -1 uninit, 0 free (Hz), 1 tempo-synced (division)
    };
    // A 2-segment switch bound to a 2-value choice parameter: the selected segment
    // lights up, clicking a segment sets the parameter.
    void setupSwitch(juce::ToggleButton& segA, juce::ToggleButton& segB,
                     const juce::String& labelA, const juce::String& labelB,
                     const juce::String& paramId);
    void setupLfo(juce::Component& parent, LfoControls&, const juce::String& prefix);
    void layoutLfo(LfoControls&, juce::Rectangle<int> area);
    // The rate knob drives the free Hz rate, or the stepped tempo division when
    // Tempo Sync is on. Re-binds it to the matching parameter.
    void configureRateKnob(LfoControls&, bool synced);

    static constexpr int kBorder = 16;     // C64 screen border, left/right
    static constexpr int kBorderY = 22;    // C64 screen border, top/bottom (centres the 496px screen)
    static constexpr int kPad = 8;         // padding inside the dark screen, all sides
    static constexpr int kPresetBarH = 34; // dark preset bar above the 720x540 content

    // The dark screen rectangle (kBg) and the padded area the controls live in.
    juce::Rectangle<int> screenBox() const {
        return getLocalBounds().withTrimmedTop(kPresetBarH).reduced(kBorder, kBorderY);
    }
    // 8px padding on the sides and bottom, but 5px on top: the content sits 3px
    // higher so the top row's boxes land a flush 8px below the dark-screen edge.
    juce::Rectangle<int> innerArea() const {
        return screenBox().reduced(kPad, 0).withTrimmedTop(kPad - 3).withTrimmedBottom(kPad);
    }

    SidLookAndFeel laf;  // declared first so it outlives every child that uses it
    // Neutral dark look for the preset bar controls, since they sit on the dark bar
    // outside the C64 screen and should not be Commodore-styled.
    juce::LookAndFeel_V4 barLnF{juce::LookAndFeel_V4::getDarkColourScheme()};
    AsidProcessor& proc;
    juce::AudioProcessorValueTreeState& state;
    // The "in use" hint uses a BubbleMessageComponent (the same reliable popup the
    // knob value bubbles use); JUCE's TooltipWindow did not show inside the DAW.
    juce::BubbleMessageComponent voiceBubble;

    juce::Image logo;  // Dehli Musikk PETSCII logo (96x8), top-left, drawn at 2x
    // Optional semi-transparent overlays painted over the whole GUI. They carry their
    // own alpha. A per-voice "OverlayVoiceN.png" is used if present, else "Overlay.png".
    juce::Image overlay[3];
    juce::Image overlayShared;
    // The overlay is a top child component (not paintOverChildren) so it sits over
    // the controls yet under the tooltip bubble, and passes all mouse events through.
    struct OverlayComp : juce::Component {
        std::function<const juce::Image*()> pick;
        OverlayComp() { setInterceptsMouseClicks(false, false); }
        void paint(juce::Graphics& g) override {
            if (pick)
                if (const juce::Image* img = pick())
                    if (img->isValid())
                        g.drawImageWithin(*img, 0, 0, getWidth(), getHeight(),
                                          juce::RectanglePlacement::stretchToFit);
        }
    };
    OverlayComp overlayComp;

    // Hover hints via BubbleMessageComponent (JUCE's TooltipWindow did not show in
    // the DAW). Attach a control with add(); it shows the text on mouse-enter.
    struct Hints : juce::MouseListener {
        static constexpr int anySide = juce::BubbleComponent::above | juce::BubbleComponent::below
                                     | juce::BubbleComponent::left | juce::BubbleComponent::right;
        juce::BubbleMessageComponent bubble;
        juce::Component* host = nullptr;
        std::map<juce::Component*, juce::String> text;
        std::map<juce::Component*, int> place;
        void add(juce::Component& c, const juce::String& t, int placement = anySide) {
            text[&c] = t;
            place[&c] = placement;
            c.addMouseListener(this, false);
        }
        void mouseEnter(const juce::MouseEvent& e) override {
            auto it = text.find(e.eventComponent);
            if (it == text.end() || host == nullptr) return;
            juce::AttributedString msg(it->second);
            msg.setColour(juce::Colour(SidLookAndFeel::kHot));
            msg.setJustification(juce::Justification::centred);
            bubble.setAllowedPlacement(place[e.eventComponent]);
            bubble.showAt(host->getLocalArea(e.eventComponent, e.eventComponent->getLocalBounds()),
                          msg, 0, false, false);
        }
        void mouseExit(const juce::MouseEvent&) override { bubble.setVisible(false); }
    };
    Hints hints;
    juce::Label title{{}, "SidStation ASID"};

    // Top-right voice selector: three cells 1/2/3. This instance's voice is bright.
    // A voice another instance already drives shows in its (dim) hue and is blocked
    // (hovering it shows an "in use" bubble). A free voice is monochrome and reveals
    // its hue on hover, inviting a click to switch to it.
    struct VoiceSwitch : juce::Component {
        int selected = 0;                              // 0..2, this instance's voice
        int hovered = -1;                              // cell under the mouse, or -1
        juce::Colour colours[3];                       // per-voice accent, filled by the editor
        bool usedByOther[3] = {false, false, false};   // driven by another instance
        std::function<void(int)> onSelect;
        std::function<void(int)> onHover;              // hovered cell changed (-1 = left)
        int cellAt(int x) const { return juce::jlimit(0, 2, x * 3 / juce::jmax(1, getWidth())); }
        void setHover(int i) { if (i != hovered) { hovered = i; repaint(); if (onHover) onHover(i); } }
        void paint(juce::Graphics& g) override {
            const int n = 3, w = getWidth() / n;
            for (int i = 0; i < n; ++i) {
                juce::Rectangle<int> cell(i * w, 0, (i == n - 1 ? getWidth() - i * w : w), getHeight());
                const bool on = i == selected;
                // Colour a cell when it is taken by another instance, or while hovering
                // a free one; otherwise a free cell is monochrome (saturation off).
                const float sat = (usedByOther[i] || i == hovered) ? 1.0f : 0.0f;
                const auto c = colours[i];
                g.setColour(on ? c : c.withMultipliedSaturation(0.5f * sat).withMultipliedBrightness(0.24f));
                g.fillRect(cell);  // flush: no gap between cells, right cell meets the padding
                g.setColour(on ? juce::Colour(SidLookAndFeel::kBg)
                              : c.withMultipliedSaturation(0.85f * sat).withMultipliedBrightness(0.68f));
                g.setFont(SidLookAndFeel::mono(15.0f, true));
                g.drawText(juce::String(i + 1), cell, juce::Justification::centred);
            }
        }
        void mouseMove(const juce::MouseEvent& e) override { setHover(cellAt(e.x)); }
        void mouseExit(const juce::MouseEvent&) override { setHover(-1); }
        void mouseDown(const juce::MouseEvent& e) override {
            const int i = cellAt(e.x);
            if (usedByOther[i]) return;   // voice taken by another instance: blocked
            if (onSelect) onSelect(i);
        }
    };
    VoiceSwitch voiceSwitch;
    juce::Label voiceCaption{{}, "VOICE"};
    // Tab bar and the three pages the tabs switch between.
    juce::TextButton oscTabBtn{"VOICE"}, ampModTabBtn{"MODULATION"}, sharedTabBtn{"GLOBAL"}, waveTabBtn{"WAVETABLE"};
    juce::Component oscPage, ampModPage, sharedPage, wtPage;
    int currentTab = 0;

    // Preset bar under the tabs: browse (prev/next + editable menu) and Save.
    juce::ComboBox presetBox;
    juce::TextButton presetPrevBtn{"<"}, presetNextBtn{">"}, presetSaveBtn{"Save"}, presetDeleteBtn{"Delete"};

    // MIDI load meter (bytes/sec vs the SidStation's ~3125 B/s ceiling).
    juce::Label midiLoadLabel{{}, "MIDI LOAD"};
    juce::Rectangle<int> meterArea;
    long long lastBytes = 0;
    double lastBytesMs = 0.0;
    float midiLoad = 0.0f;

    juce::Label modRateLabel{{}, "Clock"};
    juce::ComboBox modRateBox;
    std::unique_ptr<ComboAtt> modRateAtt;

    juce::Label outLabel{{}, "MIDI Out"};
    juce::ComboBox outputBox;
    juce::TextButton refreshButton{"Scan"};
    // Tempo: an editable BPM field in standalone, a read-only host BPM in a DAW. A
    // hidden slider carries the APVTS "bpm" binding; bpmField is the visible number.
    juce::Label bpmLabel{{}, "BPM"};
    juce::Label bpmField;
    juce::Slider bpmSlider;
    std::unique_ptr<SliderAtt> bpmAtt;
    bool bpmHostDriven = false;  // last known state, to toggle editability only on change

    // Section boxes drawn as titled group frames, spread across the three tab
    // pages. Filter nests Cutoff Mod. Filter and Master are the shared controls.
    juce::GroupComponent oscGroup, glideGroup, ampGroup;
    juce::GroupComponent pitchModGroup, pwModGroup, filterGroup, cutModGroup, masterGroup;
    LfoControls pitchLfoUi, pwLfoUi, cutLfoUi;

    // Wavetable (WAVE tab): global config plus one row per step.
    juce::GroupComponent wtConfigGroup, wtStepsGroup;
    juce::ToggleButton wtOnButton{"On"};
    std::unique_ptr<ButtonAtt> wtOnAtt;
    juce::Slider wtSpeedKnob, wtLengthKnob, wtLoopKnob;
    juce::Label wtSpeedLabel, wtLengthLabel, wtLoopLabel;
    std::unique_ptr<SliderAtt> wtSpeedAtt, wtLengthAtt, wtLoopAtt;
    // Per-step indicator: the step number, dim when the step is beyond the table
    // length, an accent outline at the loop point, and an accent fill while it plays.
    // An 8px loop indicator (accent box at the loop-point step) followed by the
    // step number box (accent fill while playing, dim beyond the table length).
    struct StepIndicator : juce::Component {
        int number = 1;
        bool active = false, loop = false, playing = false;
        juce::Colour accent{juce::Colour(SidLookAndFeel::kAccent)};
        void paint(juce::Graphics& g) override {
            auto b = getLocalBounds();
            auto loopR = b.removeFromLeft(8);  // loop indicator, then the number box
            if (loop) { g.setColour(accent); g.fillRect(loopR); }
            if (playing) { g.setColour(accent); g.fillRect(b); }
            g.setColour(playing ? juce::Colour(SidLookAndFeel::kBg)
                                : juce::Colour(active ? SidLookAndFeel::kFg : SidLookAndFeel::kDim));
            g.setFont(SidLookAndFeel::mono());
            g.drawText(juce::String(number), b, juce::Justification::centred);
        }
    };
    StepIndicator wtStepInd[AsidProcessor::kWtSteps];
    // Per step, four combinable waveform toggles under shared column headers, plus
    // a header over the arp stepper column.
    juce::Label wtWaveHead[4];
    juce::Label wtSyncHead{{}, "Syn"}, wtRingHead{{}, "Rin"}, wtTestHead{{}, "Tst"},
                wtPwHead{{}, "PW"}, wtArpHead{{}, "Arp"};
    juce::ToggleButton wtWaveTog[AsidProcessor::kWtSteps][4];
    std::unique_ptr<ButtonAtt> wtWaveTogAtt[AsidProcessor::kWtSteps][4];
    // Per-step Sync/Ring/Test toggles and a small Pulse Width knob.
    juce::ToggleButton wtSyncTog[AsidProcessor::kWtSteps], wtRingTog[AsidProcessor::kWtSteps],
                       wtTestTog[AsidProcessor::kWtSteps];
    std::unique_ptr<ButtonAtt> wtSyncAtt[AsidProcessor::kWtSteps], wtRingAtt[AsidProcessor::kWtSteps],
                               wtTestAtt[AsidProcessor::kWtSteps];
    juce::Slider wtPwKnob[AsidProcessor::kWtSteps];
    std::unique_ptr<SliderAtt> wtPwAtt[AsidProcessor::kWtSteps];
    // Arp stepper per step: a hidden slider holds the value (APVTS binding),
    // shown as a bordered number field flanked by square - / + buttons.
    juce::Slider wtArpSlider[AsidProcessor::kWtSteps];
    std::unique_ptr<SliderAtt> wtArpAtt[AsidProcessor::kWtSteps];
    juce::Label wtArpValue[AsidProcessor::kWtSteps];
    juce::TextButton wtArpDec[AsidProcessor::kWtSteps], wtArpInc[AsidProcessor::kWtSteps];

    // Oscillator. The four SID waveforms combine, so they are checkboxes rather
    // than a single choice (noise stays exclusive, handled in updateEnablement).
    juce::Label waveLabel{{}, "Waveform"};
    juce::ToggleButton waveTriButton{"Tri"}, waveSawButton{"Saw"},
                       wavePulseButton{"Pulse"}, waveNoiseButton{"Noise"};
    std::unique_ptr<ButtonAtt> waveTriAtt, waveSawAtt, wavePulseAtt, waveNoiseAtt;
    juce::ToggleButton syncButton{"Sync"}, ringButton{"Ring"}, testButton{"Test"};
    std::unique_ptr<ButtonAtt> syncAtt, ringAtt, testAtt;
    juce::Slider pwKnob, coarseKnob, fineKnob, bendKnob, portaKnob;
    juce::Label pwLabel, coarseLabel, fineLabel, bendLabel, portaLabel;
    std::unique_ptr<SliderAtt> pwAtt, coarseAtt, fineAtt, bendAtt, portaAtt;
    // Glide trigger (Legato/Always) and type (Smooth/Stepped) as 2-segment
    // switches (a pair of toggle buttons each), stacked and grouped with glide time.
    juce::ToggleButton portaTrigBtns[2], portaTypeBtns[2];

    // Control groups are separated by spacing alone now (no divider lines), so this
    // is an empty placeholder kept only so the layout's group gaps stay put.
    struct VDivider : juce::Component {};
    VDivider tuningDiv1, tuningDiv2, oscDiv1, oscDiv2, filtDiv1, filtDiv2;
    void placeDivider(VDivider&, juce::Rectangle<int>) {}

    // Amp envelope.
    juce::Slider attackKnob, decayKnob, sustainKnob, releaseKnob;
    juce::Label attackLabel, decayLabel, sustainLabel, releaseLabel;
    std::unique_ptr<SliderAtt> attackAtt, decayAtt, sustainAtt, releaseAtt;

    // Shared across all three voices. Filter Active is one checkbox per voice
    // (the instance's own voice is highlighted); filter Mode bits combine.
    juce::Label filterActiveLabel{{}, "Filter"};
    juce::ToggleButton filtButtons[3];
    std::unique_ptr<ButtonAtt> filtAtts[3];
    juce::ToggleButton filtExtButton{"External"};  // route the external audio input through the filter
    std::unique_ptr<ButtonAtt> filtExtAtt;
    juce::Label filterModeLabel{{}, "Mode"};
    juce::ToggleButton modeButtons[3];  // LP, BP, HP
    std::unique_ptr<ButtonAtt> modeAtts[3];
    int highlightedVoice = -1;  // which filter checkbox is currently marked as ours
    juce::Slider cutoffKnob, resKnob, volumeKnob, latencyKnob;
    juce::Label cutoffLabel, resLabel, volumeLabel, latencyLabel;
    std::unique_ptr<SliderAtt> cutoffAtt, resAtt, volumeAtt, latencyAtt;
    juce::ToggleButton voice3offButton{"V3 Off"};  // V3 output off, used as mod source
    std::unique_ptr<ButtonAtt> voice3offAtt;
    juce::TextButton panicButton{"Panic"};  // all-notes-off for every voice
    juce::TextButton initButton{"Init"};    // reset this voice's sound to default

    juce::Array<juce::MidiDeviceInfo> outDevices;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AsidEditor)
};
