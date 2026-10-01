#include "engine/WasapiStream.h"

#include <windows.h>
#include <objbase.h>
#include <audioclient.h>
#include <avrt.h>
#include <ksmedia.h>
#include <mmdeviceapi.h>
#include <mmreg.h>
#include <wrl/client.h>

#include <cstdio>
#include <cstring>
#include <immintrin.h>
#include <optional>
#include <thread>
#include <vector>

#include "core/Log.h"
#include "core/RealtimeGuard.h"

namespace pf8 {

using Microsoft::WRL::ComPtr;

namespace {

enum class SampleFormat : uint8_t { Float32, Int32, Int24Packed, Int16 };

std::wstring widen(const std::string& s)
{
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(static_cast<size_t>(n > 0 ? n - 1 : 0), L'\0');
    if (n > 1) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    return w;
}

std::string hr(HRESULT h)
{
    char buf[32];
    std::snprintf(buf, sizeof buf, "0x%08lX", static_cast<unsigned long>(h));
    return buf;
}

bool isFloat(const WAVEFORMATEX* wf)
{
    if (wf->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) return true;
    if (wf->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
        return reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(wf)->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
    return false;
}

std::optional<SampleFormat> formatOf(const WAVEFORMATEX* wf)
{
    if (isFloat(wf)) return wf->wBitsPerSample == 32 ? std::optional(SampleFormat::Float32) : std::nullopt;
    switch (wf->wBitsPerSample)
    {
        case 32: return SampleFormat::Int32;
        case 24: return SampleFormat::Int24Packed;
        case 16: return SampleFormat::Int16;
        default: return std::nullopt;
    }
}

WAVEFORMATEXTENSIBLE makeFormat(int rate, int channels, WORD bits, WORD valid, const GUID& sub)
{
    WAVEFORMATEXTENSIBLE wfx{};
    wfx.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    wfx.Format.nChannels = static_cast<WORD>(channels);
    wfx.Format.nSamplesPerSec = static_cast<DWORD>(rate);
    wfx.Format.wBitsPerSample = bits;
    wfx.Format.nBlockAlign = static_cast<WORD>(channels * bits / 8);
    wfx.Format.nAvgBytesPerSec = wfx.Format.nSamplesPerSec * wfx.Format.nBlockAlign;
    wfx.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
    wfx.Samples.wValidBitsPerSample = valid;
    wfx.dwChannelMask = channels == 1 ? SPEAKER_FRONT_CENTER : (channels == 2 ? KSAUDIO_SPEAKER_STEREO : 0);
    wfx.SubFormat = sub;
    return wfx;
}

REFERENCE_TIME framesToHns(int frames, int rate)
{
    return static_cast<REFERENCE_TIME>(10'000'000.0 * frames / rate + 0.5);
}

void toFloat(const BYTE* src, float* dst, size_t samples, SampleFormat f) noexcept
{
    switch (f)
    {
        case SampleFormat::Float32:
            std::memcpy(dst, src, samples * sizeof(float));
            break;
        case SampleFormat::Int32:
        {
            const auto* s = reinterpret_cast<const int32_t*>(src);
            for (size_t i = 0; i < samples; ++i) dst[i] = static_cast<float>(s[i] * (1.0 / 2147483648.0));
            break;
        }
        case SampleFormat::Int24Packed:
            for (size_t i = 0; i < samples; ++i)
            {
                const BYTE* b = src + 3 * i;
                const int32_t v = static_cast<int32_t>((b[0] << 8) | (b[1] << 16) | (b[2] << 24)) >> 8;
                dst[i] = static_cast<float>(v * (1.0 / 8388608.0));
            }
            break;
        case SampleFormat::Int16:
        {
            const auto* s = reinterpret_cast<const int16_t*>(src);
            for (size_t i = 0; i < samples; ++i) dst[i] = s[i] * (1.0f / 32768.0f);
            break;
        }
    }
}

template <typename T>
T clampToInt(double v, double scale, double lo, double hi) noexcept
{
    double x = v * scale;
    x = x < lo ? lo : (x > hi ? hi : x);
    return static_cast<T>(x < 0 ? x - 0.5 : x + 0.5);
}

void fromFloat(const float* src, BYTE* dst, size_t samples, SampleFormat f) noexcept
{
    switch (f)
    {
        case SampleFormat::Float32:
            std::memcpy(dst, src, samples * sizeof(float));
            break;
        case SampleFormat::Int32:
        {
            auto* d = reinterpret_cast<int32_t*>(dst);
            for (size_t i = 0; i < samples; ++i) d[i] = clampToInt<int32_t>(src[i], 2147483648.0, -2147483648.0, 2147483647.0);
            break;
        }
        case SampleFormat::Int24Packed:
            for (size_t i = 0; i < samples; ++i)
            {
                const int32_t v = clampToInt<int32_t>(src[i], 8388608.0, -8388608.0, 8388607.0);
                dst[3 * i] = static_cast<BYTE>(v);
                dst[3 * i + 1] = static_cast<BYTE>(v >> 8);
                dst[3 * i + 2] = static_cast<BYTE>(v >> 16);
            }
            break;
        case SampleFormat::Int16:
        {
            auto* d = reinterpret_cast<int16_t*>(dst);
            for (size_t i = 0; i < samples; ++i) d[i] = clampToInt<int16_t>(src[i], 32768.0, -32768.0, 32767.0);
            break;
        }
    }
}

} // namespace

struct WasapiStream::Impl
{
    ComPtr<IMMDevice> device;
    ComPtr<IAudioClient> client;
    ComPtr<IAudioRenderClient> render;
    ComPtr<IAudioCaptureClient> capture;
    ComPtr<IAudioClock> clock;
    HANDLE event = nullptr;
    std::thread thread;
    SampleFormat format = SampleFormat::Float32;
    int bytesPerSample = 4;
    std::vector<float> scratch;
    UINT64 clockFrequency = 1;

