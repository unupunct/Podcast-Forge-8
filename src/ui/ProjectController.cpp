#include "ui/ProjectController.h"

#include "core/Log.h"
#include "core/Paths.h"
#include "core/SettingsDb.h"
#include "media/AudioFileLoader.h"
#include "project/Project.h"
#include "project/ProjectState.h"

namespace pf8::ui {
namespace fs = std::filesystem;
namespace {

constexpr const char* kKeyLastProject = "project.last";
constexpr const char* kKeyRunning = "app.running";
constexpr const char* kKeySnapshot = "restore.snapshot";

juce::String u8(const std::string& s) { return juce::String::fromUTF8(s.c_str()); }
std::string toUtf8(const juce::String& s) { return s.toStdString(); }
fs::path toPath(const juce::File& f) { return fs::path(f.getFullPathName().toWideCharPointer()); }
juce::File toFile(const fs::path& p) { return juce::File(juce::String(p.wstring().c_str())); }

} // namespace

ProjectController::ProjectController(EngineController& controller, SettingsDb* db) : controller_(controller), db_(db)
{
    dir_ = controller_.recordingSettings().projectDir;
    name_ = paths::utf8(dir_.filename());
    loader_ = std::thread([this] { loaderMain(); });
}

ProjectController::~ProjectController()
{
    stopTimer();
    {
        std::lock_guard lock(loaderMutex_);
        loaderQuit_ = true;
        loadQueue_.clear();
    }
    loaderCv_.notify_all();
    if (loader_.joinable()) loader_.join();
}

juce::String ProjectController::displayName() const { return u8(name_) + (dirty_ ? " *" : ""); }

bool ProjectController::recordingActive() const { return controller_.recorder().state() != Recorder::State::Idle; }

void ProjectController::setCurrent(const fs::path& dir, const std::string& name)
{
    dir_ = dir;
    name_ = name;
    controller_.recordingSettings().projectDir = dir;
    if (db_) db_->set(kKeyLastProject, paths::utf8(dir));
    if (onChanged) onChanged();
}

void ProjectController::rebaseline()
{
    baseline_ = json::serialize(project::captureState(controller_));
    lastSnapshot_ = baseline_;
    dirty_ = false;
    if (onChanged) onChanged();
}

juce::String ProjectController::startup()
{
    juce::String message;
    bool crashed = false;
    if (db_)
    {
        crashed = db_->get(kKeyRunning).value_or("0") == "1";
        db_->set(kKeyRunning, "1");
        if (auto last = db_->get(kKeyLastProject); last && !last->empty())
        {
            const auto dir = paths::fromUtf8(*last);
            juce::String err;
            if (project::isProjectDir(dir) && !openDir(dir, err))
                message << "The last project could not be opened: " << err << "\n";
            else if (!project::isProjectDir(dir))
                setCurrent(dir, paths::utf8(dir.filename())); // never saved yet: keep recording there
        }
        if (crashed)
            if (auto snap = db_->get(kKeySnapshot))
                if (auto v = json::parse(*snap); v && v->isObject())
                {
                    const auto dir = paths::fromUtf8((*v)["dir"].asString());
                    if (!dir.empty() && dir != dir_)
                    {
                        juce::String err;
                        if (project::isProjectDir(dir)) openDir(dir, err);
                        else setCurrent(dir, (*v)["name"].asString(paths::utf8(dir.filename())));
                    }
                    juce::StringArray warnings;
                    applyLoaded((*v)["state"], warnings);
                    rebaselinePending_ = true;
                    message << "Podcast Forge 8 did not close normally last time. The settings in use at that moment were restored "
                               "(save the project to keep them).\n";
                    for (const auto& w : warnings) message << "  " << w << "\n";
                    PF8_LOG_WARN("project", "unclean exit detected; restore snapshot applied");
                }
    }
    startTimerHz(2);
    if (!crashed) rebaselinePending_ = true;
    return message.trim();
}

void ProjectController::shutdown()
{
    stopTimer();
    if (db_)
    {
        db_->remove(kKeySnapshot);
        db_->set(kKeyRunning, "0");
    }
}

void ProjectController::applyLoaded(const json::Value& state, juce::StringArray& warnings)
{
    auto r = project::applyState(state, controller_);
    for (const auto& w : r.warnings) warnings.add(u8(w));
    for (const auto& [cart, file] : r.carts) queueCart(cart, file);
}

bool ProjectController::openDir(const fs::path& dir, juce::String& error)
{
    if (recordingActive())
    {
        error = "Stop the recording before opening another project.";
        return false;
    }
    std::string err;
    auto p = project::loadProject(dir, err);
    if (!p)
    {
        error = u8(err);
        return false;
    }
    juce::StringArray warnings;
    applyLoaded(p->state, warnings);
    setCurrent(dir, p->name);
    controller_.waitIdle(); // device assignments applied before the baseline is taken
    rebaselinePending_ = true;
    if (!warnings.isEmpty())
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Project opened with warnings", warnings.joinIntoString("\n"));
    return true;
}

bool ProjectController::save(juce::String* error)
{
    const auto state = project::captureState(controller_);
    std::string err;
    if (!project::saveProject(dir_, name_, state, err))
    {
        if (error) *error = u8(err);
        else juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Project not saved", u8(err));
        return false;
    }
    if (db_) db_->set(kKeyLastProject, paths::utf8(dir_));
    baseline_ = json::serialize(state);
    dirty_ = false;
    if (onChanged) onChanged();
    return true;
}

void ProjectController::newProject()
{
    if (recordingActive())
    {
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, "Recording", "Stop the recording before starting a new project.");
        return;
    }
    confirmClose([this] {
        auto* w = new juce::AlertWindow("New project", "Name of the new project:", juce::MessageBoxIconType::NoIcon);
        w->addTextEditor("name", "New Episode");
        w->addButton("Create", 1, juce::KeyPress(juce::KeyPress::returnKey));
        w->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
        w->enterModalState(true, juce::ModalCallbackFunction::create([this, w](int r) {
                               if (r != 1) return;
                               const std::string name = toUtf8(w->getTextEditorContents("name").trim());
                               // The new project starts from the current settings (devices, DSP, routing)
                               // but with an empty soundboard and playlist.
                               auto state = project::captureState(controller_);
                               auto& o = state.object();
                               o.erase("carts");
                               o["music"].object()["playlist"] = json::Array{};
                               std::string err;
                               auto dir = project::createProject(paths::defaultProjects(), name, state, err);
                               if (!dir)
                               {
                                   juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Project not created", u8(err));
                                   return;
                               }
                               juce::String e;
                               if (!openDir(*dir, e))
                               {
                                   // The current project stays open: keep its carts.
                                   juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Project not opened", e);
                                   return;
                               }
                               // Clear what the new project does not carry over.
                               auto& sb = controller_.engine().soundboard();
                               for (int i = 0; i < kCartCount; ++i)
                               {
                                   sb.stop(i);
                                   sb.setBuffer(i, nullptr);
                                   sb.settings(i).name.clear();
                                   sb.settings(i).hotkey.clear();
                               }
                               rebaselinePending_ = true;
                           }),
                           true);
    });
}

