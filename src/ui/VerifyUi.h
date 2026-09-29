#pragma once
// --verify-ui (TESTING.md §4): renders every screen offscreen at several resolutions/DPI scales,
// checks the component tree for layout defects and writes PNGs + report.json. No desktop capture.
#include <filesystem>
#include <string>
#include <vector>

#include "engine/EngineController.h"

namespace pf8::ui {

struct VerifyUiResult
{
    int screensRendered = 0;
    std::vector<std::string> issues;
    std::filesystem::path outputDir;
};

VerifyUiResult runVerifyUi(EngineController& controller, const std::filesystem::path& outDir);

} // namespace pf8::ui