    ~Impl()
    {
        if (event) CloseHandle(event);
    }
};

const char* toString(StreamMode m) noexcept
{
    switch (m)
    {
        case StreamMode::Shared:           return "Shared";
        case StreamMode::SharedLowLatency: return "Shared (low latency)";
        case StreamMode::Exclusive:        return "Exclusive";
    }
    return "Shared";
}

const char* toString(StreamStatus s) noexcept
{
    switch (s)
    {
        case StreamStatus::Closed:         return "Closed";
        case StreamStatus::Running:        return "Running";
        case StreamStatus::Failed:         return "Failed";
        case StreamStatus::Disconnected:   return "Disconnected";
        case StreamStatus::InUseExclusive: return "In use by another app";
    }
    return "Unknown";
}

WasapiStream::WasapiStream(StreamConfig config, StreamCallback* callback)
    : config_(std::move(config)), callback_(callback), impl_(std::make_unique<Impl>())
{
}

WasapiStream::~WasapiStream() { stop(); }

const char* WasapiStream::sampleFormatName() const noexcept
{
    switch (impl_->format)
    {
        case SampleFormat::Float32:     return "float32";
        case SampleFormat::Int32:       return "int32";
        case SampleFormat::Int24Packed: return "int24";
        case SampleFormat::Int16:       return "int16";
    }
    return "?";
}

bool WasapiStream::open(std::string& error)
{
    auto& d = *impl_;
    ComPtr<IMMDeviceEnumerator> enumerator;
    HRESULT h = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator));
    if (FAILED(h)) { error = "MMDeviceEnumerator " + hr(h); status_ = StreamStatus::Failed; return false; }

    h = enumerator->GetDevice(widen(config_.endpointId).c_str(), &d.device);
    if (FAILED(h)) { error = "endpoint not found " + hr(h); status_ = StreamStatus::Disconnected; return false; }
    DWORD state = 0;
    d.device->GetState(&state);
    if (state != DEVICE_STATE_ACTIVE) { error = "endpoint not active"; status_ = StreamStatus::Disconnected; return false; }

