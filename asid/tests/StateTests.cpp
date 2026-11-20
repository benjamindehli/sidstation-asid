// JUCE-linked tests for the SidStation ASID processor: parameter layout, plugin
// state round-trip, and preset save/load. These need JUCE (they instantiate the
// AudioProcessor and its APVTS), unlike the pure core tests.
//
// Build with -DSID_BUILD_TESTS=ON, then run the SidStationAsidTests binary.
#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <juce_audio_utils/juce_audio_utils.h>

#include "AsidEditor.h"
#include "AsidProcessor.h"
#include "UpdateCheck.h"

static int g_checks = 0, g_failures = 0;

#define CHECK(cond, msg)                                                       \
  do {                                                                         \
    ++g_checks;                                                                \
    if (!(cond)) {                                                             \
      ++g_failures;                                                            \
      std::printf("FAIL: %s  (%s:%d)\n", msg, __FILE__, __LINE__);             \
    }                                                                          \
  } while (0)

static bool approx(float a, float b) { return std::abs(a - b) < 1.0e-4f; }
static bool approxD(double a, double b) { return std::abs(a - b) < 1.0e-9; }

static float norm(AsidProcessor &p, const juce::String &id) {
  auto *q = p.state().getParameter(id);
  return q != nullptr ? q->getValue() : -1.0f; // normalised 0..1
}
static void setNorm(AsidProcessor &p, const juce::String &id, float v) {
  if (auto *q = p.state().getParameter(id))
    q->setValueNotifyingHost(v);
}
// A freshly constructed processor holds every parameter's default, so these
// read the default by reading the current value.
static juce::AudioParameterChoice *choiceOf(AsidProcessor &p,
                                            const juce::String &id) {
  return dynamic_cast<juce::AudioParameterChoice *>(p.state().getParameter(id));
}
static juce::AudioParameterBool *boolOf(AsidProcessor &p,
                                        const juce::String &id) {
  return dynamic_cast<juce::AudioParameterBool *>(p.state().getParameter(id));
}

static void testParameterLayout() {
  AsidProcessor p;
  auto &s = p.state();
  CHECK(s.getParameter("bpm") != nullptr, "bpm parameter exists");
  if (auto *bpm = s.getParameter("bpm")) {
    const auto def =
        bpm->getNormalisableRange().convertFrom0to1(bpm->getDefaultValue());
    CHECK(approx((float)def, 120.0f), "bpm default is 120");
  }
  for (int i = 0; i < AsidProcessor::kWtSteps; ++i)
    CHECK(s.getParameter("wtTest" + juce::String(i)) != nullptr,
          "per-step wtTest parameter exists");
  CHECK(s.getParameter("test") != nullptr, "oscillator test parameter exists");
  CHECK(s.getParameter("filtExt") != nullptr,
        "filter external parameter exists");
  CHECK(s.getParameter("voice3off") != nullptr, "voice 3 off parameter exists");
}

