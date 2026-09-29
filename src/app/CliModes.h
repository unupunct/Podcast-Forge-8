#pragma once
#include <juce_core/juce_core.h>

#include <optional>

namespace pf8::cli {

// Returns an exit code when the command line selects a headless mode, nullopt for the normal UI.
std::optional<int> runHeadless(const juce::StringArray& args);

// Writes UTF-8 text to the process's stdout (redirected pipe/file or the parent console).
void writeStdout(const juce::String& text);

} // namespace pf8::cli
