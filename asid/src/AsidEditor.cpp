#include "AsidEditor.h"

#include "BinaryData.h"
#include "WindowChromeLnF.h"

namespace {

// Distinct accent per SID voice, so the three plugin windows are colour-coded.
juce::Colour voiceColour(int v) {
  switch (v) {
  case 1:
    return juce::Colour(0xffe36d7a); // Voice 2 - red/pink
  case 2:
    return juce::Colour(0xffe3c681); // Voice 3 - sand
  default:
    return juce::Colour(0xff3cb8a6); // Voice 1 - teal
  }
}
// The i-th of n equal columns spanning a row, for spreading controls to fill
// it.
juce::Rectangle<int> colOf(juce::Rectangle<int> row, int i, int n) {
  const int w = row.getWidth() / n;
  return {row.getX() + i * w, row.getY(), w, row.getHeight()};
}
// One uniform control height for every bar, button, toggle and menu.
constexpr int kCtrlH = 30;
// The max width a single control fills, so a control alone in a wide column
// does not stretch absurdly (it still shrinks to fit narrow columns).
constexpr int kCtrlWMax = 200;
// A value bar centred in a column (the label is drawn inside the bar now).
void knobInCol(juce::Rectangle<int> col, juce::Slider &s) {
  s.setBounds(col.withSizeKeepingCentre(
      juce::jmin(col.getWidth() - 8, kCtrlWMax), kCtrlH));
}
// A toggle button centred in a column both ways.
void toggleInCol(juce::Rectangle<int> col, juce::ToggleButton &b) {
  b.setBounds(col.withSizeKeepingCentre(
      juce::jmin(col.getWidth() - 8, kCtrlWMax), kCtrlH));
}

} // namespace

void AsidEditor::setupKnob(juce::Component &parent, juce::Slider &s,
                           const juce::String &name,
                           const juce::String &paramId,
                           std::unique_ptr<SliderAtt> &att) {
  // A horizontal value bar with its name drawn inside (see drawLinearSlider),
  // so knobs, buttons and menus are all one uniform-height control. No caption
  // label.
  s.setSliderStyle(juce::Slider::LinearHorizontal);
  s.setTextBoxStyle(juce::Slider::NoTextBox, false, 0,
                    0); // value shown in a popup bubble instead
  s.setPopupDisplayEnabled(true, false, this); // value bubble while dragging
  s.setName(name);                             // drawn inside the bar
  s.addMouseListener(&sliderHover,
                     false); // repaint on move for the hover preview
  parent.addAndMakeVisible(s);
  att = std::make_unique<SliderAtt>(state, paramId, s);
  if (auto *p =
          state.getParameter(paramId)) // double-click resets to the default
    s.setDoubleClickReturnValue(
        true, p->getNormalisableRange().convertFrom0to1(p->getDefaultValue()));
}

void AsidEditor::setupSwitch(juce::ToggleButton &segA, juce::ToggleButton &segB,
                             const juce::String &labelA,
                             const juce::String &labelB,
                             const juce::String &paramId) {
  segA.setButtonText(labelA);
  segB.setButtonText(labelB);
  for (auto *b : {&segA, &segB}) {
    b->setClickingTogglesState(
        false); // the parameter drives the state, not the click
    oscPage.addAndMakeVisible(*b);
  }
  // A 2-value switch: clicking either segment flips to the other value. State
  // syncs from the parameter in updateEnablement (and immediately here).
  auto flip = [this, paramId, &segA, &segB] {
    auto *rp = state.getRawParameterValue(paramId);
    const int next = (rp && juce::roundToInt(rp->load()) == 0) ? 1 : 0;
    if (auto *p = state.getParameter(paramId)) {
      p->beginChangeGesture();
      p->setValueNotifyingHost(p->convertTo0to1((float)next));
      p->endChangeGesture();
    }
    segA.setToggleState(next == 0, juce::dontSendNotification);
    segB.setToggleState(next == 1, juce::dontSendNotification);
    segA.repaint();
    segB.repaint();
  };
  segA.onClick = flip;
  segB.onClick = flip;
}

// Resolves the parameters the enablement pass reads, once. All the id building
// happens here rather than at 10 Hz.
void AsidEditor::buildParamRefs() {
  auto at = [this](const juce::String &id) {
    auto *p = state.getRawParameterValue(id);
    jassert(p != nullptr); // id typo, or a parameter missing from makeLayout()
    return p;
  };
  pr.asidVoice = at("asidVoice");
  pr.sustain = at("sustain");
  pr.waveTri = at("waveTri");
  pr.wavePulse = at("wavePulse");
  pr.waveNoise = at("waveNoise");
  pr.portaTime = at("portaTime");
  pr.portaTrigger = at("portaTrigger");
  pr.portaType = at("portaType");
  pr.wtOn = at("wtOn");
  pr.wtTempoSync = at("wtTempoSync");
  pr.wtLength = at("wtLength");
  pr.wtLoop = at("wtLoop");
  for (int i = 0; i < AsidProcessor::kWtSteps; ++i)
    pr.wtNoise[i] = at("wtNoise" + juce::String(i));
}

void AsidEditor::setupLfo(juce::Component &parent, LfoControls &u,
                          const juce::String &prefix) {
  u.prefix = prefix;
  u.onPtr = state.getRawParameterValue(prefix + "On");
  u.shapePtr = state.getRawParameterValue(prefix + "Shape");
  u.syncPtr = state.getRawParameterValue(prefix + "Sync");

  // Shape doubles as on/off: "Off" (id 1) disables the LFO, each waveform (id
  // 2+) enables it and selects that shape. Managed by hand since one control
  // drives two parameters (On and Shape).
  parent.addAndMakeVisible(u.shapeBox);
  u.shapeBox.addItem("Off", 1);
  for (const char *s :
       {"Sine", "Triangle", "Saw Up", "Saw Down", "Square", "S&H", "Random"})
    u.shapeBox.addItem(s, u.shapeBox.getNumItems() + 1);
  u.shapeBox.onChange = [this, &u] {
    const int sel = u.shapeBox.getSelectedId();
    const bool on = sel > 1;
    auto set = [this](const juce::String &id, float raw) {
      if (auto *p = state.getParameter(id)) {
        p->beginChangeGesture();
        p->setValueNotifyingHost(p->convertTo0to1(raw));
        p->endChangeGesture();
      }
    };
    set(u.prefix + "On", on ? 1.0f : 0.0f);
    if (on)
      set(u.prefix + "Shape", (float)(sel - 2)); // 0-based shape index
  };

  parent.addAndMakeVisible(u.syncButton);
  u.syncAtt = std::make_unique<ButtonAtt>(state, prefix + "Sync", u.syncButton);
  parent.addAndMakeVisible(u.wheelButton);
  u.wheelAtt =
      std::make_unique<ButtonAtt>(state, prefix + "Wheel", u.wheelButton);

  // Rate knob: set up the knob shell, then bind it (free Hz by default, or the
  // stepped tempo division when Tempo Sync is on - see configureRateKnob).
  setupKnob(parent, u.rateKnob, "Rate", prefix + "Rate", u.rateAtt);
  configureRateKnob(u, false);
  setupKnob(parent, u.depthKnob, "Depth", prefix + "Depth", u.depthAtt);
  setupKnob(parent, u.delayKnob, "Delay", prefix + "Delay", u.delayAtt);
}

void AsidEditor::configureRateKnob(LfoControls &u, bool synced) {
  u.rateAtt.reset();
  if (synced) { // bind to the tempo Division choice: stepped, shows 1/4 etc.
    u.rateAtt =
        std::make_unique<SliderAtt>(state, u.prefix + "Div", u.rateKnob);
  } else { // bind to the free Hz rate, shown to 3 decimals
    u.rateAtt =
        std::make_unique<SliderAtt>(state, u.prefix + "Rate", u.rateKnob);
    u.rateKnob.textFromValueFunction = [](double v) {
      return juce::String(v, 3);
    };
  }
  u.rateKnob.updateText();
  u.rateMode = synced ? 1 : 0;
}

