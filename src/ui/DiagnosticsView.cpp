#include "ui/DiagnosticsView.h"

#include <windows.h>

#include <chrono>
#include <cmath>
#include <vector>

#include "core/Log.h"
#include "core/Paths.h"

namespace pf8::ui {
namespace {

juce::String u8(const std::string& s) { return juce::String::fromUTF8(s.c_str()); }

// Writes 256 MB with write-through to a file of our own in `dir`, deletes that file, returns MB/s.
double measureDisk(const std::filesystem::path& dir, std::string& error)
{
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const auto file = dir / L"pf8-disk-speed-test.tmp";
    HANDLE h = CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_FLAG_WRITE_THROUGH | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (h == INVALID_HANDLE_VALUE)
    {
        error = "cannot create a test file in " + paths::utf8(dir);
        return -1.0;
    }
    std::vector<char> buf(4 << 20, 0x5a);
    const int chunks = 64; // 256 MB
    const auto t0 = std::chrono::steady_clock::now();
    bool ok = true;
    for (int i = 0; i < chunks && ok; ++i)
    {
        DWORD written = 0;
        ok = WriteFile(h, buf.data(), static_cast<DWORD>(buf.size()), &written, nullptr) && written == buf.size();
    }
    FlushFileBuffers(h);
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    CloseHandle(h); // FILE_FLAG_DELETE_ON_CLOSE removes our own test file
    if (!ok)
    {
        error = "write failed (disk full?)";
        return -1.0;
    }
    return 256.0 / std::max(secs, 1e-6);
}

} // namespace

DiagnosticsView::DiagnosticsView(EngineController& controller) : controller_(controller)
{
    report_.onClick = [this] {
        if (auto f = controller_.writeDiagnosticsReport("manual"))
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, "Diagnostics report",
                                                   "Saved to\n" + juce::String(f->wstring().c_str()) + "\n\nIt contains device names and counters only - no audio.");
        else
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Diagnostics report", "The report could not be written.");
    };
    restart_.setTooltip("Close and reopen every audio stream (what the watchdog does after a stall). A recording keeps running.");
    restart_.onClick = [this] { controller_.restartAudio(); };
    disk_.setTooltip("Write 256 MB to the project's drive and measure the speed (the test file is removed).");
    disk_.onClick = [this] { runDiskTest(); };
    note_.setFont(juce::FontOptions(12.0f));
    note_.setColour(juce::Label::textColourId, colours::textDim);
    allowEllipsis(note_);
    note_.setText("Logs: " + juce::String(paths::logs().wstring().c_str()) + "   Reports: " + juce::String(paths::diagnostics().wstring().c_str()),
                  juce::dontSendNotification);
    for (juce::Component* c : std::initializer_list<juce::Component*>{&report_, &restart_, &disk_, &note_}) addAndMakeVisible(c);
    timerCallback();
    startTimerHz(4);
}

DiagnosticsView::~DiagnosticsView()
{
    stopTimer();
    if (diskThread_.joinable()) diskThread_.join();
}

void DiagnosticsView::runDiskTest()
{
    if (diskRunning_) return;
    if (controller_.recorder().state() != Recorder::State::Idle)
    {
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, "Disk speed test", "Not while recording: the test would compete with the recording.");
        return;
    }
    if (diskThread_.joinable()) diskThread_.join();
    diskRunning_ = true;
    diskMBps_ = -1.0;
    {
        std::lock_guard lock(diskMutex_);
        diskError_.clear();
    }
    const auto dir = controller_.recordingSettings().projectDir;
    diskThread_ = std::thread([this, dir] {
        std::string err;
        const double r = measureDisk(dir, err);
        diskMBps_ = r;
        PF8_LOG_INFO("diag", "disk speed test %.1f MB/s %s", r, err.c_str());
        {
            std::lock_guard lock(diskMutex_);
            diskError_ = err;
        }
        diskRunning_ = false;
    });
}

