#pragma once
// --e2e: a real end-to-end session (TESTING.md §5) without the UI and without touching the user's
// saved device assignments.
//
//   * real capture endpoints feed channels; channels without one get simulated tones;
//   * real speakers / headphones are never used (no test tone is ever played out loud) — only a
//     VB-Cable pair, when present, closes a real playback → capture loop;
//   * records N seconds with 5 s pre-roll, a marker, a cart, music with ducking and a talkback press,
//     then validates the files and writes a JSON report.
#include <juce_core/juce_core.h>

namespace pf8::cli {

int runE2E(const juce::StringArray& args);

} // namespace pf8::cli