void AsidEditor::configureWtSpeedKnob(bool synced) {
  wtSpeedAtt.reset();
  // Keeps the name "Rate" in both modes, like the LFO rate knobs: the Sync
  // toggle beside it is what says whether the value is frames or a division.
  const juce::String id = synced ? "wtDiv" : "wtSpeed";
  wtSpeedAtt = std::make_unique<SliderAtt>(state, id, wtSpeedKnob);
  if (auto *p = state.getParameter(id)) // double-click resets to this default
    wtSpeedKnob.setDoubleClickReturnValue(
        true, p->getNormalisableRange().convertFrom0to1(p->getDefaultValue()));
  wtSpeedKnob.updateText();
  wtSpeedMode = synced ? 1 : 0;
}

void AsidEditor::layoutLfo(LfoControls &u, juce::Rectangle<int> area) {
  // Two rows of three 208px cells on the standard grid (x = 0, 232, 464):
  //   Shape | Rate | Depth
  //   Delay | Sync | Mod Wheel   (Sync sits under Rate, Mod Wheel under Depth).
  const int H = kCtrlH, vg = 8;
  auto r1 = area.removeFromTop(H);
  area.removeFromTop(vg);
  auto r2 = area.removeFromTop(H);
  auto cell = [](juce::Rectangle<int> row, int col) {
    return juce::Rectangle<int>(row.getX() + col * 232, row.getY(), 208,
                                row.getHeight());
  };
  u.shapeBox.setBounds(cell(r1, 0));
  u.rateKnob.setBounds(cell(r1, 1));
  u.depthKnob.setBounds(cell(r1, 2));
  u.delayKnob.setBounds(cell(r2, 0));
  u.syncButton.setBounds(cell(r2, 1));
  u.wheelButton.setBounds(cell(r2, 2));
}

