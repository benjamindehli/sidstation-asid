// JUCE-linked tests for the SidStation ASID processor: parameter layout, plugin
// state round-trip, and preset save/load. These need JUCE (they instantiate the
// AudioProcessor and its APVTS), unlike the pure core tests.
//
// Build with -DSID_BUILD_TESTS=ON, then run the SidStationAsidTests binary.
#include <cmath>
#include <cstdio>

#include <juce_audio_utils/juce_audio_utils.h>

#include "AsidProcessor.h"

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

int main() {
  juce::ScopedJuceInitialiser_GUI
      juceInit; // message manager, needed by JUCE classes
  testParameterLayout();
  testDivisionsAreAppendOnly();
  testModClockIntervals();
  testWavetableSyncParams();
  testStateRoundTrip();
  testPresetRoundTrip();
  testPresetMigration();
  testPresetNameSanitising();
  std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
