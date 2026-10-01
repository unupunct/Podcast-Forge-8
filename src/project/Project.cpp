#include "project/Project.h"

#include <juce_core/juce_core.h>

#include <windows.h>

#include <algorithm>
#include <map>
#include <ctime>
#include <fstream>
#include <sstream>
#include <system_error>

#include "core/Log.h"
#include "core/Paths.h"
#include "record/FileSink.h"
#include "record/Journal.h"
#include "record/Markers.h"

namespace pf8::project {
namespace fs = std::filesystem;
namespace {

std::string nowUtc()
{
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    gmtime_s(&tm, &t);
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

bool writeAtomic(const fs::path& file, const std::string& text, std::string& error)
{
    // The previous save is kept as Project.json.bak (a second copy if the new one is ever bad).
    std::error_code ec;
    if (fs::is_regular_file(file, ec)) CopyFileW(file.c_str(), (file.wstring() + L".bak").c_str(), FALSE);
    // Temp file flushed to the disk (FlushFileBuffers) before it replaces the old one, so a power
    // loss can never leave an empty Project.json behind.
    if (!writeFileAtomically(file, text))
    {
        error = "cannot write " + paths::utf8(file) + " (error " + std::to_string(GetLastError()) + ")";
        return false;
    }
    return true;
}

std::optional<json::Value> readJson(const fs::path& file, std::string& error)
{
    std::ifstream in(file, std::ios::binary);
    if (!in)
    {
        error = "cannot open " + paths::utf8(file);
        return std::nullopt;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    std::string perr;
    auto v = json::parse(ss.str(), &perr);
    if (!v || !v->isObject())
    {
        error = paths::utf8(file) + " is not valid JSON (" + perr + ")";
        return std::nullopt;
    }
    return v;
}

json::Value document(const std::string& name, const std::string& created, const json::Value& state, const fs::path& dir)
{
    json::Object root;
    root["format"] = kFormatId;
    root["version"] = 1;
    root["name"] = name;
    root["createdUtc"] = created;
    root["savedUtc"] = nowUtc();
    root["state"] = state;
    json::Array sessions;
    for (const auto& s : listSessions(dir))
    {
        json::Object o;
        o["folder"] = paths::utf8(s.dir.filename());
        o["startedUtc"] = s.startedUtc;
        o["state"] = s.state;
        o["seconds"] = s.sampleRate > 0 ? static_cast<double>(s.frames) / s.sampleRate : 0.0;
        o["markers"] = s.markers;
        sessions.emplace_back(std::move(o));
    }
    root["sessions"] = std::move(sessions); // informational index; the folders are the truth
    return json::Value(std::move(root));
}

std::optional<fs::path> uniqueFolder(const fs::path& parent, const std::string& name, std::string& error)
{
    std::error_code ec;
    fs::create_directories(parent, ec);
    const std::string base = sanitizeProjectName(name);
    for (int i = 1; i < 1000; ++i)
    {
        const fs::path dir = parent / paths::fromUtf8(i == 1 ? base : base + " (" + std::to_string(i) + ")");
        if (fs::exists(dir, ec)) continue;
        if (fs::create_directory(dir, ec)) return dir;
    }
    error = "cannot create a project folder in " + paths::utf8(parent);
    return std::nullopt;
}

} // namespace

std::string sanitizeProjectName(const std::string& name)
{
    std::string out;
    for (unsigned char c : name)
    {
        if (c < 32 || std::string_view("<>:\"/\\|?*").find(static_cast<char>(c)) != std::string_view::npos) out += '_';
        else out += static_cast<char>(c);
    }
    while (!out.empty() && (out.back() == ' ' || out.back() == '.')) out.pop_back();
    while (!out.empty() && out.front() == ' ') out.erase(out.begin());
    if (out.empty()) out = "Untitled Project";
    if (out.size() > 120)
    {
        size_t n = 120;
        while (n > 0 && (static_cast<unsigned char>(out[n]) & 0xC0) == 0x80) --n; // never split a UTF-8 character
        out.resize(n);
    }
    return out;
}

bool isProjectDir(const fs::path& dir)
{
    std::error_code ec;
    return fs::is_regular_file(dir / kProjectFile, ec);
}

std::optional<fs::path> createProject(const fs::path& parent, const std::string& name, const json::Value& state, std::string& error)
{
    auto dir = uniqueFolder(parent, name, error);
    if (!dir) return std::nullopt;
    if (!writeAtomic(*dir / kProjectFile, json::serialize(document(name, nowUtc(), state, *dir), true), error)) return std::nullopt;
    PF8_LOG_INFO("project", "project.create dir=%s", paths::utf8(*dir).c_str());
    return dir;
}

std::optional<fs::path> saveProjectAs(const fs::path& parent, const std::string& name, const json::Value& state, std::string& error)
{
    return createProject(parent, name, state, error);
}

bool saveProject(const fs::path& dir, const std::string& name, const json::Value& state, std::string& error)
{
    std::error_code ec;
    fs::create_directories(dir, ec);
    std::string created = nowUtc();
    std::string ignored;
    if (auto old = readJson(dir / kProjectFile, ignored)) created = (*old)["createdUtc"].asString(created);
    if (!writeAtomic(dir / kProjectFile, json::serialize(document(name, created, state, dir), true), error)) return false;
    PF8_LOG_INFO("project", "project.save dir=%s", paths::utf8(dir).c_str());
    return true;
}

std::optional<LoadedProject> loadProject(const fs::path& dir, std::string& error)
{
    auto v = readJson(dir / kProjectFile, error);
    if (!v || (*v)["format"].asString() != kFormatId)
    {
        // Damaged Project.json: fall back to the previous save.
        std::string bakError;
        if (auto bak = readJson(fs::path((dir / kProjectFile).wstring() + L".bak"), bakError); bak && (*bak)["format"].asString() == kFormatId)
        {
            PF8_LOG_WARN("project", "Project.json unreadable (%s); opened Project.json.bak", error.c_str());
            v = bak;
        }
    }
    if (!v) return std::nullopt;
    if ((*v)["format"].asString() != kFormatId)
    {
        error = paths::utf8(dir / kProjectFile) + " is not a Podcast Forge 8 project";
        return std::nullopt;
    }
    LoadedProject p;
    p.dir = dir;
    p.name = (*v)["name"].asString(paths::utf8(dir.filename()));
    p.createdUtc = (*v)["createdUtc"].asString();
    p.savedUtc = (*v)["savedUtc"].asString();
    p.state = (*v)["state"];
    PF8_LOG_INFO("project", "project.open dir=%s", paths::utf8(dir).c_str());
    return p;
}

std::vector<SessionInfo> listSessions(const fs::path& dir)
{
    std::vector<SessionInfo> out;
    std::error_code ec;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
    {
        if (!it->is_directory(ec)) continue;
        const auto name = it->path().filename().wstring();
        if (name.rfind(L"Session_", 0) != 0) continue;
        SessionInfo s;
        s.dir = it->path();
        if (auto j = Journal::load(s.dir / "Metadata" / "Journal.json"))
        {
            s.startedUtc = j->startedUtc;
            s.state = j->state;
            s.sampleRate = j->sampleRate;
            for (const auto& t : j->tracks) s.frames = std::max<uint64_t>(s.frames, t.samplesWritten);
        }
        if (std::ifstream mf(s.dir / "Metadata" / "Markers.json", std::ios::binary); mf)
        {
            std::stringstream ms;
            ms << mf.rdbuf();
            MarkerList m;
            if (m.loadJson(ms.str())) s.markers = static_cast<int>(m.all().size());
        }
        out.push_back(std::move(s));
    }
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.dir.filename() < b.dir.filename(); });
    return out;
}

bool archiveProject(const fs::path& dir, const fs::path& zipFile, std::string& error)
{
    std::error_code ec;
    if (fs::exists(zipFile, ec))
    {
        error = paths::utf8(zipFile) + " already exists";
        return false;
    }
    const juce::File root(juce::String(dir.wstring().c_str()));
    if (!root.isDirectory())
    {
        error = "not a folder: " + paths::utf8(dir);
        return false;
    }
    juce::ZipFile::Builder zip;
    int files = 0;
    int64_t totalBytes = 0;
    std::map<juce::String, int64_t> expected; // entry name -> size, verified after writing
    for (const auto& entry : juce::RangedDirectoryIterator(root, true, "*", juce::File::findFiles))
    {
        const auto f = entry.getFile();
        const auto ext = f.getFileExtension().toLowerCase();
        const bool audio = ext == ".wav" || ext == ".flac" || ext == ".mp3" || ext == ".rf64";
        const auto name = f.getRelativePathFrom(root.getParentDirectory()).replaceCharacter('\\', '/');
        zip.addFile(f, audio ? 0 : 6, name);
        expected[name] = f.getSize();
        totalBytes += f.getSize();
        ++files;
    }
    // The zip writer has no ZIP64: beyond 4 GiB offsets and sizes would wrap and the archive would
    // be unreadable. Refuse instead of producing a corrupt "successful" archive.
    constexpr int64_t kZipLimit = (int64_t{1} << 32) - (int64_t{64} << 20); // 64 MiB headroom for headers
    if (totalBytes >= kZipLimit)
    {
        error = "the project is " + std::to_string(totalBytes >> 20) +
                " MB; zip archives are limited to 4 GB. Copy the project folder instead (it is self-contained).";
        return false;
    }
    const juce::File out(juce::String(zipFile.wstring().c_str()));
    const juce::File tmp = out.getSiblingFile(out.getFileName() + ".part");
    {
        std::unique_ptr<juce::FileOutputStream> os(tmp.createOutputStream());
        if (!os || os->failedToOpen())
        {
            error = "cannot write " + paths::utf8(zipFile);
            return false;
        }
        // A FileOutputStream appends to an existing file: start our temp file from empty.
        os->setPosition(0);
        os->truncate();
        double progress = 0;
        if (!zip.writeToStream(*os, &progress))
        {
            os.reset();
            tmp.deleteFile(); // our own partial archive, never project data
            error = "writing the archive failed (disk full?)";
            return false;
        }
        os->flush();
        if (os->getStatus().failed())
        {
            os.reset();
            tmp.deleteFile();
            error = "writing the archive failed (disk full?)";
            return false;
        }
    }
    // Read it back: every file present with its full size, before calling it a success.
    {
        juce::ZipFile check(tmp);
        bool ok = check.getNumEntries() == files;
        for (int i = 0; ok && i < check.getNumEntries(); ++i)
        {
            const auto* e = check.getEntry(i);
            const auto it = expected.find(e->filename);
            ok = it != expected.end() && it->second == e->uncompressedSize;
        }
        if (!ok)
        {
            tmp.deleteFile();
            error = "the archive did not verify (its contents differ from the project); nothing was changed";
            return false;
        }
    }
    if (!tmp.moveFileTo(out))
    {
        error = "cannot finish " + paths::utf8(zipFile);
        return false;
    }
    PF8_LOG_INFO("project", "project.archive files=%d zip=%s", files, paths::utf8(zipFile).c_str());
    return true;
}

} // namespace pf8::project