void DiagnosticsView::timerCallback()
{
    status_ = controller_.status();
    meters_ = controller_.meters();
    rec_ = controller_.recorder().status();
    if (++ticks_ % 4 == 1) cpuPct_ = cpu_.sample();
    glitches_ = controller_.glitches();
    rows_.clear();
    auto add = [&](const EndpointView& v, const juce::String& role) {
        if (v.state == EndpointState::None) return;
        Row r;
        r.role = role;
        r.device = u8(v.assignedName);
        r.state = toString(v.state);
        r.ok = v.state == EndpointState::Ok;
        r.master = v.master;
        if (r.ok)
        {
            r.sync = v.master ? juce::String("MASTER") : juce::String(toString(v.bridge.status));
            r.ppm = juce::String(v.bridge.ppm, 1);
            r.fill = juce::String(v.bridge.fill, 0) + " / " + juce::String(v.bridge.target, 0);
            r.xruns = juce::String(static_cast<juce::int64>(v.bridge.underruns)) + " / " + juce::String(static_cast<juce::int64>(v.bridge.overruns));
            r.format = juce::String(v.deviceRate) + " Hz  " + juce::String(v.deviceChannels) + " ch  " + juce::String(v.periodFrames) + " fr";
            r.warn = v.bridge.status == SyncStatus::Unstable || std::abs(v.bridge.ppm) > 500.0;
        }
        rows_.push_back(r);
    };
    for (size_t i = 0; i < status_.channels.size(); ++i)
    {
        add(status_.channels[i].mic, "CH" + juce::String(static_cast<int>(i) + 1) + " mic");
        add(status_.channels[i].headphones, "CH" + juce::String(static_cast<int>(i) + 1) + " HP");
    }
    static const char* roles[kOutputRoles] = {"Monitor", "Stream Main", "Stream Clean", "Stream Music"};
    for (size_t r = 0; r < status_.outputs.size(); ++r) add(status_.outputs[r], roles[r]);
    add(status_.talkback, "Talkback");
    disk_.setButtonText(diskRunning_ ? "TESTING..." : "DISK SPEED TEST");
    disk_.setEnabled(!diskRunning_);
    repaint();
}

