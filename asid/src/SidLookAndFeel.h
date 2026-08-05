// A Commodore 64 flavoured look for the ASID editor: the classic blue screen,
// light-blue foreground, blocky knobs and toggles, and a monospaced (terminal)
// typeface. This only overrides drawing, never behaviour, so it drops on and off
// cleanly. The SID heritage of the SidStation is the point.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>

class SidLookAndFeel : public juce::LookAndFeel_V4 {
public:
    // C64 screen palette.
    static constexpr juce::uint32 kBg    = 0xff483aaa;  // screen blue (darker shade)
    static constexpr juce::uint32 kPanel = 0xff3a2f88;  // slightly darker inset
    static constexpr juce::uint32 kFg    = 0xff867ade;  // light blue (text, outlines, border)
    static constexpr juce::uint32 kDim   = 0xff635ab8;  // dimmed light blue (disabled)
    static constexpr juce::uint32 kHot   = 0xffffffff;  // white highlight (pointers, text)
    static constexpr juce::uint32 kAccent = 0xff35d6d0; // cyan for active states / values

    SidLookAndFeel() {
        setColour(juce::ResizableWindow::backgroundColourId, juce::Colour(kBg));
        setColour(juce::Label::textColourId, juce::Colour(kFg));
        setColour(juce::Slider::textBoxTextColourId, juce::Colour(kHot));
        setColour(juce::Slider::textBoxOutlineColourId, juce::Colour(kFg));
        setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(kPanel));
        setColour(juce::ComboBox::backgroundColourId, juce::Colour(kPanel));
        setColour(juce::ComboBox::textColourId, juce::Colour(kFg));
        setColour(juce::ComboBox::outlineColourId, juce::Colour(kFg));
        setColour(juce::ComboBox::arrowColourId, juce::Colour(kFg));
        setColour(juce::PopupMenu::backgroundColourId, juce::Colour(kPanel));
        setColour(juce::PopupMenu::textColourId, juce::Colour(kFg));
        setColour(juce::PopupMenu::highlightedBackgroundColourId, juce::Colour(kFg));
        setColour(juce::PopupMenu::highlightedTextColourId, juce::Colour(kBg));
        setColour(juce::TextButton::buttonColourId, juce::Colour(kPanel));
        setColour(juce::TextButton::textColourOffId, juce::Colour(kFg));
        setColour(juce::TextButton::textColourOnId, juce::Colour(kHot));
        setColour(juce::ToggleButton::textColourId, juce::Colour(kFg));
        setColour(juce::ToggleButton::tickColourId, juce::Colour(kHot));
        setColour(juce::GroupComponent::outlineColourId, juce::Colour(kFg));
        setColour(juce::GroupComponent::textColourId, juce::Colour(kHot));
        // The value bubble shown while turning a knob (setPopupDisplayEnabled).
        setColour(juce::BubbleComponent::backgroundColourId, juce::Colour(kPanel));
        setColour(juce::BubbleComponent::outlineColourId, juce::Colour(kFg));
        setColour(juce::TooltipWindow::backgroundColourId, juce::Colour(kPanel));
        setColour(juce::TooltipWindow::outlineColourId, juce::Colour(kFg));
        setColour(juce::TooltipWindow::textColourId, juce::Colour(kHot));
    }

    juce::Font getSliderPopupFont(juce::Slider&) override { return mono(15.0f, true); }

    static juce::Font mono(float height, bool bold = false) {
        auto o = juce::FontOptions().withName(juce::Font::getDefaultMonospacedFontName()).withHeight(height);
        return juce::Font(bold ? o.withStyle("Bold") : o);
    }

    juce::Font getLabelFont(juce::Label& l) override { return mono(l.getFont().getHeight()); }
    juce::Font getComboBoxFont(juce::ComboBox&) override { return mono(14.0f); }
    juce::Font getPopupMenuFont() override { return mono(14.0f); }
    juce::Font getTextButtonFont(juce::TextButton&, int) override { return mono(14.0f); }

    // Round knob: a filled body with a bright value arc sweeping around it and a
    // pointer line. The arc reads the value at a glance; the body gives it weight.
    void drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h,
                          float pos, float startAngle, float endAngle, juce::Slider& s) override {
        const bool on = s.isEnabled();
        const auto fg = juce::Colour(on ? kFg : kDim);
        const auto hot = juce::Colour(on ? kHot : kDim);

        auto area = juce::Rectangle<int>(x, y, w, h).toFloat().reduced(4.0f);
        const float size = juce::jmin(area.getWidth(), area.getHeight());
        const auto c = area.getCentre();
        const float radius = size * 0.5f;
        const float arcR = radius - 2.0f;
        const float knobR = radius - 9.0f;
        const float angle = startAngle + pos * (endAngle - startAngle);

        // Value arc: a dim full-sweep track with a bright fill up to the value.
        const juce::PathStrokeType stroke(4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
        juce::Path track, value;
        track.addCentredArc(c.x, c.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
        g.setColour(juce::Colour(kBg).darker(0.35f));
        g.strokePath(track, stroke);
        if (angle > startAngle + 0.01f) {
            value.addCentredArc(c.x, c.y, arcR, arcR, 0.0f, startAngle, angle, true);
            g.setColour(juce::Colour(on ? kAccent : kDim));  // accent value arc
            g.strokePath(value, stroke);
        }

        // Knob body, then a pointer from the centre to its rim.
        auto body = juce::Rectangle<float>(knobR * 2, knobR * 2).withCentre(c);
        g.setColour(juce::Colour(kPanel));
        g.fillEllipse(body);
        g.setColour(fg);
        g.drawEllipse(body, 2.0f);
        const juce::Point<float> tip(c.x + std::sin(angle) * (knobR - 2.0f),
                                     c.y - std::cos(angle) * (knobR - 2.0f));
        g.setColour(hot);
        g.drawLine({c, tip}, 2.5f);
    }

    // Shared height for checkboxes, combo boxes, number boxes and buttons, so a
    // row of mixed controls lines up. They also share the panel fill and 2px
    // border (the same look as the buttons via drawButtonBackground).
    static constexpr float kFieldH = 24.0f;
    static constexpr float kCaptionH = 13.0f;  // one size for all caption labels

    // Toggle drawn as a labelled button: it fills with the accent colour when on
    // (inverting its text), like the tab buttons, so on/off controls have the same
    // weight as everything else. "sidHighlight" marks the instance's own filter
    // voice with a white border. An empty label gives a plain on/off cell (the
    // wavetable step matrix).
    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& b, bool over, bool down) override {
        const bool on = b.getToggleState();
        const bool en = b.isEnabled();
        const bool hi = static_cast<bool>(b.getProperties().getWithDefault("sidHighlight", false));
        auto r = b.getLocalBounds().toFloat();
        if (on) g.setColour(juce::Colour(en ? kAccent : kDim));  // accent when active
        else    g.setColour(juce::Colour(kPanel).brighter(down ? 0.12f : (over ? 0.06f : 0.0f)));
        g.fillRect(r);
        g.setColour(juce::Colour(hi ? kHot : (en ? kFg : kDim)));
        g.drawRect(r, 2.0f);
        if (b.getButtonText().isNotEmpty()) {
            g.setColour(juce::Colour(on ? kBg : (hi ? kHot : (en ? kFg : kDim))));
            g.setFont(mono(14.0f, hi || on));
            g.drawText(b.getButtonText(), r.getSmallestIntegerContainer(), juce::Justification::centred, false);
        }
    }

    // Labels come in two kinds: a slider's number text box (its parent is the
    // Slider) draws as a field, matching the combo, checkbox and button look; a
    // plain caption draws as text only. Detecting the parent is reliable, where
    // guessing from the background colour was not.
    void drawLabel(juce::Graphics& g, juce::Label& l) override {
        const auto bounds = l.getLocalBounds();
        const auto bg = l.findColour(juce::Label::backgroundColourId);
        const bool field = !bg.isTransparent()
                           || dynamic_cast<juce::Slider*>(l.getParentComponent()) != nullptr
                           || static_cast<bool>(l.getProperties().getWithDefault("sidField", false));
        const float alpha = l.isEnabled() ? 1.0f : 0.5f;
        if (field) {
            g.setColour(bg.isTransparent() ? juce::Colour(kPanel) : bg);
            g.fillRect(bounds);
            g.setColour(juce::Colour(l.isEnabled() ? kFg : kDim));
            g.drawRect(bounds, 2);
        }
        if (!l.isBeingEdited()) {
            // All caption labels share one size; only large headings (the title)
            // keep their own font.
            const float h = l.getFont().getHeight();
            const auto font = mono(h >= 16.0f ? h : kCaptionH);
            g.setColour(l.findColour(juce::Label::textColourId).withMultipliedAlpha(alpha));
            g.setFont(font);
            auto textArea = bounds.reduced(field ? 3 : 0, 0);
            g.drawFittedText(l.getText(), textArea, l.getJustificationType(),
                             juce::jmax(1, (int) ((float) textArea.getHeight() / font.getHeight())),
                             l.getMinimumHorizontalScale());
        }
    }

    // Give the combo's selected text a little left padding so it is not jammed
    // against the border, and leave room on the right for the arrow.
    void positionComboBoxText(juce::ComboBox& box, juce::Label& label) override {
        label.setBounds(7, 1, box.getWidth() - 7 - 18, box.getHeight() - 2);
        label.setFont(getComboBoxFont(box));
    }

    void drawComboBox(juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box) override {
        auto r = juce::Rectangle<float>(0, 0, (float) w, (float) h);
        g.setColour(box.findColour(juce::ComboBox::backgroundColourId));
        g.fillRect(r);
        g.setColour(box.findColour(juce::ComboBox::outlineColourId).withAlpha(box.isEnabled() ? 1.0f : 0.5f));
        g.drawRect(r, 2.0f);
        // A blocky down chevron.
        const float cx = w - 12.0f, cy = h * 0.5f;
        juce::Path p;
        p.addTriangle(cx - 4.0f, cy - 2.0f, cx + 4.0f, cy - 2.0f, cx, cy + 3.0f);
        g.setColour(box.findColour(juce::ComboBox::arrowColourId).withAlpha(box.isEnabled() ? 1.0f : 0.5f));
        g.fillPath(p);
    }

    void drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&,
                              bool over, bool down) override {
        auto r = b.getLocalBounds().toFloat();
        // A toggled-on button (an active tab) inverts to a solid light-blue fill.
        if (b.getToggleState())
            g.setColour(juce::Colour(kFg));
        else
            g.setColour(juce::Colour(kPanel).brighter(down ? 0.12f : (over ? 0.06f : 0.0f)));
        g.fillRect(r);
        g.setColour(juce::Colour(b.isEnabled() ? kFg : kDim));
        g.drawRect(r, 2.0f);
    }

    // A blocky titled box for sections and their nested sub-sections. The title
    // sits over the top edge, its background cleared so the line breaks around it.
    void drawGroupComponentOutline(juce::Graphics& g, int w, int h, const juce::String& text,
                                   const juce::Justification&, juce::GroupComponent&) override {
        const float titleH = 16.0f;
        auto box = juce::Rectangle<float>(1.0f, titleH * 0.5f, w - 2.0f, h - titleH * 0.5f - 1.0f);
        g.setColour(juce::Colour(kBg).darker(0.12f));  // subtle inset panel for depth
        g.fillRect(box);
        g.setColour(juce::Colour(kFg));
        g.drawRect(box, 2.0f);

        if (text.isNotEmpty()) {
            auto f = mono(13.0f, true);
            g.setFont(f);
            // Monospaced, so glyph advance is a steady fraction of the height.
            const float tw = text.length() * f.getHeight() * 0.62f + 10.0f;
            auto title = juce::Rectangle<float>(10.0f, 0.0f, tw, titleH);
            g.setColour(juce::Colour(kBg));  // break the border behind the title
            g.fillRect(title);
            g.setColour(juce::Colour(kHot));
            g.drawText(text, title.getSmallestIntegerContainer(), juce::Justification::centred, false);
        }
    }
};
