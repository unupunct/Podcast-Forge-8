#include "record/SampleFormat.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace pf8 {

const char* toString(FileFormat f) noexcept
{
    switch (f)
    {
        case FileFormat::Wav: return "WAV";
        case FileFormat::Bwf: return "Broadcast WAV";
        case FileFormat::Flac: return "FLAC";
    }
    return "?";
}

const char* extension(FileFormat f) noexcept { return f == FileFormat::Flac ? ".flac" : ".wav"; }

void Ditherer::convert(const float* src, size_t samples, BitDepth depth, uint8_t* dst) noexcept
{
    switch (depth)
    {
        case BitDepth::Float32:
            std::memcpy(dst, src, samples * sizeof(float));
            return;
        case BitDepth::Int16:
            for (size_t i = 0; i < samples; ++i)
            {
                const float tpdf = uniform() - uniform(); // ±1 LSB triangular
                const float v = std::clamp(src[i] * 32767.0f + tpdf, -32768.0f, 32767.0f);
                const auto s = static_cast<int16_t>(std::lrint(v));
                dst[2 * i] = static_cast<uint8_t>(s);
                dst[2 * i + 1] = static_cast<uint8_t>(s >> 8);
            }
            return;
        case BitDepth::Int24:
            for (size_t i = 0; i < samples; ++i)
            {
                const float tpdf = uniform() - uniform();
                const float v = std::clamp(src[i] * 8388607.0f + tpdf, -8388608.0f, 8388607.0f);
                const auto s = static_cast<int32_t>(std::lrint(v));
                dst[3 * i] = static_cast<uint8_t>(s);
                dst[3 * i + 1] = static_cast<uint8_t>(s >> 8);
                dst[3 * i + 2] = static_cast<uint8_t>(s >> 16);
            }
            return;
    }
}

} // namespace pf8