// The note division list is APPEND ONLY, because a preset and an automation
// lane store the choice INDEX. Inserting "1/2T" in the middle would silently
// move every saved division on every LFO and every wavetable. Nothing in the
// code can stop that, so this test is the guard: it pins each index to its
// label and to its length in beats, absolutely rather than relatively, which is
// what makes an insertion fail here instead of in someone's session.
static void testDivisionsAreAppendOnly() {
  struct Div {
    const char *label;
    double beats;
  };
  static const Div expected[] = {{"1/1", 4.0},    {"1/2", 2.0},
                                 {"1/4", 1.0},    {"1/4T", 2.0 / 3.0},
                                 {"1/8", 0.5},    {"1/8T", 1.0 / 3.0},
                                 {"1/16", 0.25},  {"1/16T", 1.0 / 6.0},
                                 {"1/32", 0.125}, {"1/64", 0.0625}};
  const int n = (int)(sizeof(expected) / sizeof(expected[0]));

  const auto choices = AsidProcessor::divisionChoices();
  CHECK(choices.size() == n, "the division list has the expected length");
  for (int i = 0; i < n && i < choices.size(); ++i) {
    CHECK(choices[i] == expected[i].label, "division label is at its index");
    CHECK(approxD(AsidProcessor::beatsForDivision(i), expected[i].beats),
          "division index maps to its length in beats");
  }
  // Out of range falls back to one beat rather than reading past the switch.
  CHECK(approxD(AsidProcessor::beatsForDivision(-1), 1.0),
        "a negative division index falls back to 1/4");
  CHECK(approxD(AsidProcessor::beatsForDivision(n), 1.0),
        "a division index past the end falls back to 1/4");

  // Every synced control offers exactly that list, so none of them can drift
  // onto a hand-written copy of it.
  AsidProcessor p;
  for (const char *id : {"pitchLfoDiv", "pwLfoDiv", "cutLfoDiv", "wtDiv"}) {
    auto *c = choiceOf(p, id);
    CHECK(c != nullptr, "the division parameter exists and is a choice");
    if (c != nullptr)
      CHECK(c->choices == choices, "the parameter offers the shared list");
  }
  // Defaults, in index terms, since that is what gets stored.
  for (const char *id : {"pitchLfoDiv", "pwLfoDiv", "cutLfoDiv"})
    if (auto *c = choiceOf(p, id))
      CHECK(c->getIndex() == 2 && c->getCurrentChoiceName() == "1/4",
            "an LFO division defaults to 1/4");
  if (auto *c = choiceOf(p, "wtDiv"))
    CHECK(c->getIndex() == 6 && c->getCurrentChoiceName() == "1/16",
          "the wavetable division defaults to 1/16");
}

// The editor sizes a synced wavetable step against one tick of this clock to
// tell whether steps are being skipped, so the interval per choice and the Hz
// in each label have to agree.
static void testModClockIntervals() {
  CHECK(approxD(AsidProcessor::modIntervalForRate(0), 40.0), "Eco is 25 Hz");
  CHECK(approxD(AsidProcessor::modIntervalForRate(1), 20.0), "PAL is 50 Hz");
  CHECK(approxD(AsidProcessor::modIntervalForRate(2), 1000.0 / 60.0),
        "NTSC is 60 Hz");
  CHECK(approxD(AsidProcessor::modIntervalForRate(3), 10.0), "HiFi is 100 Hz");
  CHECK(approxD(AsidProcessor::modIntervalForRate(9), 20.0),
        "an unknown clock choice falls back to PAL");

  AsidProcessor p;
  auto *c = choiceOf(p, "modRate");
  CHECK(c != nullptr, "modRate is a choice parameter");
  if (c != nullptr) {
    for (int i = 0; i < c->choices.size(); ++i) {
      // "PAL 50 Hz" -> 50, checked against the interval it selects.
      const int hz = c->choices[i]
                         .upToFirstOccurrenceOf(" Hz", false, false)
                         .getTrailingIntValue();
      CHECK(hz ==
                juce::roundToInt(1000.0 / AsidProcessor::modIntervalForRate(i)),
            "the clock label's Hz matches the interval it selects");
    }
  }
}

// Wavetable tempo sync. The id is wtTempoSync and NOT wtSync, because
// wtSync0..7 are the per-step SID hard-sync bits: two unrelated things one
// letter apart.
static void testWavetableSyncParams() {
  AsidProcessor p;
  auto &s = p.state();
  auto *sync = boolOf(p, "wtTempoSync");
  CHECK(sync != nullptr, "wtTempoSync exists and is a bool");
  if (sync != nullptr)
    CHECK(!sync->get(), "wavetable tempo sync defaults to off");
  for (int i = 0; i < AsidProcessor::kWtSteps; ++i)
    CHECK(s.getParameter("wtSync" + juce::String(i)) != nullptr &&
              s.getParameter("wtSync" + juce::String(i)) !=
                  s.getParameter("wtTempoSync"),
          "the per-step hard-sync bit is a separate parameter");
  // The frames-per-step control kept its id when its label became Rate, which
  // is what lets old presets and automation carry over.
  auto *rate = s.getParameter("wtSpeed");
  CHECK(rate != nullptr, "wtSpeed is still the frames-per-step id");
  if (rate != nullptr) {
    CHECK(rate->getName(32) == "WT Rate", "and is labelled Rate, as the UI is");
    const auto def =
        rate->getNormalisableRange().convertFrom0to1(rate->getDefaultValue());
    CHECK(approx((float)def, 2.0f), "wtSpeed still defaults to 2 frames");
  }
}

