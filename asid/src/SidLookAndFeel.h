// A Commodore 64 flavoured look for the ASID editor: the classic blue screen,
// light-blue foreground, blocky knobs and toggles, and a monospaced (terminal)
// typeface. This only overrides drawing, never behaviour, so it drops on and off
// cleanly. The SID heritage of the SidStation is the point.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>

#include "BinaryData.h"  // embedded SidStationC64.ttf (the PETSCII pixel font)

class SidLookAndFeel : public juce::LookAndFeel_V4 {
public:
    // C64 screen palette.
    static constexpr juce::uint32 kBg    = 0xff463ea4;  // screen blue (darker shade)
    static constexpr juce::uint32 kPanel = 0xff383382;  // slightly darker inset
    static constexpr juce::uint32 kFg    = 0xff8781e3;  // light blue (text, outlines, border)
    static constexpr juce::uint32 kDim   = 0xff6461bd;  // dimmed light blue (disabled)
    static constexpr juce::uint32 kHot   = 0xffffffff;  // white highlight (pointers, text)
    static constexpr juce::uint32 kAccent = 0xff3cb8a6; // teal (Voice 1) default accent

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
        setColour(juce::TextButton::textColourOnId, juce::Colour(kBg));  // dark text on the light active fill
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

    // The active/value colour, set per plugin instance from its SID voice so the
    // three windows are colour-coded. Defaults to the base accent.
    juce::Colour accent{juce::Colour(kAccent)};
    void setAccent(juce::Colour c) { accent = c; }

    // The embedded C64 PETSCII pixel typeface, loaded once. This is the SID's own
    // machine's screen font, so all GUI text is in it.
    static const juce::Typeface::Ptr& c64Typeface() {
        static juce::Typeface::Ptr tf = juce::Typeface::createSystemTypefaceFor(
            BinaryData::SidStationC64_ttf, BinaryData::SidStationC64_ttfSize);
        return tf;
    }

    // One uniform text size for the whole GUI (labels, section titles, buttons, the
    // product title). Twice the 8 px source cell, so the pixels stay crisp. The C64
    // screen shows every character at one size, and we match that.
    static constexpr float kTextPx = 16.0f;

    // A single-weight, single-size pixel font: the height and bold arguments are
    // ignored so every call renders at kTextPx (kept for call-site compatibility).
    static juce::Font mono(float = kTextPx, bool = false) {
        if (auto tf = c64Typeface())
            return juce::Font(juce::FontOptions().withTypeface(tf).withHeight(kTextPx));
        return juce::Font(juce::FontOptions()
                              .withName(juce::Font::getDefaultMonospacedFontName())
                              .withHeight(kTextPx));
    }

    juce::Font getLabelFont(juce::Label& l) override { return mono(l.getFont().getHeight()); }
    juce::Font getComboBoxFont(juce::ComboBox&) override { return mono(14.0f); }
    juce::Font getPopupMenuFont() override { return mono(14.0f); }
    juce::Font getTextButtonFont(juce::TextButton&, int) override { return mono(14.0f); }