AsidEditor::AsidEditor(AsidProcessor &p)
    : juce::AudioProcessorEditor(p), proc(p), state(p.state()) {
  buildParamRefs(); // before currentVoice() or anything else reads a parameter
  setLookAndFeel(&laf);
  laf.setAccent(voiceColour(currentVoice())); // colour-code this voice's window
  logo = juce::ImageFileFormat::loadFrom(
      BinaryData::DehliMusikkLogo_PETSCII_png,
      (size_t)BinaryData::DehliMusikkLogo_PETSCII_pngSize);
  // Optional decorative overlays (embedded if the PNGs are present in assets/).
  // Looked up by name so missing files just leave an invalid, unused image.
  auto loadImage = [](const juce::String &resName) {
    int sz = 0;
    if (const char *d = BinaryData::getNamedResource(resName.toRawUTF8(), sz))
      return juce::ImageFileFormat::loadFrom(d, (size_t)sz);
    return juce::Image();
  };
  overlayShared = loadImage("Overlay_png");
  for (int i = 0; i < 3; ++i)
    overlay[i] = loadImage("OverlayVoice" + juce::String(i + 1) + "_png");
  overlayComp.pick = [this]() -> const juce::Image * {
    const int v = currentVoice();
    if (v >= 0 && v < 3 && overlay[v].isValid())
      return &overlay[v];
    return overlayShared.isValid() ? &overlayShared : nullptr;
  };
  title.setFont(SidLookAndFeel::mono(26.0f, true)); // match the VOICE N size
  title.setColour(juce::Label::textColourId,
                  juce::Colour(SidLookAndFeel::kHot));
  title.setJustificationType(juce::Justification::centred);
  addAndMakeVisible(title);

  addAndMakeVisible(oscPage);
  addChildComponent(ampModPage); // hidden until its tab is chosen
  addChildComponent(sharedPage);
  addChildComponent(wtPage);

  // Tab bar: a radio row of buttons that switch which page shows.
  auto tab = [this](juce::TextButton &b, int index) {
    b.setClickingTogglesState(true);
    b.setRadioGroupId(1);
    b.setColour(juce::TextButton::buttonOnColourId,
                juce::Colour(SidLookAndFeel::kFg));
    b.setColour(juce::TextButton::textColourOnId,
                juce::Colour(SidLookAndFeel::kBg));
    b.onClick = [this, index] { setTab(index); };
    addAndMakeVisible(b);
  };
  tab(oscTabBtn, 0);    // VOICE
  tab(waveTabBtn, 1);   // WAVETABLE
  tab(ampModTabBtn, 2); // MODULATION
  tab(sharedTabBtn, 3); // GLOBAL

  // Preset bar: prev/next to browse, an editable menu (type a name to save
  // under), and Save. Presets hold the voice's sound only (see AsidProcessor).
  addAndMakeVisible(presetBox);
  presetBox.setEditableText(true);
  presetBox.setTextWhenNothingSelected("(unsaved)");
  presetBox.onChange = [this] {
    if (presetBox.getSelectedId() >=
        1) // a listed preset chosen (not just typed text)
      proc.loadPreset(presetBox.getText());
  };
  addAndMakeVisible(presetPrevBtn);
  presetPrevBtn.onClick = [this] { cyclePreset(-1); };
  addAndMakeVisible(presetNextBtn);
  presetNextBtn.onClick = [this] { cyclePreset(1); };
  addAndMakeVisible(presetSaveBtn);
  presetSaveBtn.onClick = [this] {
    // Show the name it was stored under, not what was typed: savePreset
    // sanitises the name into a legal filename, so the two can differ.
    if (proc.savePreset(presetBox.getText()))
      refreshPresets(proc.currentPreset());
  };
  addAndMakeVisible(presetDeleteBtn);
  presetDeleteBtn.onClick = [this] {
    const auto name = presetBox.getText().trim();
    if (proc.presetNames().contains(name)) {
      proc.deletePreset(name);
      refreshPresets();
    }
  };
  // "Tips" toggle at the start of the bar: turns the hover hints on and off
  // (the value popups a slider shows while turning are separate and stay).
  // Default on.
  addAndMakeVisible(tooltipToggle);
  tooltipToggle.setClickingTogglesState(true);
  tooltipToggle.setToggleState(proc.showTooltips(), juce::dontSendNotification);
  hints.active = proc.showTooltips();
  tooltipToggle.onClick = [this] {
    hints.active = tooltipToggle.getToggleState();
    proc.setShowTooltips(hints.active); // persist with the plugin state
    if (!hints.active)
      hints.bubble.setVisible(false); // dismiss any showing hint
  };
  // The bar sits on the dark strip outside the C64 screen, so give its controls
  // a neutral look rather than the Commodore styling: an almost-grayscale
  // palette with the same faint blue lean as the #191a1b background (each
  // channel is green = red + 1, blue = red + 2).
  auto greyTint = [](int r) {
    return juce::Colour((juce::uint8)r, (juce::uint8)(r + 1),
                        (juce::uint8)(r + 2));
  };
  barLnF.setColourScheme({
      greyTint(25),  // window background: matches the bar
      greyTint(30),  // widget background: button / combo fill
      greyTint(26),  // menu background
      greyTint(66),  // outline
      greyTint(198), // default text
      greyTint(58),  // default fill (accent)
      greyTint(236), // highlighted text
      greyTint(46),  // highlighted fill (hover / selection)
      greyTint(198), // menu text
  });
  for (auto *c : {&presetPrevBtn, &presetNextBtn, &presetSaveBtn,
                  &presetDeleteBtn, &tooltipToggle})
    c->setLookAndFeel(&barLnF);
  presetBox.setLookAndFeel(&barLnF);
  // A clearly brighter fill so the "Tips" on-state is obvious in the grey bar.
  tooltipToggle.setColour(juce::TextButton::buttonOnColourId, greyTint(140));
  tooltipToggle.setColour(juce::TextButton::textColourOnId, greyTint(20));
  refreshPresets(proc.currentPreset());

  // Group boxes go into their page first, so the frames sit behind the
  // controls.
  auto group = [](juce::Component &page, juce::GroupComponent &g,
                  const juce::String &t) {
    g.setText(t);
    g.setInterceptsMouseClicks(false, false); // decorative frame only
    page.addAndMakeVisible(g);
  };
  group(oscPage, oscGroup, "OSCILLATOR");
  group(oscPage, glideGroup, "TUNING");
  group(oscPage, ampGroup, "AMP ENVELOPE");
  group(ampModPage, pitchModGroup, "PITCH MODULATION");
  group(ampModPage, pwModGroup, "PULSE WIDTH MODULATION");
  group(sharedPage, filterGroup, "FILTER");
  group(sharedPage, cutModGroup, "CUTOFF MODULATION");
  group(sharedPage, masterGroup, "MASTER");
  group(wtPage, wtConfigGroup, "WAVETABLE");
  group(wtPage, wtStepsGroup, "STEPS");

  // Global header (above the tabs): MIDI out and SID voice apply to the whole
  // instance, so they sit outside the tabbed pages.
  addAndMakeVisible(outLabel);
  addAndMakeVisible(outputBox);
  addAndMakeVisible(refreshButton);
  // Tempo field (replaces the old SID Voice selector; voice moved to the
  // top-right switch). A hidden slider holds the "bpm" param; bpmField is the
  // visible number.
  addAndMakeVisible(bpmLabel);
  addChildComponent(bpmSlider);
  bpmAtt = std::make_unique<SliderAtt>(state, "bpm", bpmSlider);
  bpmField.setJustificationType(juce::Justification::centred);
  bpmField.setFont(juce::Font(juce::FontOptions().withHeight(14.0f)));
  bpmField.getProperties().set("sidField", true); // draw as a filled field
  bpmField.setColour(juce::Label::textColourId,
                     juce::Colour(SidLookAndFeel::kHot));
  bpmField.setEditable(
      true, true, false); // standalone: click to type (toggled off in a DAW)
  addAndMakeVisible(bpmField);
  bpmField.onTextChange = [this] {
    bpmSlider.setValue(juce::jlimit(20, 300, bpmField.getText().getIntValue()),
                       juce::sendNotificationSync);
  };
  bpmSlider.onValueChange = [this] {
    if (proc.hostBpm() <= 0.0 && !bpmField.isBeingEdited())
      bpmField.setText(juce::String((int)bpmSlider.getValue()),
                       juce::dontSendNotification);
  };

  // Voice selector, top right: three coloured cells, click to switch this
  // instance's SID voice. Recolours the whole window (see updateEnablement).
  for (int i = 0; i < 3; ++i)
    voiceSwitch.colours[i] = voiceColour(i);
  voiceSwitch.selected = currentVoice();
  voiceSwitch.onSelect = [this](int v) {
    if (auto *vp = state.getParameter("asidVoice")) {
      vp->beginChangeGesture();
      vp->setValueNotifyingHost(vp->convertTo0to1((float)v));
      vp->endChangeGesture();
    }
    voiceSwitch.selected =
        v; // light the cell now; updateEnablement recolours the rest
    voiceSwitch.repaint();
  };
  // Hover an in-use voice cell: show a bubble hint (BubbleMessageComponent,
  // which works where TooltipWindow did not). Leaving the cell hides it.
  voiceSwitch.onHover = [this](int cell) {
    if (cell >= 0 && cell < 3 && voiceSwitch.usedByOther[cell]) {
      const auto sw = voiceSwitch.getBounds();
      const int w = sw.getWidth() / 3;
      const juce::Rectangle<int> cellRect(sw.getX() + cell * w, sw.getY(), w,
                                          sw.getHeight());
      juce::AttributedString msg("In use by another instance");
      msg.setColour(juce::Colour(SidLookAndFeel::kHot));
      msg.setJustification(juce::Justification::centred);
      voiceBubble.showAt(cellRect, msg, 0, false,
                         false); // stays until the mouse leaves
    } else {
      voiceBubble.setVisible(false);
    }
  };
  addAndMakeVisible(voiceSwitch);
  voiceCaption.setJustificationType(juce::Justification::centredRight);
  voiceCaption.setColour(juce::Label::textColourId,
                         juce::Colour(SidLookAndFeel::kHot)); // white
  voiceCaption.setFont(SidLookAndFeel::mono(24.0f, true));
  addAndMakeVisible(voiceCaption);
  addAndMakeVisible(midiLoadLabel);
  addAndMakeVisible(modRateLabel);
  addAndMakeVisible(modRateBox);
  for (const char *r : {"Eco 25 Hz", "PAL 50 Hz", "NTSC 60 Hz", "HiFi 100 Hz"})
    modRateBox.addItem(r, modRateBox.getNumItems() + 1);
  modRateAtt = std::make_unique<ComboAtt>(state, "modRate", modRateBox);

  // ---- OSC page: Oscillator, Glide ---- (waveform toggles are self-labelled
  // buttons now, so no "Waveform" caption).
  for (auto *b :
       {&waveTriButton, &waveSawButton, &wavePulseButton, &waveNoiseButton})
    oscPage.addAndMakeVisible(*b);
  oscPage.addAndMakeVisible(syncButton);
  oscPage.addAndMakeVisible(ringButton);
  oscPage.addAndMakeVisible(testButton);

  outputBox.onChange = [this] {
    const int id = outputBox.getSelectedId();
    if (id >= 1 && id <= outDevices.size()) {
      proc.midi().openOutputByIdentifier(outDevices[id - 1].identifier);
      // Shared output: make every instance re-push its voice to the new device.
      AsidShared::get().outGeneration.fetch_add(1);
    }
  };
  refreshButton.onClick = [this] { refreshDevices(); };

  waveTriAtt = std::make_unique<ButtonAtt>(state, "waveTri", waveTriButton);
  waveSawAtt = std::make_unique<ButtonAtt>(state, "waveSaw", waveSawButton);
  wavePulseAtt =
      std::make_unique<ButtonAtt>(state, "wavePulse", wavePulseButton);
  waveNoiseAtt =
      std::make_unique<ButtonAtt>(state, "waveNoise", waveNoiseButton);

  setupKnob(oscPage, pwKnob, "Pulse Width", "pulseWidth", pwAtt);
  setupKnob(oscPage, coarseKnob, "Coarse", "coarse", coarseAtt);
  setupKnob(oscPage, fineKnob, "Fine", "fine", fineAtt);
  // These have a natural centre default, so their arcs fill out from the
  // centre.
  for (auto *k : {&pwKnob, &coarseKnob, &fineKnob})
    k->getProperties().set("sidBipolar", true);
  setupKnob(oscPage, bendKnob, "Bend", "pitchBendRange", bendAtt);
  syncAtt = std::make_unique<ButtonAtt>(state, "sync", syncButton);
  ringAtt = std::make_unique<ButtonAtt>(state, "ring", ringButton);
  testAtt = std::make_unique<ButtonAtt>(state, "test", testButton);

  setupKnob(oscPage, portaKnob, "Glide", "portaTime", portaAtt);
  setupSwitch(portaTrigBtns[0], portaTrigBtns[1], "Legato", "Always",
              "portaTrigger");
  setupSwitch(portaTypeBtns[0], portaTypeBtns[1], "Smooth", "Step",
              "portaType");

  // ---- AMP+MOD page: Amp envelope, Pitch Mod, PW Mod ----
  setupKnob(oscPage, attackKnob, "Attack", "attack", attackAtt);
  setupKnob(oscPage, decayKnob, "Decay", "decay", decayAtt);
  setupKnob(oscPage, sustainKnob, "Sustain", "sustain", sustainAtt);
  setupKnob(oscPage, releaseKnob, "Release", "release", releaseAtt);
  setupLfo(ampModPage, pitchLfoUi, "pitchLfo");
  setupLfo(ampModPage, pwLfoUi, "pwLfo");

  // ---- SHARED page: Filter (with Cutoff Mod) and Master ----
  // Filter Active: one checkbox per voice, all shared, so any instance can
  // route any voice. Filter Mode: LP/BP/HP, combinable.
  const char *voiceNames[3] = {"V1", "V2", "V3"}; // self-labelled, no caption
  const char *filtIds[3] = {"filt1", "filt2", "filt3"};
  for (int i = 0; i < 3; ++i) {
    filtButtons[i].setButtonText(voiceNames[i]);
    sharedPage.addAndMakeVisible(filtButtons[i]);
    filtAtts[i] =
        std::make_unique<ButtonAtt>(state, filtIds[i], filtButtons[i]);
  }
  const char *modeNames[3] = {"LP", "BP", "HP"};
  const char *modeIds[3] = {"modeLP", "modeBP", "modeHP"};
  for (int i = 0; i < 3; ++i) {
    modeButtons[i].setButtonText(modeNames[i]);
    sharedPage.addAndMakeVisible(modeButtons[i]);
    modeAtts[i] =
        std::make_unique<ButtonAtt>(state, modeIds[i], modeButtons[i]);
  }
  filtExtButton.setButtonText("Ext");
  sharedPage.addAndMakeVisible(filtExtButton);
  filtExtAtt = std::make_unique<ButtonAtt>(state, "filtExt", filtExtButton);
  setupKnob(sharedPage, cutoffKnob, "Cutoff", "cutoff", cutoffAtt);
  setupKnob(sharedPage, resKnob, "Resonance", "resonance", resAtt);
  setupLfo(sharedPage, cutLfoUi, "cutLfo");
  setupKnob(sharedPage, volumeKnob, "Volume", "volume", volumeAtt);
  setupKnob(sharedPage, latencyKnob, "Latency", "latency", latencyAtt);
  sharedPage.addAndMakeVisible(voice3offButton);
  voice3offAtt =
      std::make_unique<ButtonAtt>(state, "voice3off", voice3offButton);
  sharedPage.addAndMakeVisible(panicButton);
  panicButton.onClick = [this] { proc.panic(); };
  // Init resets the current patch, so it lives in the preset bar (top) with the
  // other patch actions, styled like them (neutral dark, outside the C64
  // screen).
  addAndMakeVisible(initButton);
  initButton.setLookAndFeel(&barLnF);
  initButton.onClick = [this] { proc.resetVoiceToDefault(); };

  // ---- WAVE page: wavetable config and the per-step rows ----
  wtPage.addAndMakeVisible(wtOnButton);
  wtOnAtt = std::make_unique<ButtonAtt>(state, "wtOn", wtOnButton);
  wtPage.addAndMakeVisible(wtTempoSyncButton);
  wtTempoSyncAtt =
      std::make_unique<ButtonAtt>(state, "wtTempoSync", wtTempoSyncButton);
  // Rate knob: set up the shell, then bind it (frames per step by default, or
  // the stepped tempo division when Sync is on - see configureWtSpeedKnob).
  setupKnob(wtPage, wtSpeedKnob, "Rate", "wtSpeed", wtSpeedAtt);
  configureWtSpeedKnob(false);
  setupKnob(wtPage, wtLengthKnob, "Length", "wtLength", wtLengthAtt);
  setupKnob(wtPage, wtLoopKnob, "Loop", "wtLoop", wtLoopAtt);
  const char *wtHeads[4] = {"Tri", "Saw", "Pul", "Noi"};
  const char *wtIds[4] = {"wtTri", "wtSaw", "wtPulse", "wtNoise"};
  for (int w = 0; w < 4; ++w) {
    wtWaveHead[w].setText(wtHeads[w], juce::dontSendNotification);
    wtWaveHead[w].setJustificationType(juce::Justification::centred);
    wtPage.addAndMakeVisible(wtWaveHead[w]);
  }
  for (auto *h :
       {&wtSyncHead, &wtRingHead, &wtTestHead, &wtPwHead, &wtArpHead}) {
    h->setJustificationType(juce::Justification::centred);
    wtPage.addAndMakeVisible(*h);
  }
  for (int i = 0; i < AsidProcessor::kWtSteps; ++i) {
    wtStepInd[i].number = i + 1;
    wtPage.addAndMakeVisible(wtStepInd[i]);
    for (int w = 0; w < 4; ++w) {
      wtPage.addAndMakeVisible(wtWaveTog[i][w]);
      wtWaveTogAtt[i][w] = std::make_unique<ButtonAtt>(
          state, juce::String(wtIds[w]) + juce::String(i), wtWaveTog[i][w]);
    }
    wtPage.addAndMakeVisible(wtSyncTog[i]);
    wtSyncAtt[i] = std::make_unique<ButtonAtt>(
        state, "wtSync" + juce::String(i), wtSyncTog[i]);
    wtPage.addAndMakeVisible(wtRingTog[i]);
    wtRingAtt[i] = std::make_unique<ButtonAtt>(
        state, "wtRing" + juce::String(i), wtRingTog[i]);
    wtPage.addAndMakeVisible(wtTestTog[i]);
    wtTestAtt[i] = std::make_unique<ButtonAtt>(
        state, "wtTest" + juce::String(i), wtTestTog[i]);
    wtPwKnob[i].setSliderStyle(juce::Slider::LinearHorizontal);
    wtPwKnob[i].setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    wtPwKnob[i].setPopupDisplayEnabled(true, false,
                                       this); // value bubble while dragging
    wtPwKnob[i].getProperties().set("sidBipolar",
                                    true); // fill from the centre default
    wtPwKnob[i].addMouseListener(&sliderHover, false);
    wtPage.addAndMakeVisible(wtPwKnob[i]);
    wtPwAtt[i] = std::make_unique<SliderAtt>(state, "wtPw" + juce::String(i),
                                             wtPwKnob[i]);
    if (auto *pwParam = state.getParameter(
            "wtPw" + juce::String(i))) // double-click resets to default
      wtPwKnob[i].setDoubleClickReturnValue(
          true, pwParam->getNormalisableRange().convertFrom0to1(
                    pwParam->getDefaultValue()));
    // Hidden slider: the value model / APVTS binding. UI is the field +
    // buttons.
    wtPage.addChildComponent(wtArpSlider[i]);
    wtArpAtt[i] = std::make_unique<SliderAtt>(state, "wtArp" + juce::String(i),
                                              wtArpSlider[i]);
    wtArpValue[i].setJustificationType(juce::Justification::centred);
    wtArpValue[i].setFont(juce::Font(juce::FontOptions().withHeight(13.0f)));
    wtArpValue[i].getProperties().set("sidField",
                                      true); // draw as a filled field
    wtArpValue[i].setColour(juce::Label::textColourId,
                            juce::Colour(SidLookAndFeel::kHot));
    wtPage.addAndMakeVisible(wtArpValue[i]);
    wtArpSlider[i].onValueChange = [this, i] {
      wtArpValue[i].setText(juce::String((int)wtArpSlider[i].getValue()),
                            juce::dontSendNotification);
    };
    wtArpValue[i].setText(juce::String((int)wtArpSlider[i].getValue()),
                          juce::dontSendNotification);
    wtArpDec[i].setButtonText("-");
    wtArpInc[i].setButtonText("+");
    wtPage.addAndMakeVisible(wtArpDec[i]);
    wtPage.addAndMakeVisible(wtArpInc[i]);
    wtArpDec[i].onClick = [this, i] {
      wtArpSlider[i].setValue(wtArpSlider[i].getValue() - 1,
                              juce::sendNotificationSync);
    };
    wtArpInc[i].onClick = [this, i] {
      wtArpSlider[i].setValue(wtArpSlider[i].getValue() + 1,
                              juce::sendNotificationSync);
    };
  }

  addAndMakeVisible(
      overlayComp); // added last so it draws over all the controls
  voiceBubble.setInterceptsMouseClicks(false, false);
  addChildComponent(
      voiceBubble); // above the overlay; shown on demand by the hover handler

  // Hover hints on the non-obvious controls (bubble sits above the overlay).
  hints.host = this;
  hints.bubble.setInterceptsMouseClicks(false, false);
  addChildComponent(hints.bubble);
  // Bubble side, chosen so edge controls do not clip off the window.
  const int above = juce::BubbleComponent::above,
            below = juce::BubbleComponent::below;
  const int left = juce::BubbleComponent::left,
            right = juce::BubbleComponent::right;

  // --- Oscillator ---
  hints.add(
      testButton,
      "Holds the oscillator in reset - silences the voice (SID TEST bit).",
      above);
  hints.add(syncButton, "Hard-sync this oscillator to the previous voice's.",
            "Disabled: hard sync has no effect on a noise-only voice.", above);
  hints.add(
      ringButton, "Ring-modulate this oscillator with the previous voice.",
      "Disabled: ring mod needs the Triangle wave (and not Noise).", above);
  const juce::String noiseOff =
      "Disabled: turn Noise off to use this waveform.";
  hints.add(waveTriButton, {}, noiseOff, right);
  hints.add(waveSawButton, {}, noiseOff);
  hints.add(wavePulseButton, {}, noiseOff, right);
  hints.add(pwKnob, "Pulse wave duty cycle (width).",
            "Disabled: turn the Pulse wave on to set its width.");

  // --- Tuning ---
  hints.add(coarseKnob, "Coarse tune, +/- 24 semitones.", right);
  hints.add(fineKnob, "Fine tune, +/- 50 cents.", right);
  hints.add(bendKnob, "Pitch bend range in semitones.");
  hints.add(portaKnob, "Portamento glide time (0 = off).");
  const juce::String glideOff = "Disabled: set a Glide time above zero first.";
  hints.add(portaTrigBtns[0], "Glide only between overlapping (legato) notes.",
            glideOff, left);
  hints.add(portaTrigBtns[1], "Glide on every note.", glideOff, left);
  hints.add(portaTypeBtns[0], "Smooth, continuous pitch glide.", glideOff,
            left);
  hints.add(portaTypeBtns[1], "Glide in semitone steps.", glideOff, left);

  // --- Amp envelope ---
  hints.add(attackKnob, "Amp envelope attack time.", right);
  hints.add(decayKnob, "Amp envelope decay time.",
            "Disabled: decay does nothing at full Sustain (15).");
  hints.add(sustainKnob, "Amp envelope sustain level.");
  hints.add(releaseKnob, "Amp envelope release time.", left);

  // --- Modulation LFOs (pitch / pulse width / cutoff) ---
  for (auto *u : {&pitchLfoUi, &pwLfoUi, &cutLfoUi}) {
    // The PW LFO also needs the Pulse wave, so its "why greyed" reason differs.
    const juce::String lfoOff =
        (u == &pwLfoUi) ? "Disabled: turn Pulse Width Modulation on (the Pulse "
                          "wave must be on)."
                        : "Disabled: turn this LFO on (choose a Shape) first.";
    const juce::String shapeOff =
        (u == &pwLfoUi)
            ? "Disabled: the Pulse wave must be on to modulate its width."
            : juce::String();
    hints.add(u->shapeBox, "LFO shape (Off turns it off).", shapeOff, right);
    hints.add(u->rateKnob, "LFO rate (Hz, or a tempo division when synced).",
              lfoOff);
    hints.add(u->depthKnob, "LFO depth.", lfoOff, left);
    hints.add(u->delayKnob, "LFO fade-in time after a note.", lfoOff, right);
    hints.add(u->syncButton,
              "Lock the rate to the host tempo (Rate becomes a note division).",
              lfoOff);
    hints.add(u->wheelButton,
              "The mod wheel scales the depth (Depth becomes the maximum).",
              lfoOff, left);
  }

  // --- Global: filter + master ---
  hints.add(filtExtButton, "Route the external audio input through the filter.",
            right);
  hints.add(cutoffKnob, "Filter cutoff frequency (shared).", left);
  hints.add(resKnob, "Filter resonance (shared).", left);
  hints.add(voice3offButton,
            "Silence voice 3 so it can drive ring/sync as a modulator.", right);
  hints.add(volumeKnob, "Master volume (shared).");
  hints.add(latencyKnob, "Output latency added to sends (ms).");
  hints.add(panicButton, "All-notes-off for every voice.", left);
  hints.add(initButton, "Reset this voice's sound to the default patch.",
            below); // top bar: point down

  // --- Wavetable ---
  const juce::String wtOff = "Disabled: turn the wavetable On first.";
  hints.add(wtTempoSyncButton,
            "Time each step by a note division at the host tempo instead of by "
            "frames.",
            wtOff);
  hints.add(wtSpeedKnob,
            "How long a step lasts: frames per step, or the note division when "
            "Sync is on.",
            wtOff);
  hints.add(wtLengthKnob, "Number of steps that play.", wtOff);
  hints.add(wtLoopKnob, "Step the table loops back to.", wtOff, left);

  refreshDevices();
  updateEnablement();
  setTab(0);
  setSize(720, 574); // extra height for the preset bar (pages keep their size)
  startTimerHz(10); // drives updateEnablement (waveform / sustain / LFO gating)
}