// A preset written before tempo sync existed has no wtTempoSync or wtDiv in it.
// Loading one has to leave sync OFF, so an old patch sounds as it did, which is
// the claim the changelog makes. loadPreset defaults everything first for
// exactly this reason, and this is the test that it keeps doing so.
static void testPresetMigration() {
  AsidProcessor a;
  const juce::String name = "__sidstation_test_migration__";
  const auto file = a.presetsDir().getChildFile(name + ".xml");
  file.deleteFile(); // clean start

  // Save with sync ON and a division away from its default, then strip both
  // keys back out of the file to make it look like a pre-sync preset.
  setNorm(a, "wtTempoSync", 1.0f);
  if (auto *c = choiceOf(a, "wtDiv"))
    c->setValueNotifyingHost(c->convertTo0to1(0.0f)); // 1/1
  CHECK(a.savePreset(name), "preset with the sync parameters saves");

  auto xml = juce::XmlDocument::parse(file);
  CHECK(xml != nullptr, "the saved preset parses");
  if (xml == nullptr)
    return;
  CHECK(xml->hasAttribute("wtTempoSync") && xml->hasAttribute("wtDiv"),
        "the sync parameters are part of a preset, not shared state");
  xml->removeAttribute("wtTempoSync");
  xml->removeAttribute("wtDiv");
  CHECK(xml->writeTo(file), "the stripped preset is written back");

  CHECK(a.loadPreset(name), "the pre-sync preset loads");
  if (auto *sync = boolOf(a, "wtTempoSync"))
    CHECK(!sync->get(), "a preset without the key loads with sync off");
  if (auto *c = choiceOf(a, "wtDiv"))
    CHECK(c->getIndex() == 6, "and with the division back at its default");

  file.deleteFile(); // cleanup
  CHECK(!a.presetNames().contains(name), "preset removed after cleanup");
}

static void testStateRoundTrip() {
  AsidProcessor a;
  setNorm(a, "cutoff", 0.73f);
  setNorm(a, "waveNoise", 1.0f);
  juce::MemoryBlock mb;
  a.getStateInformation(mb);

  AsidProcessor b;
  b.setStateInformation(mb.getData(), (int)mb.getSize());
  CHECK(approx(norm(a, "cutoff"), norm(b, "cutoff")),
        "cutoff round-trips through state");
  CHECK(approx(norm(a, "waveNoise"), norm(b, "waveNoise")),
        "waveNoise round-trips through state");
}

// Every parameter of p holds its default and a finite value.
static bool allAtDefaults(AsidProcessor &p) {
  AsidProcessor fresh;
  for (auto *q : p.getParameters()) {
    auto *wid = dynamic_cast<juce::AudioProcessorParameterWithID *>(q);
    if (wid == nullptr)
      continue;
    const auto *raw = p.state().getRawParameterValue(wid->paramID);
    const auto *def = fresh.state().getRawParameterValue(wid->paramID);
    if (raw == nullptr || def == nullptr || !std::isfinite(raw->load()) ||
        !approx(raw->load(), def->load())) {
      std::printf("  %s is %f\n", wid->paramID.toRawUTF8(),
                  raw != nullptr ? (double)raw->load() : -1.0);
      return false;
    }
  }
  return true;
}

