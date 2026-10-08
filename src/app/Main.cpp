// Podcast Forge 8 — multi-channel podcast recording and monitoring console.
// Copyright (C) 2026 unupunct. Licensed under the GNU AGPL v3 (see LICENSE).
#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

#include "app/CliModes.h"
#include "app/E2E.h"
#include "core/Log.h"
#include "core/Paths.h"
#include "core/SettingsDb.h"
#include "engine/EngineController.h"
#include "project/AppSettings.h"
#include "ui/MainWindow.h"
#include "ui/UiPrefs.h"
#include "ui/VerifyUi.h"

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

        if (args.contains("--e2e"))
        {
            const int code = cli::runE2E(args);
            log::stop();
            setApplicationReturnValue(code);
            quit();
            return;
        }

        if (args.contains("--verify-ui"))
        {
            // Offscreen UI self-test: no settings, no persisted assignments, no desktop capture.
            EngineController verifyController(EngineController::Settings{}, nullptr);
            verifyController.start();
            verifyController.waitIdle();
            const auto r = ui::runVerifyUi(verifyController, paths::verifyOutput());
            juce::String out;
            out << "verify-ui: " << r.screensRendered << " screens, " << static_cast<int>(r.issues.size()) << " issues\n";
            for (const auto& i : r.issues) out << "  " << juce::String::fromUTF8(i.c_str()) << "\n";
            out << "output: " << juce::String(r.outputDir.wstring().c_str()) << "\n";
            cli::writeStdout(out);
            PF8_LOG_INFO("app", "verify-ui screens=%d issues=%zu", r.screensRendered, r.issues.size());
            log::stop();
            setApplicationReturnValue(r.issues.empty() ? 0 : 1);
            quit();
            return;
        }

        std::string dbError;
        if (!settings_.open(paths::settingsDb(), &dbError))
            PF8_LOG_ERROR("app", "settings unavailable (%s); assignments will not persist", dbError.c_str());
        const auto app = AppSettings::load(settings_.isOpen() ? &settings_ : nullptr);
        if (app.debugLog) log::setMinLevel(log::Level::Debug);
        if (app.uiScale != 1.0) juce::Desktop::getInstance().setGlobalScaleFactor(static_cast<float>(app.uiScale));
        EngineController::Settings es;
        es.sampleRate = app.sampleRate;
        es.blockFrames = app.blockFrames;
        es.mode = app.mode;
        es.raw = app.rawStreams;
        ui::uiPrefs().showAllChannels = app.showAllChannels;
        PF8_LOG_INFO("app", "engine rate=%d block=%d mode=%d", es.sampleRate, es.blockFrames, static_cast<int>(es.mode));
        controller_ = std::make_unique<EngineController>(es, settings_.isOpen() ? &settings_ : nullptr);
        controller_->start();
        window_ = std::make_unique<ui::MainWindow>(getApplicationName(), *controller_);
    }

    void shutdown() override
    {
        if (window_) window_->shutdown();
        window_.reset();
        controller_.reset();
        settings_.close();
        if (log::isRunning())
        {
            PF8_LOG_INFO("app", "app.exit clean");
            log::stop();
        }
    }

    void systemRequestedQuit() override
    {
        if (window_) window_->requestQuit([] { juce::JUCEApplication::quit(); });
        else quit();
    }

    void anotherInstanceStarted(const juce::String&) override
    {
        if (window_) window_->toFront(true);
    }

private:
    SettingsDb settings_;
    std::unique_ptr<EngineController> controller_;
    std::unique_ptr<ui::MainWindow> window_;
};

} // namespace pf8

START_JUCE_APPLICATION(pf8::PodcastForgeApp)