void ProjectController::openProject()
{
    if (recordingActive())
    {
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, "Recording", "Stop the recording before opening another project.");
        return;
    }
    confirmClose([this] {
        chooser_ = std::make_unique<juce::FileChooser>("Open project (Project.json)", toFile(paths::defaultProjects()), "Project.json");
        chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& fc) {
            const auto f = fc.getResult();
            if (!f.existsAsFile()) return;
            juce::String err;
            if (!openDir(toPath(f.getParentDirectory()), err))
                juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Project not opened", err);
        });
    });
}

void ProjectController::saveAs()
{
    auto* w = new juce::AlertWindow("Save project as",
                                    "Name of the new project. Recordings stay with the current project; the new one starts "
                                    "with these settings, carts and playlist.",
                                    juce::MessageBoxIconType::NoIcon);
    w->addTextEditor("name", u8(name_) + " copy");
    w->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
    w->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    w->enterModalState(true, juce::ModalCallbackFunction::create([this, w](int r) {
                           if (r != 1) return;
                           if (recordingActive())
                           {
                               juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, "Recording",
                                                                      "Stop the recording first: it belongs to the current project.");
                               return;
                           }
                           const std::string name = toUtf8(w->getTextEditorContents("name").trim());
                           std::string err;
                           auto state = project::captureState(controller_);
                           auto dir = project::saveProjectAs(paths::defaultProjects(), name, state, err);
                           if (!dir)
                           {
                               juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Project not saved", u8(err));
                               return;
                           }
                           setCurrent(*dir, name.empty() ? paths::utf8(dir->filename()) : name);
                           baseline_ = json::serialize(state);
                           dirty_ = false;
                           if (onChanged) onChanged();
                       }),
                       true);
}

