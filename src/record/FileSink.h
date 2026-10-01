#pragma once
// File output abstraction used by the track writers, so disk failures can be simulated in tests.
// The only way to open a file is CREATE_NEW: an existing file is never opened for writing here.
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace pf8 {

enum class SinkError : uint8_t { None, Exists, DiskFull, Io };
const char* toString(SinkError e) noexcept;

class IFileSink
{
public:
    virtual ~IFileSink() = default;
    virtual SinkError create(const std::filesystem::path& path) = 0; // CREATE_NEW only
    // Appends at the end of the file. Returns bytes written (≤ n) and the error, if any.
    virtual SinkError append(const void* data, size_t n, size_t& written) = 0;
    // Overwrites bytes at an offset that has already been written (header patching).
    virtual SinkError writeAt(uint64_t offset, const void* data, size_t n) = 0;
    virtual SinkError flush() = 0; // FlushFileBuffers
    virtual void close() = 0;
    virtual uint64_t size() const = 0;
    virtual const std::filesystem::path& path() const = 0;
};

class SinkFactory
{
public:
    virtual ~SinkFactory() = default;
    virtual std::unique_ptr<IFileSink> make() = 0;
};

// Real files: CreateFileW(CREATE_NEW, share read), sequential writes.
class Win32FileSink : public IFileSink
{
public:
    ~Win32FileSink() override;
    SinkError create(const std::filesystem::path& path) override;
    SinkError append(const void* data, size_t n, size_t& written) override;
    SinkError writeAt(uint64_t offset, const void* data, size_t n) override;
    SinkError flush() override;
    void close() override;
    uint64_t size() const override { return size_; }
    const std::filesystem::path& path() const override { return path_; }

private:
    void* handle_ = nullptr;
    uint64_t size_ = 0;
    std::filesystem::path path_;
};

class Win32SinkFactory : public SinkFactory
{
public:
    std::unique_ptr<IFileSink> make() override { return std::make_unique<Win32FileSink>(); }
};

// Opens a file for read/write *only* to patch a header during recovery. Never truncates.
class RecoveryFile
{
public:
    ~RecoveryFile();
    bool open(const std::filesystem::path& path);
    uint64_t size() const;
    bool readAt(uint64_t offset, void* dst, size_t n) const;
    bool writeAt(uint64_t offset, const void* src, size_t n);
    void close();

private:
    void* handle_ = nullptr;
};

// Writes `text` atomically: <path>.tmp then MoveFileEx(REPLACE_EXISTING | WRITE_THROUGH).
// Used for metadata (journal, markers) only — never for audio.
bool writeFileAtomically(const std::filesystem::path& path, const std::string& text);

} // namespace pf8