    auto activate = [&]() -> HRESULT {
        d.client.Reset();
        return d.device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(d.client.GetAddressOf()));
    };
    if (FAILED(h = activate())) { error = "Activate " + hr(h); status_ = StreamStatus::Failed; return false; }

    WAVEFORMATEX* mix = nullptr;
    if (FAILED(h = d.client->GetMixFormat(&mix)) || !mix) { error = "GetMixFormat " + hr(h); status_ = StreamStatus::Failed; return false; }
    const int mixRate = static_cast<int>(mix->nSamplesPerSec);
    const int mixChannels = mix->nChannels;

    bool initialised = false;
    if (config_.mode == StreamMode::Exclusive)
    {
        struct Try { WORD bits, valid; GUID sub; SampleFormat f; };
        const Try tries[] = {{32, 32, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT, SampleFormat::Float32},
                             {32, 24, KSDATAFORMAT_SUBTYPE_PCM, SampleFormat::Int32},
                             {24, 24, KSDATAFORMAT_SUBTYPE_PCM, SampleFormat::Int24Packed},
                             {16, 16, KSDATAFORMAT_SUBTYPE_PCM, SampleFormat::Int16}};
        REFERENCE_TIME defPeriod = 0, minPeriod = 0;
        d.client->GetDevicePeriod(&defPeriod, &minPeriod);
        for (const auto& t : tries)
        {
            auto wfx = makeFormat(config_.requestedRate, mixChannels, t.bits, t.valid, t.sub);
            if (d.client->IsFormatSupported(AUDCLNT_SHAREMODE_EXCLUSIVE, &wfx.Format, nullptr) != S_OK) continue;
            REFERENCE_TIME period = framesToHns(config_.requestedFrames, config_.requestedRate);
            if (period < minPeriod) period = minPeriod;
            h = d.client->Initialize(AUDCLNT_SHAREMODE_EXCLUSIVE, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, period, period,
                                     &wfx.Format, nullptr);
            if (h == AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED)
            {
                UINT32 frames = 0;
                d.client->GetBufferSize(&frames);
                period = framesToHns(static_cast<int>(frames), config_.requestedRate);
                if (FAILED(activate())) break;
                h = d.client->Initialize(AUDCLNT_SHAREMODE_EXCLUSIVE, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, period, period,
                                         &wfx.Format, nullptr);
            }
            if (h == AUDCLNT_E_DEVICE_IN_USE)
            {
                CoTaskMemFree(mix);
                error = "device in use by another application (exclusive)";
                status_ = StreamStatus::InUseExclusive;
                return false;
            }
            if (SUCCEEDED(h))
            {
                d.format = t.f;
                d.bytesPerSample = t.bits / 8;
                sampleRate_ = config_.requestedRate;
                channels_ = mixChannels;
                grantedMode_ = StreamMode::Exclusive;
                periodFrames_ = static_cast<int>(period * sampleRate_ / 10'000'000);
                initialised = true;
                break;
            }
            if (FAILED(activate())) break;
        }
        if (!initialised)
        {
            CoTaskMemFree(mix);
            error = "no exclusive format accepted at " + std::to_string(config_.requestedRate) + " Hz (" + hr(h) + ")";
            status_ = StreamStatus::Failed;
            return false;
        }
    }
    else
    {
        auto fmt = formatOf(mix);
        if (!fmt)
        {
            CoTaskMemFree(mix);
            error = "unsupported mix format";
            status_ = StreamStatus::Failed;
            return false;
        }
        d.format = *fmt;
        d.bytesPerSample = mix->wBitsPerSample / 8;
        sampleRate_ = mixRate;
        channels_ = mixChannels;

        if (config_.mode == StreamMode::SharedLowLatency)
        {
            ComPtr<IAudioClient3> c3;
            if (SUCCEEDED(d.client.As(&c3)))
            {
                UINT32 def = 0, fund = 0, mn = 0, mx = 0;
                if (SUCCEEDED(c3->GetSharedModeEnginePeriod(mix, &def, &fund, &mn, &mx)) && fund > 0)
                {
                    const UINT32 engineRate = config_.requestedRate > 0 ? static_cast<UINT32>(config_.requestedRate) : 48000u;
                    UINT32 want = static_cast<UINT32>(config_.requestedFrames) * mixRate / engineRate;
                    UINT32 period = ((want + fund - 1) / fund) * fund;
                    period = period < mn ? mn : (period > mx ? mx : period);
                    h = c3->InitializeSharedAudioStream(AUDCLNT_STREAMFLAGS_EVENTCALLBACK, period, mix, nullptr);
                    if (SUCCEEDED(h))
                    {
                        grantedMode_ = StreamMode::SharedLowLatency;
                        periodFrames_ = static_cast<int>(period);
                        initialised = true;
                    }
                    else if (FAILED(activate()))
                    {
                        CoTaskMemFree(mix);
                        error = "re-activate " + hr(h);
                        status_ = StreamStatus::Failed;
                        return false;
                    }
                }
            }
        }
        if (!initialised)
        {
            h = d.client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, 0, 0, mix, nullptr);
            if (FAILED(h))
            {
                CoTaskMemFree(mix);
                error = "Initialize(shared) " + hr(h);
                status_ = StreamStatus::Failed;
                return false;
            }
            REFERENCE_TIME defPeriod = 0, minPeriod = 0;
            d.client->GetDevicePeriod(&defPeriod, &minPeriod);
            grantedMode_ = StreamMode::Shared;
            periodFrames_ = static_cast<int>(defPeriod * sampleRate_ / 10'000'000);
        }
    }
    CoTaskMemFree(mix);

    UINT32 bufferFrames = 0;
    d.client->GetBufferSize(&bufferFrames);
    bufferFrames_ = static_cast<int>(bufferFrames);

    d.event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (FAILED(h = d.client->SetEventHandle(d.event))) { error = "SetEventHandle " + hr(h); status_ = StreamStatus::Failed; return false; }

    if (config_.flow == Flow::Render)
        h = d.client->GetService(IID_PPV_ARGS(&d.render));
    else
        h = d.client->GetService(IID_PPV_ARGS(&d.capture));
    if (FAILED(h)) { error = "GetService " + hr(h); status_ = StreamStatus::Failed; return false; }
    if (SUCCEEDED(d.client->GetService(IID_PPV_ARGS(&d.clock)))) d.clock->GetFrequency(&d.clockFrequency);

    d.scratch.assign(static_cast<size_t>(bufferFrames_) * static_cast<size_t>(channels_) * 2, 0.0f);

    PF8_LOG_INFO("stream", "open id=%s flow=%s mode=%s rate=%d ch=%d fmt=%s period=%d buffer=%d",
                 config_.endpointId.c_str(), toString(config_.flow), toString(grantedMode_), sampleRate_, channels_,
                 sampleFormatName(), periodFrames_, bufferFrames_);
    return true;
}