AsidEditor::~AsidEditor() {
  stopTimer();
  // Detach our chrome look-and-feel from the standalone window before it (or
  // we) go away, so the window is not left pointing at a destroyed editor's
  // context.
  if (auto *dw = findParentComponentOfClass<juce::DocumentWindow>())
    if (&dw->getLookAndFeel() == &windowChromeLnF())
      dw->setLookAndFeel(nullptr);
  setLookAndFeel(nullptr);
}

void AsidEditor::setTab(int t) {
  currentTab = t;
  oscPage.setVisible(t == 0);    // VOICE
  wtPage.setVisible(t == 1);     // WAVETABLE
  ampModPage.setVisible(t == 2); // MODULATION
  sharedPage.setVisible(t == 3); // GLOBAL
  oscTabBtn.setToggleState(t == 0, juce::dontSendNotification);
  waveTabBtn.setToggleState(t == 1, juce::dontSendNotification);
  ampModTabBtn.setToggleState(t == 2, juce::dontSendNotification);
  sharedTabBtn.setToggleState(t == 3, juce::dontSendNotification);
}

void AsidEditor::refreshPresets(const juce::String &selectName) {
  presetBox.clear(juce::dontSendNotification);
  const auto names = proc.presetNames();
  for (int i = 0; i < names.size(); ++i)
    presetBox.addItem(names[i], i + 1);
  const int idx = names.indexOf(selectName);
  if (idx >= 0)
    presetBox.setSelectedId(idx + 1, juce::dontSendNotification);
  else if (selectName.isNotEmpty())
    presetBox.setText(selectName, juce::dontSendNotification);
}

