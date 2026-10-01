#pragma once
// Decodes WAV / MP3 / FLAC (JUCE readers) into interleaved stereo at the engine rate. Not for the
// audio thread. Mono files are duplicated to both sides; rates other than the engine rate are
// converted with the engine's own windowed-sinc resampler.
#include <filesystem>
#include <memory>
#include <string>

#include "media/Soundboard.h"

namespace pf8 {

struct LoadOptions
{
    int engineRate = 48000;
    double maxSeconds = 600.0; // carts are short; long material belongs in the music player
};

std::shared_ptr<CartBuffer> loadAudioFile(const std::filesystem::path& file, const LoadOptions& options);

// File extensions accepted by the soundboard and music player.
const char* supportedAudioWildcard() noexcept; // "*.wav;*.mp3;*.flac"

} // namespace pf8
