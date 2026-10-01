#include "project/AppSettings.h"

#include <cstdlib>
#include <string>

#include "core/SettingsDb.h"

namespace pf8 {
namespace {

int getInt(const SettingsDb* db, const char* key, int def)
{
    if (!db) return def;
    const auto v = db->get(key);
    if (!v || v->empty()) return def;
    char* end = nullptr;
    const long n = std::strtol(v->c_str(), &end, 10);
    return end && *end == '\0' ? static_cast<int>(n) : def;
}

} // namespace

AppSettings AppSettings::load(const SettingsDb* db)
{
    AppSettings s;
    const int rate = getInt(db, "audio.sampleRate", s.sampleRate);
    if (rate == 44100 || rate == 48000 || rate == 96000) s.sampleRate = rate;
    const int block = getInt(db, "audio.blockFrames", s.blockFrames);
    if (block == 64 || block == 128 || block == 256 || block == 512) s.blockFrames = block;
    const int mode = getInt(db, "audio.mode", 0);
    if (mode >= 0 && mode <= 2) s.mode = static_cast<StreamMode>(mode);
    const int scale = getInt(db, "ui.scalePercent", 100);
    if (scale >= 100 && scale <= 200) s.uiScale = scale / 100.0;
    s.debugLog = getInt(db, "log.debug", 0) == 1;
    return s;
}

bool AppSettings::save(SettingsDb* db) const
{
    if (!db) return false;
    bool ok = db->set("audio.sampleRate", std::to_string(sampleRate));
    ok = db->set("audio.blockFrames", std::to_string(blockFrames)) && ok;
    ok = db->set("audio.mode", std::to_string(static_cast<int>(mode))) && ok;
    ok = db->set("ui.scalePercent", std::to_string(static_cast<int>(uiScale * 100.0 + 0.5))) && ok;
    ok = db->set("log.debug", debugLog ? "1" : "0") && ok;
    return ok;
}

} // namespace pf8
