#pragma once
// Per-machine application settings kept in Settings.db (not in projects). Audio engine settings
// are read once at start; the Settings page saves them for the next start.
#include "engine/StreamTypes.h"

namespace pf8 {

class SettingsDb;

struct AppSettings
{
    int sampleRate = 48000;     // 44100 / 48000 / 96000
    int blockFrames = 128;      // 64 / 128 / 256 / 512
    StreamMode mode = StreamMode::Shared;
    double uiScale = 1.0;       // 1.0 … 2.0
    bool debugLog = false;
    bool rawStreams = true;     // bypass Windows audio effects

    static AppSettings load(const SettingsDb* db); // invalid / missing values → defaults
    bool save(SettingsDb* db) const;
    bool operator==(const AppSettings&) const = default;
};

} // namespace pf8