void AsidEditor::cyclePreset(int delta) {
  const auto names = proc.presetNames();
  if (names.isEmpty())
    return;
  int idx = names.indexOf(presetBox.getText());
  idx = (idx < 0) ? 0 : (idx + delta + names.size()) % names.size();
  presetBox.setSelectedId(
      idx + 1, juce::sendNotification); // fires onChange -> loadPreset
}

void AsidEditor::updateEnablement() {
  // Noise locks the other waveforms on the 6581, so it is exclusive: when it is
  // on, grey out the other three (they keep their state but are ignored).
  const bool noise = intOf(pr.waveNoise) != 0;
  waveTriButton.setEnabled(!noise);
  waveSawButton.setEnabled(!noise);
  wavePulseButton.setEnabled(!noise);

  // Pulse width only matters when the pulse wave actually sounds.
  const bool pulse = intOf(pr.wavePulse) != 0 && !noise;
  pwKnob.setEnabled(pulse);

  // Hard sync is meaningless on a noise-only voice; ring mod needs the
  // triangle.
  syncButton.setEnabled(!noise);
  ringButton.setEnabled(intOf(pr.waveTri) != 0 && !noise);

  // Glide trigger and type only matter when portamento time is up. Reflect the
  // choice value in each 2-segment switch (which segment is lit).
  const bool porta = intOf(pr.portaTime) > 0;
  auto syncSwitch = [porta](juce::ToggleButton &a, juce::ToggleButton &b,
                            int val) {
    a.setToggleState(val == 0, juce::dontSendNotification);
    b.setToggleState(val == 1, juce::dontSendNotification);
    a.setEnabled(porta);
    b.setEnabled(porta);
  };
  syncSwitch(portaTrigBtns[0], portaTrigBtns[1], intOf(pr.portaTrigger));
  syncSwitch(portaTypeBtns[0], portaTypeBtns[1], intOf(pr.portaType));

  // The wavetable's config and steps are live only when it is on. Steps beyond
  // the table length are greyed; the loop point and the playing step are marked
  // on the per-step indicators.
  const bool wtOn = boolOf(pr.wtOn);
  for (auto *s : {&wtSpeedKnob, &wtLengthKnob, &wtLoopKnob})
    s->setEnabled(wtOn);
  wtTempoSyncButton.setEnabled(wtOn);
  // Re-bind the Rate knob when Sync toggles (frames per step vs the stepped
  // tempo division), the same way the LFO rate knobs work below.
  if (const bool wtSynced = boolOf(pr.wtTempoSync);
      (wtSynced ? 1 : 0) != wtSpeedMode)
    configureWtSpeedKnob(wtSynced);
  for (auto &h : wtWaveHead)
    h.setEnabled(wtOn);
  for (auto *h : {&wtSyncHead, &wtRingHead, &wtTestHead, &wtPwHead, &wtArpHead})
    h->setEnabled(wtOn);
  const int wtLen = intOf(pr.wtLength);
  const int wtLoopPt = intOf(pr.wtLoop);
  const int wtPlaying = proc.wtStep();
  const auto acc = voiceColour(currentVoice());
  for (int i = 0; i < AsidProcessor::kWtSteps; ++i) {
    const bool rowActive = wtOn && i < wtLen;
    // Noise is exclusive per step, same as the oscillator: grey the other
    // three.
    const bool stepNoise = intOf(pr.wtNoise[i]) != 0;
    for (int w = 0; w < 4; ++w)
      wtWaveTog[i][w].setEnabled(rowActive && (w == 3 || !stepNoise));
    wtSyncTog[i].setEnabled(rowActive);
    wtRingTog[i].setEnabled(rowActive);
    wtTestTog[i].setEnabled(rowActive);
    wtPwKnob[i].setEnabled(rowActive);
    wtArpValue[i].setEnabled(rowActive);
    wtArpDec[i].setEnabled(rowActive);
    wtArpInc[i].setEnabled(rowActive);
    auto &ind = wtStepInd[i];
    const bool loop = wtOn && i == wtLoopPt, playing = i == wtPlaying;
    if (ind.active != rowActive || ind.loop != loop || ind.playing != playing ||
        ind.accent != acc) {
      ind.active = rowActive;
      ind.loop = loop;
      ind.playing = playing;
      ind.accent = acc;
      ind.repaint();
    }
  }

  // Re-bind each LFO's rate knob when its Tempo Sync toggles (free Hz vs the
  // stepped tempo division). Only on change, so it does not thrash every tick.
  for (auto *u : {&pitchLfoUi, &pwLfoUi, &cutLfoUi}) {
    const bool synced = boolOf(u->syncPtr);
    if ((synced ? 1 : 0) != u->rateMode)
      configureRateKnob(*u, synced);
  }

  // The filter checkboxes are shared across instances; mark this instance's own
  // voice so you can see which of the three it drives. Only touch it on change.
  const int myVoice = juce::jlimit(0, 2, intOf(pr.asidVoice));
  if (myVoice != highlightedVoice) {
    highlightedVoice = myVoice;
    laf.setAccent(
        voiceColour(myVoice)); // recolour the whole window for the new voice
    voiceSwitch.selected =
        myVoice; // light the selected cell in the top-right switch
    for (int i = 0; i < 3; ++i)
      filtButtons[i].getProperties().set("sidHighlight", i == myVoice);
    repaint(); // border, voice switch, and every control pick up the new accent
  }

  // Mark voices another instance already drives, so the switch colours and
  // blocks them (one instance per voice).
  bool sharesChanged = false;
  for (int i = 0; i < 3; ++i) {
    const bool used = proc.voiceUsedByOthers(i);
    if (voiceSwitch.usedByOther[i] != used) {
      voiceSwitch.usedByOther[i] = used;
      sharesChanged = true;
    }
  }
  if (sharesChanged)
    voiceSwitch.repaint();

  // Tempo field: a DAW drives it (host BPM, read-only); standalone lets the
  // user edit the BPM parameter. Toggle editability only when the source
  // changes.
  const double hb = proc.hostBpm();
  const bool hostDriven = hb > 0.0;
  if (hostDriven != bpmHostDriven) {
    bpmHostDriven = hostDriven;
    bpmField.setEditable(!hostDriven, !hostDriven, false);
  }
  if (hostDriven) {
    const int r = juce::roundToInt(hb);
    const bool whole =
        hb - r < 0.05 && r - hb < 0.05; // show "120", not "120.0"
    bpmField.setText(whole ? juce::String(r) : juce::String(hb, 1),
                     juce::dontSendNotification);
  } else if (!bpmField.isBeingEdited()) {
    bpmField.setText(juce::String((int)bpmSlider.getValue()),
                     juce::dontSendNotification);
  }

  // Decay does nothing audible at full sustain, and a high decay there is what
  // makes notes drop out on the unit, so the processor sends decay 0 while
  // sustain is 15 (see applyControlChanges). Grey the knob to show it is not in
  // play, but do NOT write the parameter: the disabled bar draws no value at
  // all, so the stored decay is invisible either way, and zeroing it here threw
  // the user's setting away for good and rewrote it inside presets that carried
  // sustain 15.
  const bool sustainMax = intOf(pr.sustain) == 15;
  decayKnob.setEnabled(!sustainMax);

  // Shape doubles as on/off, so reflect each LFO's On + Shape into its
  // selector. Never grey the selector itself, or the LFO could not be switched
  // back on.
  for (auto *u : {&pitchLfoUi, &pwLfoUi, &cutLfoUi}) {
    const bool on = boolOf(u->onPtr);
    const int wantId = on ? intOf(u->shapePtr) + 2 : 1;
    if (u->shapeBox.getSelectedId() != wantId)
      u->shapeBox.setSelectedId(wantId, juce::dontSendNotification);
  }
  // Grey the rest of each LFO's controls when it is off. The rate knob works in
  // either mode (free Hz or the stepped tempo division), so it follows the LFO.
  auto applyLfo = [](LfoControls &u, bool on) {
    u.syncButton.setEnabled(on);
    u.wheelButton.setEnabled(on);
    u.depthKnob.setEnabled(on);
    u.rateKnob.setEnabled(on);
    u.delayKnob.setEnabled(on);
  };
  applyLfo(pitchLfoUi, boolOf(pitchLfoUi.onPtr));
  applyLfo(cutLfoUi, boolOf(cutLfoUi.onPtr));
  // The PW LFO only works on a pulse wave, so its Shape selector greys out too.
  pwLfoUi.shapeBox.setEnabled(pulse);
  applyLfo(pwLfoUi, pulse && boolOf(pwLfoUi.onPtr));
}

