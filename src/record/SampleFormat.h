#pragma once
// Float → file sample conversion with TPDF dither on integer formats.
#include <cstddef>
#include <cstdint>

namespace pf8 {

enum class BitDepth : uint8_t { Int16 = 16, Int24 = 24, Float32 = 32 };
enum class FileFormat : uint8_t { Wav, Bwf, Flac };

constexpr int bytesPerSample(BitDepth d) noexcept { return d == BitDepth::Int16 ? 2 : d == BitDepth::Int24 ? 3 : 4; }
const char* toString(FileFormat f) noexcept;
const char* extension(FileFormat f) noexcept;

class Ditherer
{
public:
    explicit Ditherer(uint32_t seed = 0x9e3779b9u) : state_(seed ? seed : 1u) {}
    // Converts `samples` floats to little-endian `depth` bytes at dst (samples × bytesPerSample).
    void convert(const float* src, size_t samples, BitDepth depth, uint8_t* dst) noexcept;

private:
    float uniform() noexcept
    {
        state_ ^= state_ << 13;
        state_ ^= state_ >> 17;
        state_ ^= state_ << 5;
        return static_cast<float>(state_) * (1.0f / 4294967296.0f); // [0, 1)
    }
    uint32_t state_;
};

} // namespace pf8