// A damaged or hand-edited session or preset can hold "nan", which the
// parameters' range clamps let through. It has to come back as the default.
static void testValuesThatAreNotNumbers() {
  AsidProcessor a;
  juce::MemoryBlock mb;
  a.getStateInformation(mb);
  auto xml =
      juce::AudioProcessor::getXmlFromBinary(mb.getData(), (int)mb.getSize());
  CHECK(xml != nullptr, "the state reads back as XML");
  if (xml == nullptr)
    return;
  int poisoned = 0;
  for (auto *child : xml->getChildIterator())
    if (child->hasAttribute("value")) {
      child->setAttribute("value", "nan");
      ++poisoned;
    }
  CHECK(poisoned > 100, "every saved value is set to nan");
  juce::MemoryBlock bad;
  juce::AudioProcessor::copyXmlToBinary(*xml, bad);

  AsidProcessor b;
  b.setStateInformation(bad.getData(), (int)bad.getSize());
  CHECK(allAtDefaults(b),
        "a saved value that is not a number comes back as the default");

  // The same for a preset file.
  const juce::String name = "__sidstation_nan_preset__";
  AsidProcessor c;
  CHECK(c.savePreset(name), "the preset is saved");
  const auto file = c.presetsDir().getChildFile(name + ".xml");
  if (auto preset = juce::XmlDocument::parse(file)) {
    for (int i = 0; i < preset->getNumAttributes(); ++i)
      preset->setAttribute(preset->getAttributeName(i), "nan");
    preset->writeTo(file);
  }
  CHECK(c.loadPreset(name), "the preset with nan values loads");
  CHECK(allAtDefaults(c),
        "a preset value that is not a number comes back as the default");
  file.deleteFile(); // cleanup
}

static void testPresetRoundTrip() {
  AsidProcessor a;
  const juce::String name = "__sidstation_test_preset__";
  a.presetsDir().getChildFile(name + ".xml").deleteFile(); // clean start

  setNorm(a, "attack", 0.9f);
  const float saved = norm(
      a, "attack"); // actual stored value (attack is a 0..15 int, so 0.9 snaps)
  a.savePreset(name);
  CHECK(a.currentPreset() == name, "current preset name is set after save");
  CHECK(a.presetNames().contains(name), "saved preset appears in the list");

  setNorm(a, "attack", 0.1f);
  CHECK(!approx(norm(a, "attack"), saved), "value changed before load");
  CHECK(a.loadPreset(name), "loadPreset succeeds");
  CHECK(approx(norm(a, "attack"), saved), "preset restores the saved value");

  a.presetsDir().getChildFile(name + ".xml").deleteFile(); // cleanup
  CHECK(!a.presetNames().contains(name), "preset removed after cleanup");
}

// A preset name becomes a filename, and juce::File::getChildFile honours "../"
// and absolute paths, so the name has to be reduced to something that cannot
// escape the presets folder. The typed name and the stored name may therefore
// differ.
static void testPresetNameSanitising() {
  for (const char *hostile :
       {"../escaped", "/tmp/escaped", "..\\escaped", "a/b:c"}) {
    const auto key = AsidProcessor::presetKey(hostile);
    CHECK(!key.containsChar('/') && !key.containsChar('\\') &&
              !key.containsChar(':'),
          "sanitised preset name carries no path separator");
    CHECK(AsidProcessor::presetKey(key) == key, "sanitising is idempotent");
  }
  CHECK(AsidProcessor::presetKey("   ").isEmpty(),
        "a whitespace-only name is rejected");

  AsidProcessor a;
  CHECK(!a.savePreset("   "), "saving an unusable name fails");

  const juce::String typed = "__sid/test:preset__";
  const auto key = AsidProcessor::presetKey(typed);       // __sidtestpreset__
  a.presetsDir().getChildFile(key + ".xml").deleteFile(); // clean start
  CHECK(a.savePreset(typed), "a name needing sanitising still saves");
  CHECK(a.currentPreset() == key,
        "currentPreset reports the stored key, not the typed name");
  CHECK(a.presetsDir().getChildFile(key + ".xml").existsAsFile(),
        "the file lands inside the presets folder");
  CHECK(a.presetNames().contains(key),
        "the sanitised preset appears in the list");
  CHECK(a.loadPreset(typed), "the typed name still resolves to it");
  a.presetsDir().getChildFile(key + ".xml").deleteFile(); // cleanup
}

