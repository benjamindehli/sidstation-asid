// Custom parameter-editing UI (milestone 6).
//
// Builds a synth-style editor straight from the core parameter registry: one
// titled Section per group (Global, Filter, Osc 1-3, LFO 1-4, Direct Ctrl),
// each holding a knob (numeric params) or toggle (booleans) bound to the APVTS.
// The whole thing scrolls in a Viewport. Header-only; included once from
// PluginEditor.cpp.
#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include <memory>
#include <utility>
#include <vector>

#include "sidstation/Parameters.h"

// One labelled control: a rotary knob for numeric params, a toggle for booleans.
class ParamControl : public juce::Component {
public:
    static constexpr int prefW = 78;
    static constexpr int prefH = 84;

    ParamControl(juce::AudioProcessorValueTreeState& s, const sidstation::ParamInfo& p) {
        name.setText(juce::String(p.name.c_str()), juce::dontSendNotification);
        name.setJustificationType(juce::Justification::centred);
        name.setFont(juce::Font(juce::FontOptions().withHeight(11.0f)));
        name.setMinimumHorizontalScale(0.7f);
        addAndMakeVisible(name);

        const juce::String pid(p.id.c_str());
        if (p.kind == sidstation::ParamKind::Bool) {
            isBool = true;
            toggle = std::make_unique<juce::ToggleButton>();
            addAndMakeVisible(*toggle);
            buttonAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
                s, pid, *toggle);
        } else {
            slider = std::make_unique<juce::Slider>(
                juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow);
            slider->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 62, 16);
            addAndMakeVisible(*slider);
            sliderAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
                s, pid, *slider);
        }
    }

    void resized() override {
        auto r = getLocalBounds();
        name.setBounds(r.removeFromTop(15));
        if (isBool)
            toggle->setBounds(r.withSizeKeepingCentre(26, 26));
        else
            slider->setBounds(r.reduced(2, 0));
    }

private:
    juce::Label name;
    bool isBool = false;
    std::unique_ptr<juce::Slider> slider;
    std::unique_ptr<juce::ToggleButton> toggle;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sliderAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> buttonAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ParamControl)
};

// A titled group of controls that flows its knobs into as many columns as fit.
class ParamSection : public juce::Component {
public:
    static constexpr int headerH = 24;
    static constexpr int pad = 8;

    ParamSection(juce::AudioProcessorValueTreeState& s, juce::String sectionTitle,
                 const std::string& group)
        : title(std::move(sectionTitle)) {
        for (const auto& p : sidstation::parameters()) {
            if (p.group != group) continue;
            auto c = std::make_unique<ParamControl>(s, p);
            addAndMakeVisible(*c);
            controls.push_back(std::move(c));
        }
    }

    bool empty() const { return controls.empty(); }

    int preferredHeight(int width) const {
        const int usable = juce::jmax(ParamControl::prefW, width - pad * 2);
        const int cols = juce::jmax(1, usable / ParamControl::prefW);
        const int rows = (static_cast<int>(controls.size()) + cols - 1) / cols;
        return headerH + rows * ParamControl::prefH + pad;
    }

    void paint(juce::Graphics& g) override {
        g.setColour(juce::Colours::white.withAlpha(0.05f));
        g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(1.0f), 5.0f);
        g.setColour(juce::Colours::white.withAlpha(0.85f));
        g.setFont(juce::Font(juce::FontOptions().withHeight(14.0f).withStyle("Bold")));
        g.drawText(title, 12, 2, getWidth() - 24, headerH, juce::Justification::centredLeft);
    }

    void resized() override {
        auto r = getLocalBounds().reduced(pad, 0);
        r.removeFromTop(headerH);
        const int cols = juce::jmax(1, r.getWidth() / ParamControl::prefW);
        const int cw = r.getWidth() / cols;
        for (size_t i = 0; i < controls.size(); ++i) {
            const int col = static_cast<int>(i) % cols;
            const int row = static_cast<int>(i) / cols;
            controls[i]->setBounds(r.getX() + col * cw, r.getY() + row * ParamControl::prefH,
                                   cw, ParamControl::prefH);
        }
    }

private:
    juce::String title;
    std::vector<std::unique_ptr<ParamControl>> controls;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ParamSection)
};

// The scrollable parameter editor: stacks the sections in a Viewport.
class ParametersPanel : public juce::Component {
public:
    explicit ParametersPanel(juce::AudioProcessorValueTreeState& s) {
        content = std::make_unique<Content>(s);
        viewport.setViewedComponent(content.get(), false);
        viewport.setScrollBarsShown(true, false);
        addAndMakeVisible(viewport);
    }

    void resized() override {
        viewport.setBounds(getLocalBounds());
        const int w = juce::jmax(200, viewport.getMaximumVisibleWidth());
        content->setSize(w, content->heightForWidth(w));
    }

private:
    struct Content : juce::Component {
        static constexpr int pad = 10, gap = 10;

        explicit Content(juce::AudioProcessorValueTreeState& s) {
            const std::vector<std::pair<juce::String, std::string>> groups = {
                {"Global", "Global"},           {"Filter", "Filter"},
                {"Oscillator 1", "Osc 1"},       {"Oscillator 2", "Osc 2"},
                {"Oscillator 3", "Osc 3"},       {"LFO 1", "LFO 1"},
                {"LFO 2", "LFO 2"},              {"LFO 3", "LFO 3"},
                {"LFO 4", "LFO 4"},              {"Direct Controllers", "Direct Ctrl"}};
            for (const auto& [titleText, group] : groups) {
                auto sec = std::make_unique<ParamSection>(s, titleText, group);
                if (sec->empty()) continue;
                addAndMakeVisible(*sec);
                sections.push_back(std::move(sec));
            }
        }

        int heightForWidth(int w) const {
            int h = pad;
            for (const auto& s : sections) h += s->preferredHeight(w - pad * 2) + gap;
            return h + pad;
        }

        void resized() override {
            const int w = getWidth();
            int y = pad;
            for (const auto& s : sections) {
                const int hh = s->preferredHeight(w - pad * 2);
                s->setBounds(pad, y, w - pad * 2, hh);
                y += hh + gap;
            }
        }

        std::vector<std::unique_ptr<ParamSection>> sections;
    };

    juce::Viewport viewport;
    std::unique_ptr<Content> content;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ParametersPanel)
};
