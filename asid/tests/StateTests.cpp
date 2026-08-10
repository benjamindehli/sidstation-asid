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

static float norm(AsidProcessor &p, const juce::String &id) {
  auto *q = p.state().getParameter(id);
  return q != nullptr ? q->getValue() : -1.0f; // normalised 0..1
}
static void setNorm(AsidProcessor &p, const juce::String &id, float v) {
  if (auto *q = p.state().getParameter(id))
    q->setValueNotifyingHost(v);
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
  testStateRoundTrip();
  testPresetRoundTrip();
  testPresetNameSanitising();
  std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