// AsidShared is made with the first instance and deleted with the last, so its
// watchdog and MIDI sender threads stop and the port closes. A process-wide
// static would outlive the plugin binary in a host that unloads it, with those
// threads still running (see Lifetime in AsidShared.h).
static void testSharedStateLifetime() {
  using Shared = juce::SharedResourcePointer<AsidShared>;
  CHECK(!Shared::getSharedObjectWithoutCreating().has_value(),
        "no shared state exists before the first instance");
  {
    auto a = std::make_unique<AsidProcessor>();
    auto b = std::make_unique<AsidProcessor>();
    CHECK(&a->shared() == &b->shared(), "two instances share one state");

    setNorm(*a, "cutoff", 0.3f);
    CHECK(approx(norm(*a, "cutoff"), norm(*b, "cutoff")),
          "a shared control set on one instance reaches the other");

    const AsidShared *first = &a->shared();
    a.reset();
    {
      const auto still = Shared::getSharedObjectWithoutCreating();
      CHECK(still.has_value() && &still->get() == first,
            "the shared state outlives an instance while another is open");
    }
  }
  CHECK(!Shared::getSharedObjectWithoutCreating().has_value(),
        "deleting the last instance deletes the shared state");

  // The next first instance starts a fresh one, as on a fresh launch.
  AsidProcessor c;
  CHECK(Shared::getSharedObjectWithoutCreating().has_value(),
        "a new first instance makes the shared state again");
}