void ProjectController::archive()
{
    if (recordingActive())
    {
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, "Recording", "Stop the recording before archiving.");
        return;
    }
    if (dirty_ || !project::isProjectDir(dir_)) save();
    const auto suggested = toFile(dir_).getParentDirectory().getChildFile(u8(paths::utf8(dir_.filename())) + ".zip");
    chooser_ = std::make_unique<juce::FileChooser>("Archive project to", suggested, "*.zip");
    chooser_->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [this](const juce::FileChooser& fc) {
        auto f = fc.getResult();
        if (f == juce::File()) return;
        if (!f.hasFileExtension("zip")) f = f.withFileExtension("zip");
        if (f.exists())
        {
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Archive", "That file already exists. Choose a new name.");
            return;
        }
        std::string err;
        if (project::archiveProject(dir_, toPath(f), err))
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, "Archive", "Project archived to\n" + f.getFullPathName());
        else
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Archive failed", u8(err));
    });
}

void ProjectController::showMenu(juce::Component& target)
{
    juce::PopupMenu m;
    m.addSectionHeader(u8(name_));
    m.addItem(1, "New project...");
    m.addItem(2, "Open project...");
    m.addSeparator();
    m.addItem(3, "Save", true, false);
    m.addItem(4, "Save as...");
    m.addSeparator();
    m.addItem(5, "Archive project (zip)...");
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&target), [this](int r) {
        switch (r)
        {
            case 1: newProject(); break;
            case 2: openProject(); break;
            case 3: save(); break;
            case 4: saveAs(); break;
            case 5: archive(); break;
            default: break;
        }
    });
}

void ProjectController::confirmClose(std::function<void()> proceed)
{
    if (!dirty_)
    {
        proceed();
        return;
    }
    auto* w = new juce::AlertWindow("Unsaved changes", "Save the changes to \"" + u8(name_) + "\"?", juce::MessageBoxIconType::QuestionIcon);
    w->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
    w->addButton("Don't save", 2);
    w->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    w->enterModalState(true, juce::ModalCallbackFunction::create([this, proceed](int r) {
                           if (r == 0) return;
                           if (r == 1 && !save()) return;
                           proceed();
                       }),
                       true);
}

void ProjectController::writeSnapshot(const std::string& stateText)
{
    if (!db_) return;
    json::Object o;
    o["dir"] = paths::utf8(dir_);
    o["name"] = name_;
    auto v = json::parse(stateText);
    o["state"] = v ? *v : json::Value();
    db_->set(kKeySnapshot, json::serialize(json::Value(std::move(o))));
}

void ProjectController::timerCallback()
{
    ++ticks_;
    if (rebaselinePending_ && loading_->load() == 0)
    {
        rebaselinePending_ = false;
        rebaseline();
        return;
    }
    if (ticks_ % 6 != 0) return; // every 3 s
    const std::string now = json::serialize(project::captureState(controller_));
    const bool d = now != baseline_;
    if (d != dirty_)
    {
        dirty_ = d;
        if (onChanged) onChanged();
    }
    if (now != lastSnapshot_)
    {
        lastSnapshot_ = now;
        writeSnapshot(now);
    }
}

void ProjectController::queueCart(int cart, const fs::path& file)
{
    {
        std::lock_guard lock(loaderMutex_);
        loadQueue_.emplace_back(cart, file);
        loading_->fetch_add(1);
    }
    loaderCv_.notify_one();
}

void ProjectController::loaderMain()
{
    for (;;)
    {
        std::pair<int, fs::path> job;
        {
            std::unique_lock lock(loaderMutex_);
            loaderCv_.wait(lock, [this] { return loaderQuit_ || !loadQueue_.empty(); });
            if (loaderQuit_) return;
            job = std::move(loadQueue_.front());
            loadQueue_.pop_front();
        }
        auto buf = loadAudioFile(job.second, {controller_.settings().sampleRate, 600.0});
        const int cart = job.first;
        auto& sb = controller_.engine().soundboard();
        // Hand over on the message thread (the soundboard's control side is UI-thread only).
        juce::MessageManager::callAsync([&sb, cart, buf, loading = loading_] {
            sb.setBuffer(cart, buf);
            if (!buf->error.empty()) PF8_LOG_WARN("project", "cart %d: %s", cart + 1, buf->error.c_str());
            loading->fetch_sub(1);
        });
    }
}

} // namespace pf8::ui