bool WasapiStream::start()
{
    auto& d = *impl_;
    if (!d.client) return false;
    stopRequested_ = false;

    if (config_.flow == Flow::Render)
    {
        // Pre-fill with silence so the first period doesn't glitch.
        BYTE* data = nullptr;
        if (SUCCEEDED(d.render->GetBuffer(static_cast<UINT32>(bufferFrames_), &data)))
            d.render->ReleaseBuffer(static_cast<UINT32>(bufferFrames_), AUDCLNT_BUFFERFLAGS_SILENT);
    }
    const HRESULT h = d.client->Start();
    if (FAILED(h))
    {
        PF8_LOG_ERROR("stream", "Start failed id=%s hr=%s", config_.endpointId.c_str(), hr(h).c_str());
        status_ = StreamStatus::Failed;
        return false;
    }
    status_ = StreamStatus::Running;
    d.thread = std::thread([this] { threadMain(); });
    return true;
}

void WasapiStream::stop()
{
    auto& d = *impl_;
    stopRequested_ = true;
    if (d.event) SetEvent(d.event);
    if (d.thread.joinable()) d.thread.join();
    if (d.client) d.client->Stop();
    if (status_ == StreamStatus::Running) status_ = StreamStatus::Closed;
}

void WasapiStream::threadMain()
{
    auto& d = *impl_;
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    DWORD taskIndex = 0;
    HANDLE mmcss = AvSetMmThreadCharacteristicsW(L"Pro Audio", &taskIndex);
    if (mmcss) AvSetMmThreadPriority(mmcss, AVRT_PRIORITY_HIGH);
    // Flush denormals to zero on this thread (FTZ | DAZ).
    _mm_setcsr(_mm_getcsr() | 0x8040);

    const bool exclusive = grantedMode_ == StreamMode::Exclusive;
    StreamStatus exitStatus = StreamStatus::Closed;
    {
        rt::ScopedRealtime rtScope;
        while (!stopRequested_.load(std::memory_order_relaxed))
        {
            const DWORD w = WaitForSingleObject(d.event, 200);
            if (stopRequested_.load(std::memory_order_relaxed)) break;
            if (w == WAIT_TIMEOUT)
            {
                stats_.glitches.fetch_add(1, std::memory_order_relaxed);
                continue;
            }

            HRESULT h = S_OK;
            if (config_.flow == Flow::Render)
            {
                UINT32 padding = 0;
                if (!exclusive) h = d.client->GetCurrentPadding(&padding);
                if (SUCCEEDED(h))
                {
                    const UINT32 frames = static_cast<UINT32>(bufferFrames_) - padding;
                    if (frames > 0)
                    {
                        BYTE* data = nullptr;
                        h = d.render->GetBuffer(frames, &data);
                        if (SUCCEEDED(h))
                        {
                            const size_t samples = static_cast<size_t>(frames) * static_cast<size_t>(channels_);
                            std::memset(d.scratch.data(), 0, samples * sizeof(float));
                            UINT64 pos = 0, qpc = 0;
                            if (d.clock) d.clock->GetPosition(&pos, &qpc);
                            callback_->onStreamBlock(d.scratch.data(), static_cast<int>(frames), channels_,
                                                     static_cast<int64_t>(qpc),
                                                     d.clockFrequency ? pos * static_cast<UINT64>(sampleRate_) / d.clockFrequency : 0);
                            fromFloat(d.scratch.data(), data, samples, d.format);
                            h = d.render->ReleaseBuffer(frames, 0);
                            stats_.framesProcessed.fetch_add(frames, std::memory_order_relaxed);
                            stats_.lastQpc100ns.store(static_cast<int64_t>(qpc), std::memory_order_relaxed);
                        }
                    }
                }
            }
            else
            {
                UINT32 packet = 0;
                while (SUCCEEDED(h = d.capture->GetNextPacketSize(&packet)) && packet > 0)
                {
                    BYTE* data = nullptr;
                    UINT32 frames = 0;
                    DWORD flags = 0;
                    UINT64 devPos = 0, qpc = 0;
                    h = d.capture->GetBuffer(&data, &frames, &flags, &devPos, &qpc);
                    if (FAILED(h)) break;
                    const size_t samples = static_cast<size_t>(frames) * static_cast<size_t>(channels_);
                    if (samples <= d.scratch.size())
                    {
                        if (flags & AUDCLNT_BUFFERFLAGS_SILENT)
                            std::memset(d.scratch.data(), 0, samples * sizeof(float));
                        else
                            toFloat(data, d.scratch.data(), samples, d.format);
                        if (flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY)
                            stats_.glitches.fetch_add(1, std::memory_order_relaxed);
                        callback_->onStreamBlock(d.scratch.data(), static_cast<int>(frames), channels_,
                                                 static_cast<int64_t>(qpc), devPos);
                    }
                    else
                    {
                        stats_.glitches.fetch_add(1, std::memory_order_relaxed);
                    }
                    d.capture->ReleaseBuffer(frames);
                    stats_.framesProcessed.fetch_add(frames, std::memory_order_relaxed);
                    stats_.lastQpc100ns.store(static_cast<int64_t>(qpc), std::memory_order_relaxed);
                    stats_.lastDevicePosition.store(devPos, std::memory_order_relaxed);
                }
            }
            stats_.callbacks.fetch_add(1, std::memory_order_relaxed);

            if (FAILED(h))
            {
                exitStatus = (h == AUDCLNT_E_DEVICE_INVALIDATED) ? StreamStatus::Disconnected : StreamStatus::Failed;
                break;
            }
        }
    }

    if (exitStatus != StreamStatus::Closed)
    {
        status_ = exitStatus;
        PF8_LOG_WARN("stream", "stream stopped id=%s status=%s", config_.endpointId.c_str(), toString(exitStatus));
        callback_->onStreamError(exitStatus);
    }
    if (mmcss) AvRevertMmThreadCharacteristics(mmcss);
    CoUninitialize();
}

} // namespace pf8