// Deciding whether a release is newer, and reading the feed that says so. The
// fetch needs a network and is not exercised here. A comparison that comes out
// backwards nags every user, and one that is too eager treats a malformed feed
// as an upgrade.
static void testUpdateCheck() {
  using asid::isNewerVersion;
  CHECK(isNewerVersion("1.3.0", "1.2.1"), "a higher minor is newer");
  CHECK(isNewerVersion("2.0.0", "1.9.9"), "a higher major is newer");
  CHECK(isNewerVersion("1.2.2", "1.2.1"), "a higher patch is newer");
  CHECK(!isNewerVersion("1.2.1", "1.2.1"), "the same version is not newer");
  CHECK(!isNewerVersion("1.2.0", "1.2.1"), "an older version is not newer");
  CHECK(isNewerVersion("1.2.10", "1.2.9"),
        "10 is newer than 9, which sorting as text would deny");
  CHECK(!isNewerVersion("1.2.9", "1.2.10"), "and the reverse");
  CHECK(isNewerVersion("v1.3.0", "1.2.1"), "a leading v is ignored");
  CHECK(isNewerVersion("1.3", "1.2.9"), "a missing component counts as zero");
  CHECK(!isNewerVersion("1.3", "1.3.0"), "so 1.3 and 1.3.0 are one release");
  for (const char *junk : {"", "  ", "latest", "1.3.0-beta", "v", "1..0",
                           "<!doctype html>", "1.3.0a"})
    CHECK(!isNewerVersion(junk, "1.2.1"),
          "a version that does not parse is not newer");
  CHECK(!isNewerVersion("2.0.0", "garbage"),
        "an unreadable current version blocks the comparison too");

  using asid::parseReleaseJson;
  const auto good = parseReleaseJson(
      R"({"version":"1.3.0","url":"https://github.com/benjamindehli/sidstation-asid/releases/tag/v1.3.0"})");
  CHECK(good && good->version == "1.3.0", "a well-formed feed parses");
  CHECK(good && good->url == "https://github.com/benjamindehli/"
                             "sidstation-asid/releases/tag/v1.3.0",
        "and carries the page to send someone to");
  const auto noUrl = parseReleaseJson(R"({"version":"1.3.0"})");
  CHECK(noUrl && noUrl->url == asid::kLatestRelease,
        "a feed with no url falls back to the releases page");
  // The link is opened in a browser, so only this project's pages pass.
  for (const char *elsewhere :
       {"https://example.invalid/v1.3.0",
        "http://github.com/benjamindehli/sidstation-asid/",
        "file:///etc/passwd", "javascript:alert(1)",
        "https://github.com/benjamindehli/sidstation-asid.evil.example/",
        "https://github.com/someone-else/sidstation-asid/releases",
        "https://github.com/benjamindehli/sidstation-asid/../../"
        "someone-else"}) {
    const auto moved = parseReleaseJson(
        juce::String(R"({"version":"1.3.0","url":")") + elsewhere + "\"}");
    CHECK(moved && moved->url == asid::kLatestRelease,
          "a link anywhere else is replaced by the releases page");
  }
  for (const char *bad : {"", "{}", "[]", "not json", R"({"version":""})",
                          R"({"version":null})", "null"})
    CHECK(!parseReleaseJson(bad).has_value(), "a broken feed yields nothing");
}

static juce::Button *buttonTitled(juce::Component &root,
                                  const juce::String &title) {
  for (auto *child : root.getChildren()) {
    if (auto *b = dynamic_cast<juce::Button *>(child))
      if (b->getTitle() == title)
        return b;
    if (auto *found = buttonTitled(*child, title))
      return found;
  }
  return nullptr;
}

// The preset bar asks once whether to check for a new version, and the version
// at its end says when one is out. Run against the machine's own settings, so
// each is put back as it was found. "Yes" is never pressed, since that would
// reach the network.
static void testUpdateQuestion() {
  const bool allowedBefore = asid::updateCheckAllowed();
  const bool offeredBefore = asid::updateCheckOffered();

  asid::setUpdateCheckAllowed(true);
  CHECK(asid::updateCheckAllowed(), "the setting is read back from the file");
  asid::setUpdateCheckAllowed(false);
  CHECK(!asid::updateCheckAllowed(), "and so is the other answer");

  asid::setUpdateCheckOffered(false);
  AsidProcessor p;
  {
    std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor());
    auto &editor = dynamic_cast<AsidEditor &>(*ed);
    CHECK(editor.isAskingAboutUpdates(), "the first window asks");
    CHECK(!asid::updateCheckOffered(),
          "and opening it marks nothing, so a host scanning its plugins does "
          "not use the question up");
    auto *preset = buttonTitled(editor, "Previous preset");
    CHECK(preset != nullptr && !preset->isVisible(),
          "the question takes the place of the preset controls");
    auto *notNow = buttonTitled(editor, "Do not check for a new version");
    CHECK(notNow != nullptr, "Not now is there to press");
    if (notNow != nullptr)
      notNow->onClick();
    CHECK(!editor.isAskingAboutUpdates() && preset != nullptr &&
              preset->isVisible(),
          "an answer brings the preset controls back");
  }
  CHECK(asid::updateCheckOffered() && !asid::updateCheckAllowed(),
        "Not now is remembered, and nothing is checked");
  for (int open = 0; open < 3; ++open) {
    std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor());
    CHECK(!dynamic_cast<AsidEditor &>(*ed).isAskingAboutUpdates(),
          "so later windows do not ask again");
  }
  CHECK(!asid::updateCheckAllowed(),
        "and opening windows never turns the check on");

  {
    std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor());
    auto &editor = dynamic_cast<AsidEditor &>(*ed);
    CHECK(buttonTitled(editor, "Version " SID_ASID_VERSION) != nullptr,
          "the version is named for a screen reader");
    editor.announceRelease("99.0.0", "https://github.com/benjamindehli/"
                                     "sidstation-asid/releases/tag/v99.0.0");
    CHECK(buttonTitled(editor, "Version 99.0.0 is out") != nullptr,
          "a release found is named in its place");
    editor.announceRelease({}, {});
    CHECK(buttonTitled(editor, "Version " SID_ASID_VERSION) != nullptr,
          "and clearing it brings the version back");
  }

  // The menu, as data, since showing one needs a real window.
  {
    auto menu = AsidEditor::updateMenu(false, "99.0.0");
    juce::PopupMenu::MenuItemIterator it(menu);
    CHECK(it.next() && it.getItem().itemID == AsidEditor::kGetRelease &&
              it.getItem().text.contains("99.0.0"),
          "a release found is the first thing in the menu");
    bool toggle = false, ticked = true;
    while (it.next())
      if (it.getItem().itemID == AsidEditor::kToggleCheck) {
        toggle = true;
        ticked = it.getItem().isTicked;
      }
    CHECK(toggle && !ticked, "and the setting follows, unticked when off");
  }
  {
    auto menu = AsidEditor::updateMenu(true, {});
    juce::PopupMenu::MenuItemIterator it(menu);
    CHECK(
        it.next() && it.getItem().itemID == AsidEditor::kToggleCheck &&
            it.getItem().isTicked && !it.next(),
        "with nothing found, the menu holds only the setting, ticked when on");
  }

  asid::setUpdateCheckAllowed(allowedBefore);
  asid::setUpdateCheckOffered(offeredBefore);
  CHECK(asid::updateCheckAllowed() == allowedBefore &&
            asid::updateCheckOffered() == offeredBefore,
        "and the machine's own answers are put back");
}

