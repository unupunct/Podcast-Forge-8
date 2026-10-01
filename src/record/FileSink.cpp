#include "record/FileSink.h"

#include <windows.h>

namespace pf8 {

const char* toString(SinkError e) noexcept
{
    switch (e)
    {
        case SinkError::None: return "ok";
        case SinkError::Exists: return "file already exists";
        case SinkError::DiskFull: return "disk full";
        case SinkError::Io: return "write error";
    }
    return "?";
}

namespace {
SinkError mapError(DWORD e)
{
    switch (e)
    {
        case ERROR_FILE_EXISTS:
        case ERROR_ALREADY_EXISTS: return SinkError::Exists;
        case ERROR_DISK_FULL:
        case ERROR_HANDLE_DISK_FULL: return SinkError::DiskFull;
        default: return SinkError::Io;
    }
}
} // namespace

Win32FileSink::~Win32FileSink() { close(); }

SinkError Win32FileSink::create(const std::filesystem::path& path)
{
    close();
    path_ = path;
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW,
                           FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (h == INVALID_HANDLE_VALUE) return mapError(GetLastError());
    handle_ = h;
    size_ = 0;
    return SinkError::None;
}

SinkError Win32FileSink::append(const void* data, size_t n, size_t& written)
{
    written = 0;
    if (!handle_) return SinkError::Io;
    LARGE_INTEGER end;
    end.QuadPart = static_cast<LONGLONG>(size_);
    if (!SetFilePointerEx(handle_, end, nullptr, FILE_BEGIN)) return mapError(GetLastError());
    const auto* p = static_cast<const unsigned char*>(data);
    while (written < n)
    {
        const DWORD chunk = static_cast<DWORD>(std::min<size_t>(n - written, 1u << 30));
        DWORD w = 0;
        if (!WriteFile(handle_, p + written, chunk, &w, nullptr))
        {
            size_ += w;
            written += w;
            return mapError(GetLastError());
        }
        size_ += w;
        written += w;
        if (w == 0) return SinkError::Io;
    }
    return SinkError::None;
}

SinkError Win32FileSink::writeAt(uint64_t offset, const void* data, size_t n)
{
    if (!handle_ || offset + n > size_) return SinkError::Io;
    LARGE_INTEGER pos;
    pos.QuadPart = static_cast<LONGLONG>(offset);
    if (!SetFilePointerEx(handle_, pos, nullptr, FILE_BEGIN)) return mapError(GetLastError());
    DWORD w = 0;
    if (!WriteFile(handle_, data, static_cast<DWORD>(n), &w, nullptr) || w != n) return mapError(GetLastError());
    return SinkError::None;
}

SinkError Win32FileSink::flush()
{
    if (!handle_) return SinkError::Io;
    return FlushFileBuffers(handle_) ? SinkError::None : mapError(GetLastError());
}

void Win32FileSink::close()
{
    if (handle_)
    {
        FlushFileBuffers(handle_);
        CloseHandle(handle_);
        handle_ = nullptr;
    }
}

RecoveryFile::~RecoveryFile() { close(); }

bool RecoveryFile::open(const std::filesystem::path& path)
{
    close();
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    handle_ = h;
    return true;
}

uint64_t RecoveryFile::size() const
{
    LARGE_INTEGER s{};
    if (!handle_ || !GetFileSizeEx(handle_, &s)) return 0;
    return static_cast<uint64_t>(s.QuadPart);
}

bool RecoveryFile::readAt(uint64_t offset, void* dst, size_t n) const
{
    if (!handle_) return false;
    LARGE_INTEGER pos;
    pos.QuadPart = static_cast<LONGLONG>(offset);
    if (!SetFilePointerEx(handle_, pos, nullptr, FILE_BEGIN)) return false;
    DWORD r = 0;
    return ReadFile(handle_, dst, static_cast<DWORD>(n), &r, nullptr) && r == n;
}

bool RecoveryFile::writeAt(uint64_t offset, const void* src, size_t n)
{
    if (!handle_ || offset + n > size()) return false; // header patching only, never extends or truncates
    LARGE_INTEGER pos;
    pos.QuadPart = static_cast<LONGLONG>(offset);
    if (!SetFilePointerEx(handle_, pos, nullptr, FILE_BEGIN)) return false;
    DWORD w = 0;
    return WriteFile(handle_, src, static_cast<DWORD>(n), &w, nullptr) && w == n && FlushFileBuffers(handle_);
}

void RecoveryFile::close()
{
    if (handle_) CloseHandle(handle_);
    handle_ = nullptr;
}

bool writeFileAtomically(const std::filesystem::path& path, const std::string& text)
{
    const auto tmp = std::filesystem::path(path.wstring() + L".tmp");
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD w = 0;
    const bool ok = WriteFile(h, text.data(), static_cast<DWORD>(text.size()), &w, nullptr) && w == text.size() && FlushFileBuffers(h);
    CloseHandle(h);
    if (!ok) return false;
    return MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}

} // namespace pf8