    // Pixel knob: a ring of square "LED" ticks lit up to the value, a square body
    // block, and a bright square marker at the value angle. All squares (snapped to
    // whole pixels), so it reads at the same resolution as the font, not smooth.
    void drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h,
                          float pos, float startAngle, float endAngle, juce::Slider& s) override {
        const bool on = s.isEnabled();
        auto area = juce::Rectangle<int>(x, y, w, h).toFloat().reduced(3.0f);
        const float size = juce::jmin(area.getWidth(), area.getHeight());
        const auto c = area.getCentre();
        const float radius = size * 0.5f;

        // A "sidBipolar" control (centre default, e.g. tune / pulse width) lights
        // out from the 12-o'clock centre; others fill from the start.
        const bool bipolar = static_cast<bool>(s.getProperties().getWithDefault("sidBipolar", false));
        const float ctr = bipolar ? 0.5f : 0.0f;

        // Draw a whole-pixel square centred on a point.
        auto pixelSquare = [&g](juce::Point<float> p, float side) {
            g.fillRect(juce::Rectangle<int>((int) std::round(p.x - side * 0.5f),
                                            (int) std::round(p.y - side * 0.5f),
                                            (int) side, (int) side));
        };

        const int ticks = 11;
        const float tickSide = juce::jmax(4.0f, std::floor(size * 0.14f));
        const float ringR = radius - tickSide * 0.5f;
        const auto litCol = on ? accent : juce::Colour(kDim);
        const auto dimCol = juce::Colour(kBg).darker(0.45f);
        for (int i = 0; i < ticks; ++i) {
            const float f = (float) i / (ticks - 1);
            const float ang = startAngle + f * (endAngle - startAngle);
            const juce::Point<float> p(c.x + std::sin(ang) * ringR, c.y - std::cos(ang) * ringR);
            const bool lit = f >= juce::jmin(ctr, pos) && f <= juce::jmax(ctr, pos);
            g.setColour(lit ? litCol : dimCol);
            pixelSquare(p, tickSide);
        }

        // Body block and the value marker riding its edge at the value angle.
        const float bodySide = std::floor(size * 0.42f);
        g.setColour(on ? fieldFill() : juce::Colour(kDim).darker(0.3f));
        pixelSquare(c, bodySide);
        const float ang = startAngle + pos * (endAngle - startAngle);
        const juce::Point<float> marker(c.x + std::sin(ang) * (bodySide * 0.5f),
                                        c.y - std::cos(ang) * (bodySide * 0.5f));
        g.setColour(juce::Colour(on ? kHot : kDim));
        pixelSquare(marker, tickSide);
    }

    // Value bar: the whole control is one field-height block. The unfilled part is
    // a field (dark, kFg text) and the filled part is the accent (kBg text), so it
    // reads exactly like an off/on button split at the value. The slider's name is
    // drawn inside, two-toned across the split. A "sidBipolar" bar fills out from
    // the centre. Value-while-dragging still shows in the popup bubble.
    void drawLinearSlider(juce::Graphics& g, int, int, int, int, float, float, float,
                          juce::Slider::SliderStyle, juce::Slider& s) override {
        const bool on = s.isEnabled();
        const bool bipolar = static_cast<bool>(s.getProperties().getWithDefault("sidBipolar", false));
        // Fill the whole component (JUCE insets the linear-slider track by a thumb
        // margin; we want the bar flush with the buttons and toggles beside it).
        const auto bar = s.getLocalBounds();
        const int x = bar.getX(), y = bar.getY(), w = bar.getWidth(), h = bar.getHeight();
        const float frac = (float) s.valueToProportionOfLength(s.getValue());
        const float ctr = bipolar ? 0.5f : 0.0f;
        const int fx0 = x + juce::roundToInt(w * juce::jmin(ctr, frac));
        const int fx1 = x + juce::roundToInt(w * juce::jmax(ctr, frac));
        const auto filled = juce::Rectangle<int>(fx0, y, fx1 - fx0, h);

        g.setColour(on ? fieldFill() : juce::Colour(kDim).darker(0.3f));  // unfilled (off) part
        g.fillRect(bar);
        g.setColour(on ? accent : juce::Colour(kDim));                    // filled (on) part
        g.fillRect(filled);

        const auto name = s.getName();
        if (name.isNotEmpty()) {
            g.setFont(mono());
            const auto txt = bar.reduced(4, 0);
            g.setColour(juce::Colour(on ? kFg : kDim));  // text over the unfilled part
            g.drawText(name, txt, juce::Justification::centred, false);
            juce::Graphics::ScopedSaveState clip(g);     // over the filled part: inverted
            g.reduceClipRegion(filled);
            g.setColour(juce::Colour(on ? kBg : kDim).withMultipliedAlpha(on ? 1.0f : 0.6f));
            g.drawText(name, txt, juce::Justification::centred, false);
        }
    }

    // Shared height for checkboxes, combo boxes, number boxes and buttons, so a
    // row of mixed controls lines up.
    static constexpr float kFieldH = 24.0f;
    static constexpr float kCaptionH = 16.0f;  // one size for all caption labels

    // Two backgrounds only (keeping it Commodore-spare): the screen (kBg) and one
    // darker fill for every field, input and button. Active states use the accent.
    static juce::Colour fieldFill() { return juce::Colour(kBg).darker(0.42f); }

    // Toggle drawn as a labelled button: it fills with the accent colour when on
    // (inverting its text), like the tab buttons, so on/off controls have the same
    // weight as everything else. "sidHighlight" marks the instance's own filter
    // voice with a brighter fill. An empty label gives a plain on/off cell (the
    // wavetable step matrix).
    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& b, bool over, bool down) override {
        const bool on = b.getToggleState();
        const bool en = b.isEnabled();
        const bool hi = static_cast<bool>(b.getProperties().getWithDefault("sidHighlight", false));
        auto r = b.getLocalBounds().toFloat();
        if (on)      g.setColour(en ? accent : juce::Colour(kDim));  // accent when active
        else if (hi) g.setColour(fieldFill().brighter(0.45f));       // own filter voice
        else         g.setColour(fieldFill().brighter(down ? 0.20f : (over ? 0.10f : 0.0f)));
        g.fillRect(r);
        if (b.getButtonText().isNotEmpty()) {
            g.setColour(juce::Colour(on ? kBg : (hi ? kHot : (en ? kFg : kDim))));
            g.setFont(mono());
            g.drawFittedText(b.getButtonText(), r.toNearestInt().reduced(2, 0),
                             juce::Justification::centred, 1, 1.0f);
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
            g.setColour(bg.isTransparent() ? fieldFill() : bg);
            g.fillRect(bounds);
        }
        if (!l.isBeingEdited()) {
            const auto font = mono();  // one uniform size for every label
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
        g.setColour(fieldFill());
        g.fillRect(r);
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
            g.setColour(fieldFill().brighter(down ? 0.20f : (over ? 0.10f : 0.0f)));
        g.fillRect(r);
    }

    // Text buttons (tabs, Init, Panic, the arp steppers) draw their own label so it
    // uses the pixel font and fills the button width (the default inset clipped the
    // one-character steppers to an ellipsis).
    void drawButtonText(juce::Graphics& g, juce::TextButton& b, bool, bool) override {
        const bool on = b.getToggleState();
        g.setFont(mono());
        g.setColour(b.findColour(on ? juce::TextButton::textColourOnId : juce::TextButton::textColourOffId)
                        .withMultipliedAlpha(b.isEnabled() ? 1.0f : 0.5f));
        g.drawFittedText(b.getButtonText(), b.getLocalBounds().reduced(3, 0),
                         juce::Justification::centred, 1, 1.0f);
    }

    // A section is just its title on the screen background; the controls below are
    // grouped by spacing alone. No panel fill or outline (fewer shades, cleaner).
    void drawGroupComponentOutline(juce::Graphics& g, int w, int h, const juce::String& text,
                                   const juce::Justification&, juce::GroupComponent&) override {
        juce::ignoreUnused(h);
        if (text.isEmpty()) return;
        const float titleH = kTextPx + 2.0f;
        g.setFont(mono());
        g.setColour(juce::Colour(kHot));
        g.drawText(text, juce::Rectangle<float>(6.0f, 0.0f, w - 12.0f, titleH).getSmallestIntegerContainer(),
                   juce::Justification::centredLeft, false);
    }
};