// A screen reader announces a control by its title (a button by its text when
// it has none), so every control needs one, and no two may share it: three
// sliders all called "Rate" leave a listener no better off. Parameter-bound
// controls take their parameter's name, which hosts already need unique.
static void testEveryControlIsNamed() {
  // With the update question answered, so the preset bar shows its controls
  // rather than the question. Put back afterwards, since it is machine-wide.
  const bool offeredBefore = asid::updateCheckOffered();
  asid::setUpdateCheckOffered(true);
  AsidProcessor p;
  std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
  asid::setUpdateCheckOffered(offeredBefore);
  CHECK(editor != nullptr, "the editor opens");
  if (editor == nullptr)
    return;

  std::map<std::string, int> names;
  int controls = 0;
  std::vector<std::string> nameless;

  std::function<void(juce::Component &)> walk = [&](juce::Component &c) {
    for (auto *child : c.getChildren()) {
      // Hidden by itself, not by a parent: a control that is never shown,
      // like the slider that carries the tempo behind its text field.
      // Controls on other tabs count, since their page is what is hidden.
      const bool control = dynamic_cast<juce::Slider *>(child) != nullptr ||
                           dynamic_cast<juce::Button *>(child) != nullptr ||
                           dynamic_cast<juce::ComboBox *>(child) != nullptr ||
                           child->getTitle() == "SID voice";
      if (control && child->isVisible()) {
        ++controls;
        auto name = child->getTitle();
        if (name.isEmpty())
          if (auto *b = dynamic_cast<juce::Button *>(child))
            name = b->getButtonText();
        if (name.isEmpty())
          nameless.push_back(std::string(typeid(*child).name()) + " in " +
                             typeid(c).name() + " named '" +
                             child->getName().toStdString() + "'");
        else
          ++names[name.toStdString()];
      }
      walk(*child);
    }
  };
  walk(*editor);

  std::printf("  %d controls, %d distinct names\n", controls,
              (int)names.size());
  CHECK(controls > 150, "the walk reached every tab");
  for (const auto &n : nameless)
    std::printf("  nameless: %s\n", n.c_str());
  CHECK(nameless.empty(), "every control has a name");
  for (const auto &[name, count] : names)
    if (count > 1)
      std::printf("  shared by %d: %s\n", count, name.c_str());
  CHECK((int)names.size() + (int)nameless.size() == controls,
        "and no two controls share one");

  CHECK(names.count("Cutoff") == 1, "a knob is named after its parameter");
  CHECK(names.count("Cutoff LFO Sync") == 1 && names.count("PW LFO Sync") == 1,
        "the three LFOs' switches are told apart");
  CHECK(names.count("WT Arp 3 up") == 1, "the arp steppers name their step");
  CHECK(names.count("SID voice") == 1, "the voice switch is reachable");
}

int main() {
  juce::ScopedJuceInitialiser_GUI
      juceInit;              // message manager, needed by JUCE classes
  testSharedStateLifetime(); // first, while no instance exists
  testParameterLayout();
  testDivisionsAreAppendOnly();
  testModClockIntervals();
  testWavetableSyncParams();
  testStateRoundTrip();
  testValuesThatAreNotNumbers();
  testPresetRoundTrip();
  testPresetMigration();
  testPresetNameSanitising();
  testUpdateCheck();
  testUpdateQuestion();
  testEveryControlIsNamed();
  std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