void DiagnosticsView::paint(juce::Graphics& g)
{
    g.fillAll(colours::background);
    // Tiles.
    struct Tile { juce::String caption, value; juce::Colour colour; };
    const double diskMB = diskMBps_.load();
    juce::String diskErr;
    {
        std::lock_guard lock(diskMutex_);
        diskErr = u8(diskError_);
    }
    std::vector<Tile> tiles = {
        {"PROCESS CPU", juce::String(cpuPct_, 1) + " %", colours::text},
        {"AUDIO LOAD", juce::String(meters_.load * 100.0, 1) + " %  (peak " + juce::String(meters_.loadPeak * 100.0, 0) + ")",
         meters_.loadPeak > 0.8 ? colours::error : meters_.loadPeak > 0.5 ? colours::warn : colours::ok},
        {"CLOCK", status_.internalClock ? juce::String("INTERNAL") : u8(status_.masterName), status_.internalClock ? colours::warn : colours::ok},
        {"TICKS", juce::String(static_cast<juce::int64>(meters_.ticks)) + "  (skipped " + juce::String(static_cast<juce::int64>(meters_.skippedTicks)) + ")",
         colours::text},
        {"WATCHDOG", juce::String(static_cast<juce::int64>(controller_.watchdogStalls())) + " stalls / " +
                         juce::String(static_cast<juce::int64>(controller_.watchdogRestarts())) + " restarts",
         controller_.watchdogStalls() ? colours::warn : colours::ok},
        {"RECORDER", rec_.state == Recorder::State::Idle ? juce::String("idle")
                                                         : juce::String(rec_.writeBytesPerSecond / 1048576.0, 2) + " MB/s, dropped " +
                                                               juce::String(static_cast<juce::int64>(rec_.droppedFrames)),
         rec_.droppedFrames || rec_.writeError ? colours::error : colours::text},
        {"DISK TEST", diskMB > 0 ? juce::String(diskMB, 0) + " MB/s" : (diskErr.isNotEmpty() ? diskErr : juce::String("-")),
         diskMB > 0 && diskMB < 20 ? colours::warn : colours::text},
    };
    const int tw = tiles_.getWidth() / static_cast<int>(tiles.size());
    for (size_t i = 0; i < tiles.size(); ++i)
    {
        auto r = juce::Rectangle<int>(tiles_.getX() + static_cast<int>(i) * tw, tiles_.getY(), tw - 6, tiles_.getHeight());
        g.setColour(colours::panel);
        g.fillRoundedRectangle(r.toFloat(), 4.0f);
        g.setColour(colours::textDim);
        g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
        g.drawText(tiles[i].caption, r.reduced(8, 4).removeFromTop(16), juce::Justification::centredLeft, true);
        g.setColour(tiles[i].colour);
        g.setFont(juce::Font(juce::FontOptions(15.0f, juce::Font::bold)));
        g.drawFittedText(tiles[i].value, r.reduced(8, 4).withTrimmedTop(18), juce::Justification::centredLeft, 1, 0.8f);
    }

    // Stream table.
    static const char* heads[] = {"ROLE", "DEVICE", "STATE", "SYNC", "PPM", "FILL / TARGET", "UNDER / OVER", "FORMAT"};
    const float cols[] = {0.09f, 0.27f, 0.10f, 0.10f, 0.07f, 0.12f, 0.10f, 0.15f};
    // Rows shrink (down to 15 px) so every stream fits; beyond that the list says how many are hidden.
    const int rowH = juce::jlimit(15, 22, (table_.getHeight() - 26) / juce::jmax(1, static_cast<int>(rows_.size())));
    auto drawRow = [&](int y, const juce::String* cells, juce::Colour c, bool bold) {
        int x = table_.getX();
        g.setFont(juce::Font(juce::FontOptions(juce::jmin(13.0f, static_cast<float>(rowH) - 3.0f), bold ? juce::Font::bold : juce::Font::plain)));
        for (int k = 0; k < 8; ++k)
        {
            const int w = static_cast<int>(cols[k] * static_cast<float>(table_.getWidth()));
            g.setColour(c);
            g.drawText(cells[k], x + 4, y, w - 8, rowH, juce::Justification::centredLeft, true);
            x += w;
        }
    };
    juce::String hdr[8];
    for (int k = 0; k < 8; ++k) hdr[k] = heads[k];
    g.setColour(colours::panel);
    g.fillRect(table_);
    drawRow(table_.getY(), hdr, colours::textDim, true);
    int y = table_.getY() + 24;
    if (rows_.empty())
    {
        g.setColour(colours::textDim);
        g.setFont(juce::Font(juce::FontOptions(13.0f)));
        g.drawText("No devices assigned - the engine runs on its internal clock.", table_.withTrimmedTop(24).reduced(8), juce::Justification::topLeft, true);
    }
    for (size_t i = 0; i < rows_.size(); ++i)
    {
        const auto& r = rows_[i];
        if (y + rowH > table_.getBottom())
        {
            g.setColour(colours::textDim);
            g.drawText("+ " + juce::String(static_cast<int>(rows_.size() - i)) + " more (full list in SAVE REPORT)", table_.getX() + 4, y - rowH,
                       table_.getWidth() - 8, rowH, juce::Justification::centredRight, false);
            break;
        }
        const juce::String cells[8] = {r.role, r.device, r.state, r.sync, r.ppm, r.fill, r.xruns, r.format};
        drawRow(y, cells, !r.ok ? colours::warn : r.warn ? colours::error : colours::text, r.master);
        y += rowH;
    }

    // Glitch log (newest first).
    g.setColour(colours::panel);
    g.fillRect(log_);
    g.setColour(colours::textDim);
    g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
    g.drawText("GLITCH LOG", log_.reduced(8, 4).removeFromTop(16), juce::Justification::centredLeft, false);
    g.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::plain)));
    int ly = log_.getY() + 22;
    if (glitches_.empty())
    {
        g.setColour(colours::ok);
        g.drawText("No glitches since start.", log_.getX() + 8, ly, log_.getWidth() - 16, 18, juce::Justification::centredLeft, true);
    }
    for (auto it = glitches_.rbegin(); it != glitches_.rend() && ly + 18 <= log_.getBottom(); ++it, ly += 18)
    {
        const juce::String line = u8(it->utc.substr(11, 12)) + "  " + juce::String(it->kind).paddedRight(' ', 10) + " " + u8(it->device) + "  x" +
                                  juce::String(static_cast<juce::int64>(it->count)) + "  fill " + juce::String(it->fill, 0) + "/" + juce::String(it->target, 0) +
                                  "  " + juce::String(it->ppm, 1) + " ppm  load " + juce::String(it->load * 100.0, 0) + "%";
        g.setColour(it->kind == "tick-stall" || it->kind == "restart" ? colours::error : colours::warn);
        g.drawText(line, log_.getX() + 8, ly, log_.getWidth() - 16, 18, juce::Justification::centredLeft, true);
    }
}

void DiagnosticsView::resized()
{
    auto b = getLocalBounds().reduced(10);
    tiles_ = b.removeFromTop(56);
    b.removeFromTop(10);
    auto bottom = b.removeFromBottom(30);
    report_.setBounds(bottom.removeFromRight(140).reduced(2, 0));
    restart_.setBounds(bottom.removeFromRight(150).reduced(2, 0));
    disk_.setBounds(bottom.removeFromRight(160).reduced(2, 0));
    note_.setBounds(bottom);
    b.removeFromBottom(8);
    log_ = b.removeFromBottom(juce::jlimit(110, 240, b.getHeight() / 4));
    b.removeFromBottom(8);
    table_ = b;
}

void DiagnosticsView::collectLayoutIssues(std::vector<std::string>& issues) const
{
    if (table_.getHeight() < 24 * 5) issues.push_back("diagnostics: stream table under 4 rows");
    if (tiles_.getWidth() / 7 < 120) issues.push_back("diagnostics: tiles under 120 px");
}

} // namespace pf8::ui