void AsidEditor::timerCallback() {
  updateEnablement();

  // MIDI load: bytes sent (all instances) since the last poll, over the
  // ceiling.
  const long long now =
      AsidShared::get().bytesSent.load(std::memory_order_relaxed);
  const double nowMs = juce::Time::getMillisecondCounterHiRes();
  if (lastBytesMs > 0.0) {
    const double dt = (nowMs - lastBytesMs) / 1000.0;
    if (dt > 0.0) {
      const double rate =
          static_cast<double>(now - lastBytes) / dt; // bytes/sec
      const float load =
          static_cast<float>(rate / AsidShared::kMidiBytesPerSec);
      midiLoad = 0.6f * midiLoad + 0.4f * load; // smooth the reading
      repaint(meterArea);
    }
  }
  lastBytes = now;
  lastBytesMs = nowMs;
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

void AsidEditor::paint(juce::Graphics &g) {
  // Dark preset bar across the very top, matching the window title bar, above
  // the untouched 720x540 content and its overlay.
  g.setColour(juce::Colour(0xff191a1b));
  g.fillRect(getLocalBounds().removeFromTop(kPresetBarH));

  // C64 screen for the content region below the bar: the neutral light-blue
  // border framing the darker screen. The per-voice colour lives in the accent
  // (arcs, active buttons) and the voice switch (top right), drawn over this.
  const auto content = getLocalBounds().withTrimmedTop(kPresetBarH);
  g.setColour(juce::Colour(SidLookAndFeel::kFg));
  g.fillRect(content);
  g.setColour(juce::Colour(SidLookAndFeel::kBg));
  g.fillRect(
      screenBox()); // 688 x 496 dark screen, centred in the light-blue frame

  // Top row: Dehli Musikk logo left, product title centred (the Label), and the
  // voice caption + switch right (components, positioned in resized()).
  auto titleRow = innerArea().removeFromTop(34);
  if (logo.isValid()) {
    // Draw the 96x8 pixel logo at an exact 2x (192x16), left-aligned and
    // vertically centred. Low resampling quality keeps the pixels crisp.
    const int lw = logo.getWidth() * 2, lh = logo.getHeight() * 2;
    g.setImageResamplingQuality(juce::Graphics::lowResamplingQuality);
    g.drawImage(logo, titleRow.getX(), titleRow.getCentreY() - lh / 2, lw, lh,
                0, 0, logo.getWidth(), logo.getHeight());
  }

  // MIDI load meter: segmented blocks lit to the current fraction. The top
  // fifth turns white as a warning; over 100% every block is white (overload).
  if (!meterArea.isEmpty()) {
    // A flat field holding the segments, no border (matching the other fields).
    g.setColour(SidLookAndFeel::fieldFill());
    g.fillRect(meterArea);
    auto inner = meterArea.reduced(2);
    const int segs = 20;
    const int gap = 2;
    const int segW = (inner.getWidth() - (segs - 1) * gap) / segs;
    const bool over = midiLoad >= 1.0f;
    for (int i = 0; i < segs; ++i) {
      juce::Rectangle<int> seg(inner.getX() + i * (segW + gap), inner.getY(),
                               segW, inner.getHeight());
      const bool lit = midiLoad > static_cast<float>(i) / segs;
      if (!lit)
        continue; // unlit shows the dark field behind
      g.setColour((over || i >= 16)
                      ? juce::Colour(SidLookAndFeel::kHot) // warning / overload
                      : juce::Colour(SidLookAndFeel::kFg));
      g.fillRect(seg);
    }
  }
}

void AsidEditor::parentHierarchyChanged() {
  // Standalone only: recolour the host window's title bar and drop the
  // yellow/red from its buttons. In a DAW the host owns the title bar, so leave
  // it untouched.
  if (proc.wrapperType != juce::AudioProcessor::wrapperType_Standalone)
    return;
  // Do it asynchronously and preserve the window bounds: swapping a
  // DocumentWindow's look-and-feel while it is still mid-setup (editor not yet
  // sized) makes it fit to a zero-size content and collapse to a tiny square.
  juce::Component::SafePointer<AsidEditor> self(this);
  juce::MessageManager::callAsync([self] {
    if (self == nullptr)
      return;
    auto *dw = self->findParentComponentOfClass<juce::DocumentWindow>();
    if (dw == nullptr || &dw->getLookAndFeel() == &windowChromeLnF())
      return;
    const auto keep = dw->getBounds();
    dw->setLookAndFeel(&windowChromeLnF());
    if (dw->getBounds() != keep)
      dw->setBounds(keep); // undo any collapse
  });
}

void AsidEditor::resized() {
  // Dark preset bar across the top, above the untouched 720x540 content region.
  auto pbar =
      getLocalBounds().removeFromTop(kPresetBarH).reduced(kBorder + 6, 5);
  tooltipToggle.setBounds(pbar.removeFromLeft(44));
  pbar.removeFromLeft(12);
  presetPrevBtn.setBounds(pbar.removeFromLeft(28));
  pbar.removeFromLeft(4);
  presetNextBtn.setBounds(pbar.removeFromLeft(28));
  pbar.removeFromLeft(10);
  presetDeleteBtn.setBounds(pbar.removeFromRight(62));
  pbar.removeFromRight(6);
  presetSaveBtn.setBounds(pbar.removeFromRight(56));
  pbar.removeFromRight(6);
  initButton.setBounds(pbar.removeFromRight(56));
  pbar.removeFromRight(8);
  presetBox.setBounds(pbar);

  const auto content = getLocalBounds().withTrimmedTop(kPresetBarH);
  overlayComp.setBounds(
      content); // overlay stays on the 720x540 region, never stretched
  auto area = innerArea(); // 672 x 480 padded area inside the dark screen
  auto titleRow = area.removeFromTop(34);
  title.setBounds(titleRow); // full width, text centred
  { // Voice selector in the top-right: "VOICE" caption then the 3-cell switch.
    auto sw = titleRow.removeFromRight(96);
    voiceSwitch.setBounds(sw.withSizeKeepingCentre(96, 28));
    titleRow.removeFromRight(8);
    voiceCaption.setBounds(titleRow.removeFromRight(84));
  }
  area.removeFromTop(8);

  // Global header (above the tabs): MIDI out (+ Refresh) and SID voice, laid
  // horizontally with labels over the controls.
  auto header = area.removeFromTop(44);
  auto midi = header.removeFromLeft(210);
  outLabel.setBounds(midi.removeFromTop(18));
  refreshButton.setBounds(midi.removeFromRight(76));
  midi.removeFromRight(6);
  outputBox.setBounds(midi);
  header.removeFromLeft(12);
  auto tempo = header.removeFromLeft(100);
  bpmLabel.setBounds(tempo.removeFromTop(18));
  bpmField.setBounds(tempo.removeFromTop(24));
  header.removeFromLeft(12);
  auto mod = header.removeFromLeft(126);
  modRateLabel.setBounds(mod.removeFromTop(18));
  modRateBox.setBounds(mod.removeFromTop(24));
  header.removeFromLeft(12);
  midiLoadLabel.setBounds(header.removeFromTop(18));
  meterArea = header.removeFromTop(24);
  area.removeFromTop(8);

  // Tabs: four 168px cells filling the 672px width, no gap (colour marks the
  // active one, like the dual buttons).
  auto tabs = area.removeFromTop(30);
  oscTabBtn.setBounds(tabs.removeFromLeft(168));    // VOICE
  waveTabBtn.setBounds(tabs.removeFromLeft(168));   // WAVETABLE
  ampModTabBtn.setBounds(tabs.removeFromLeft(168)); // MODULATION
  sharedTabBtn.setBounds(tabs.removeFromLeft(168)); // GLOBAL
  // No gap here: each page adds its own space above its first section title.

  // All pages fill the same area; each lays out in its own local coords.
  oscPage.setBounds(area);
  ampModPage.setBounds(area);
  sharedPage.setBounds(area);
  wtPage.setBounds(area);
  layoutOscPage(oscPage.getLocalBounds());
  layoutAmpModPage(ampModPage.getLocalBounds());
  layoutSharedPage(sharedPage.getLocalBounds());
  layoutWavePage(wtPage.getLocalBounds());
}

void AsidEditor::layoutOscPage(juce::Rectangle<int> area) {
  // Fixed grid, full inner width (672): controls span the padded dark screen
  // with no extra side inset. Heights are the uniform control height; gaps are
  // 8 px vertical, 24 px between groups. The block is centred vertically in the
  // page.
  const int H = kCtrlH, vg = 8, hg = 24, titleH = 22, sabove = 24;
  const int twoRow = titleH + 2 * H + vg; // Oscillator / Tuning sections
  const int ampH = titleH + H;            // Amp Envelope section

  auto laySwitchAt = [](juce::Rectangle<int> r, juce::ToggleButton &a,
                        juce::ToggleButton &b) {
    const int half = r.getWidth() / 2; // two flush halves, no gap
    a.setBounds(r.getX(), r.getY(), half, r.getHeight());
    b.setBounds(r.getX() + half, r.getY(), r.getWidth() - half, r.getHeight());
  };

  area.removeFromTop(sabove); // 24px above OSCILLATOR (from the tabs)
  { // OSCILLATOR. Row 1: Tri Saw | Sync Ring | Test(208). Row 2: Pulse Noise |
    // Pulse Width(208) | dead space under Test.
    auto box = area.removeFromTop(twoRow);
    oscGroup.setBounds(box);
    auto c = box.withTrimmedTop(titleH);
    auto r1 = c.removeFromTop(H);
    c.removeFromTop(vg);
    auto r2 = c.removeFromTop(H);
    const int x = r1.getX();
    waveTriButton.setBounds(x, r1.getY(), 100, H);
    waveSawButton.setBounds(x + 108, r1.getY(), 100, H);
    syncButton.setBounds(x + 232, r1.getY(), 100, H); // 24px after Saw
    ringButton.setBounds(x + 340, r1.getY(), 100, H); // 8px after Sync
    testButton.setBounds(x + 464, r1.getY(), 208, H); // 24px after Ring, wide
    wavePulseButton.setBounds(x, r2.getY(), 100, H);
    waveNoiseButton.setBounds(x + 108, r2.getY(), 100, H);
    pwKnob.setBounds(x + 232, r2.getY(), 208, H); // under Sync/Ring, 208 wide
  }
  area.removeFromTop(sabove);
  { // TUNING: Coarse/Fine, Bend/Glide and the switch pair - three 208px
    // columns.
    auto box = area.removeFromTop(twoRow);
    glideGroup.setBounds(box);
    auto c = box.withTrimmedTop(titleH);
    auto r1 = c.removeFromTop(H);
    c.removeFromTop(vg);
    auto r2 = c.removeFromTop(H);
    const int x = r1.getX(), colW = 208, step = colW + hg; // 232
    coarseKnob.setBounds(x, r1.getY(), colW, H);
    fineKnob.setBounds(x, r2.getY(), colW, H);
    bendKnob.setBounds(x + step, r1.getY(), colW, H);
    portaKnob.setBounds(x + step, r2.getY(), colW, H);
    laySwitchAt({x + 2 * step, r1.getY(), colW, H}, portaTrigBtns[0],
                portaTrigBtns[1]);
    laySwitchAt({x + 2 * step, r2.getY(), colW, H}, portaTypeBtns[0],
                portaTypeBtns[1]);
  }
  area.removeFromTop(sabove);
  { // AMP ENVELOPE: four 162px bars, 8px gaps.
    auto box = area.removeFromTop(ampH);
    ampGroup.setBounds(box);
    auto r = box.withTrimmedTop(titleH).removeFromTop(H);
    const int x = r.getX(), step = 162 + 8; // 170
    attackKnob.setBounds(x, r.getY(), 162, H);
    decayKnob.setBounds(x + step, r.getY(), 162, H);
    sustainKnob.setBounds(x + 2 * step, r.getY(), 162, H);
    releaseKnob.setBounds(x + 3 * step, r.getY(), 162, H);
  }
}

void AsidEditor::layoutAmpModPage(juce::Rectangle<int> area) {
  const int H = kCtrlH, vg = 8, titleH = 22, sabove = 24;
  const int secH = titleH + 2 * H + vg; // title + the LFO's two rows
  area.removeFromTop(sabove);
  { // PITCH MODULATION
    auto pm = area.removeFromTop(secH);
    pitchModGroup.setBounds(pm);
    layoutLfo(pitchLfoUi, pm.withTrimmedTop(titleH));
  }
  area.removeFromTop(sabove);
  { // PULSE WIDTH MODULATION
    auto pw = area.removeFromTop(secH);
    pwModGroup.setBounds(pw);
    layoutLfo(pwLfoUi, pw.withTrimmedTop(titleH));
  }
}

void AsidEditor::layoutSharedPage(juce::Rectangle<int> area) {
  const int H = kCtrlH, vg = 8, titleH = 22, sabove = 24;
  const int twoRow = titleH + 2 * H + vg; // Filter, Cutoff Modulation
  const int ampH = titleH + H;            // Master
  area.removeFromTop(sabove);
  { // FILTER. Row 1: V1 V2 V3 | LP BP HP | Cutoff(208). Row 2: External(208) |
    // dead space | Resonance(208). Three 208px columns at x = 0, 232, 464.
    auto box = area.removeFromTop(twoRow);
    filterGroup.setBounds(box);
    auto c = box.withTrimmedTop(titleH);
    auto r1 = c.removeFromTop(H);
    c.removeFromTop(vg);
    auto r2 = c.removeFromTop(H);
    const int x = r1.getX(), sw = 64;
    filtButtons[0].setBounds(x, r1.getY(), sw, H);       // V1
    filtButtons[1].setBounds(x + 72, r1.getY(), sw, H);  // V2
    filtButtons[2].setBounds(x + 144, r1.getY(), sw, H); // V3
    modeButtons[0].setBounds(x + 232, r1.getY(), sw, H); // LP
    modeButtons[1].setBounds(x + 304, r1.getY(), sw, H); // BP
    modeButtons[2].setBounds(x + 376, r1.getY(), sw, H); // HP
    cutoffKnob.setBounds(x + 464, r1.getY(), 208, H);
    filtExtButton.setBounds(x, r2.getY(), 208,
                            H); // External (dead space in col 2)
    resKnob.setBounds(x + 464, r2.getY(), 208, H);
  }
  area.removeFromTop(sabove);
  { // CUTOFF MODULATION
    auto cm = area.removeFromTop(twoRow);
    cutModGroup.setBounds(cm);
    layoutLfo(cutLfoUi, cm.withTrimmedTop(titleH));
  }
  area.removeFromTop(sabove);
  { // MASTER: Volume, Latency, Voice 3 Off, Panic - four equal controls filling
    // the width with 24px gaps (Init moved to the preset bar).
    auto box = area.removeFromTop(ampH);
    masterGroup.setBounds(box);
    auto row = box.withTrimmedTop(titleH).removeFromTop(H);
    const int g = 24, cw = (row.getWidth() - 3 * g) / 4;
    int cx = row.getX();
    voice3offButton.setBounds(cx, row.getY(), cw, H);
    cx += cw + g;
    volumeKnob.setBounds(cx, row.getY(), cw, H);
    cx += cw + g;
    latencyKnob.setBounds(cx, row.getY(), cw, H);
    cx += cw + g;
    panicButton.setBounds(cx, row.getY(), cw, H);
  }
}

void AsidEditor::layoutWavePage(juce::Rectangle<int> area) {
  const int H = kCtrlH, titleH = 22, sabove = 24;
  area.removeFromTop(sabove); // 24px above the WAVETABLE title (from the tabs)
  { // WAVETABLE config: On, Sync, Rate, Length, Loop. Five columns in the
    // same single row (134px each), so the section keeps its height and STEPS
    // below it does not move. Box wraps title + one row tightly (no extra
    // bottom padding) so STEPS sits a clean 24px below, like other sections.
    auto box = area.removeFromTop(titleH + H);
    wtConfigGroup.setBounds(box);
    auto c = box.withTrimmedTop(titleH);
    toggleInCol(colOf(c, 0, 5), wtOnButton);
    toggleInCol(colOf(c, 1, 5), wtTempoSyncButton);
    knobInCol(colOf(c, 2, 5), wtSpeedKnob);
    knobInCol(colOf(c, 3, 5), wtLengthKnob);
    knobInCol(colOf(c, 4, 5), wtLoopKnob);
  }
  area.removeFromTop(sabove); // 24px above the STEPS title
  { // STEPS. Each row (24px tall, 4px apart): loop+number (40), then seven 32px
    // toggle boxes (Tri Saw Pul Noi Syn Rin Test) 24px apart, a 96px Pulse
    // Width fader, and the arp stepper [-][value][+], filling the full 672px
    // width.
    const int steps = AsidProcessor::kWtSteps;
    wtStepsGroup.setBounds(area);
    auto c = area.withTrimmedTop(18); // below the STEPS title, full width
    const int box = 32, pwW = 96;
    // loop(8)+number(32) = 40, then seven 32px boxes 24px apart, so the row
    // fills the full 672: the i-th toggle box starts at 64 + i*56.
    auto cx = [](int i) { return 64 + i * 56; };
    const int pwX = cx(7), arpX = pwX + pwW + 24; // 456, 576
    // Header row: labels are 52px wide (wider than the 32px box) and centred on
    // each box, so a 3-char label ("Tri") fits by spilling into the 24px gaps.
    // Columns are 56px apart, so neighbours never overlap.
    auto head = c.removeFromTop(16);
    auto headAt = [&](juce::Label &lbl, int col) {
      lbl.setBounds(cx(col) + box / 2 - 26, head.getY(), 52, head.getHeight());
    };
    for (int w = 0; w < 4; ++w)
      headAt(wtWaveHead[w], w);
    headAt(wtSyncHead, 4);
    headAt(wtRingHead, 5);
    headAt(wtTestHead, 6);
    wtPwHead.setBounds(pwX, head.getY(), pwW, head.getHeight());
    wtArpHead.setBounds(arpX, head.getY(), 96, head.getHeight());
    c.removeFromTop(4);
    // Step rows are shorter than the standard control height, hence their own
    // height and gap rather than the page's H / vg.
    const int rowH = 24, rowGap = 4;
    for (int i = 0; i < steps; ++i) {
      if (i > 0)
        c.removeFromTop(rowGap);
      auto line = c.removeFromTop(rowH);
      const int y = line.getY();
      wtStepInd[i].setBounds(0, y, 40, rowH); // 8px loop box + 32px number
      for (int w = 0; w < 4; ++w)
        wtWaveTog[i][w].setBounds(cx(w), y, box, rowH);
      wtSyncTog[i].setBounds(cx(4), y, box, rowH);
      wtRingTog[i].setBounds(cx(5), y, box, rowH);
      wtTestTog[i].setBounds(cx(6), y, box, rowH);
      wtPwKnob[i].setBounds(pwX, y, pwW, rowH); // horizontal fader
      const int bw = 24;                        // arp stepper: [-][value][+]
      wtArpDec[i].setBounds(arpX, y, bw, rowH);
      wtArpValue[i].setBounds(arpX + bw, y, 48, rowH);
      wtArpInc[i].setBounds(arpX + bw + 48, y, bw, rowH);
    }
  }
}
