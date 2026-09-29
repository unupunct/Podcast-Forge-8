// Podcast Forge 8 — multi-channel podcast recording and monitoring console.
// Copyright (C) 2026 unupunct. Licensed under the GNU AGPL v3 (see LICENSE).
#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

#include "app/CliModes.h"
#include "core/Log.h"
#include "core/Paths.h"
#include "engine/EngineController.h"
#include "ui/MainWindow.h"

namespace pf8 {

class PodcastForgeApp : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise(const juce::String& commandLine) override
    {
        const auto args = juce::StringArray::fromTokens(commandLine, true);
        if (auto code = cli::runHeadless(args))
        {
            setApplicationReturnValue(*code);
            quit();
            return;
        }

        log::start({paths::logs()});
        PF8_LOG_INFO("app", "app.start version=%s", JUCE_APPLICATION_VERSION_STRING);

        controller_ = std::make_unique<EngineController>(EngineController::Settings{});
        controller_->rescanDevices();
        controller_->startEngineOnDefaultOutput();
        window_ = std::make_unique<ui::MainWindow>(getApplicationName(), *controller_);
    }

    void shutdown() override
    {
        window_.reset();
        controller_.reset();
        if (log::isRunning())
        {
            PF8_LOG_INFO("app", "app.exit clean");
            log::stop();
        }
    }

    void systemRequestedQuit() override { quit(); }

    void anotherInstanceStarted(const juce::String&) override
    {
        if (window_) window_->toFront(true);
    }

private:
    std::unique_ptr<EngineController> controller_;
    std::unique_ptr<ui::MainWindow> window_;
};

} // namespace pf8

START_JUCE_APPLICATION(pf8::PodcastForgeApp)
