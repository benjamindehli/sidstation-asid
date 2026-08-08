// Standalone window chrome: paint the JUCE title bar dark
// (#191a1b) and draw the minimise/close glyphs in a neutral grey instead of the
// default yellow/red. Only the title bar and its buttons are overridden; the rest
// keeps the V4 look. Applied to the standalone DocumentWindow only (a DAW owns
// its own title bar) via each editor's parentHierarchyChanged().
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

struct WindowChromeLnF : juce::LookAndFeel_V4 {
    void drawDocumentWindowTitleBar(juce::DocumentWindow& window, juce::Graphics& g, int w, int h,
                                    int titleSpaceX, int titleSpaceW, const juce::Image* icon,
                                    bool drawTitleTextOnLeft) override {
        juce::ignoreUnused(w, icon);
        g.fillAll(juce::Colour(0xff191a1b));
        g.setColour(juce::Colour(0xffd8d9da));
        g.setFont(juce::Font(juce::FontOptions().withHeight(h * 0.5f)));
        g.drawText(window.getName(), titleSpaceX, 0, titleSpaceW, h,
                   drawTitleTextOnLeft ? juce::Justification::centredLeft : juce::Justification::centred, true);
    }
    juce::Button* createDocumentWindowButton(int buttonType) override {
        juce::Path shape;
        const float t = 0.09f;              // stroke thickness (fraction of the button)
        const float a = 0.32f, b = 0.68f;   // glyph inset, so it stays small and centred
        juce::String name;
        if (buttonType == juce::DocumentWindow::closeButton) {
            name = "close";
            shape.addLineSegment({a, a, b, b}, t);
            shape.addLineSegment({b, a, a, b}, t);
        } else if (buttonType == juce::DocumentWindow::minimiseButton) {
            name = "minimise";
            shape.addLineSegment({a, 0.5f, b, 0.5f}, t);
        } else {
            name = "maximise";
            shape.addLineSegment({a, a, b, a}, t);
            shape.addLineSegment({b, a, b, b}, t);
            shape.addLineSegment({b, b, a, b}, t);
            shape.addLineSegment({a, b, a, a}, t);
        }
        // Pin the path bounds to the whole button so the glyph is not scaled up to
        // fill it (these two points draw nothing, they just set the bounding box).
        shape.startNewSubPath(0.0f, 0.0f);
        shape.startNewSubPath(1.0f, 1.0f);
        const juce::Colour col(0xffbfc1c4);  // neutral grey, no traffic-light hues
        auto* btn = new juce::ShapeButton(name, col, col.brighter(0.4f), col.brighter(0.8f));
        btn->setShape(shape, false, true, false);
        return btn;
    }
};

inline WindowChromeLnF& windowChromeLnF() { static WindowChromeLnF lnf; return lnf; }
