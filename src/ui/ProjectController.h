#pragma once
// The current project on the UI thread (brief §24): New / Open / Save / Save As / Archive, unsaved-
// change tracking, and the crash-restore snapshot.
//
// Crash restore: every few seconds the working state is compared with the last snapshot and, when
// it changed, written to Settings.db together with the project folder. A "running" flag is set at
// start and cleared at a clean exit; if it is still set at the next start, the previous run did not
// exit cleanly and the snapshot (the newest state there is) is applied over the project.
#include <juce_gui_basics/juce_gui_basics.h>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "core/Json.h"
#include "engine/EngineController.h"

namespace pf8 {
class SettingsDb;
}

namespace pf8::ui {

class ProjectController : private juce::Timer
{
public:
    ProjectController(EngineController& controller, SettingsDb* db);
    ~ProjectController() override;

    // Start-up: reopen the last project (or the default Untitled Project) and, after an unclean
    // exit, apply the crash-restore snapshot. Returns a message for the user ("" = nothing to say).
    juce::String startup();
    void shutdown(); // clean exit: snapshot cleared, running flag reset

    const std::filesystem::path& dir() const noexcept { return dir_; }
    const std::string& name() const noexcept { return name_; }
    bool dirty() const noexcept { return dirty_; }
    juce::String displayName() const;

    // Menu actions (asynchronous dialogs; `parent` positions them).
    void showMenu(juce::Component& target);
    void newProject();
    void openProject();
    bool save(juce::String* error = nullptr); // saves in place (creates Project.json the first time)
    void saveAs();
    void archive();
    // Quit flow: asks to save unsaved changes, then calls `proceed` (not called on Cancel).
    void confirmClose(std::function<void()> proceed);

    std::function<void()> onChanged; // name / dirty state changed

    // Loads a project folder now (also used by tests and the Open dialog).
    bool openDir(const std::filesystem::path& dir, juce::String& error);

private:
    void timerCallback() override;
    void applyLoaded(const json::Value& state, juce::StringArray& warnings);
    void setCurrent(const std::filesystem::path& dir, const std::string& name);
    void rebaseline();
    void writeSnapshot(const std::string& stateText);
    void queueCart(int cart, const std::filesystem::path& file);
    void loaderMain();
    bool recordingActive() const;

    EngineController& controller_;
    SettingsDb* db_;
    std::filesystem::path dir_;
    std::string name_;
    std::string baseline_;     // serialised state at the last save / open
    std::string lastSnapshot_;
    bool dirty_ = false;
    bool rebaselinePending_ = false;
    int ticks_ = 0;
    std::unique_ptr<juce::FileChooser> chooser_;

    // Cart loader: one worker, joined on destruction (never outlives the soundboard).
    std::thread loader_;
    std::mutex loaderMutex_;
    std::condition_variable loaderCv_;
    std::deque<std::pair<int, std::filesystem::path>> loadQueue_;
    std::shared_ptr<std::atomic<int>> loading_ = std::make_shared<std::atomic<int>>(0); // shared with queued hand-overs
    bool loaderQuit_ = false;
};

} // namespace pf8::ui
