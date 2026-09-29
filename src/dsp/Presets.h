#pragma once
// Compressor and EQ presets (DSP.md §3). Applying a preset writes the parameters and switches the
// block on; nothing else is touched.
#include <array>
#include <string_view>

#include "dsp/DspParams.h"

namespace pf8::dsp {

enum class CompPreset : uint8_t { Speech, Podcast, AggressiveVoice, SoftVoice, Radio };
enum class EqPreset : uint8_t { MaleVoice, FemaleVoice, DeepVoice, BrightVoice, RadioVoice };

constexpr std::array<CompPreset, 5> kCompPresets{CompPreset::Speech, CompPreset::Podcast, CompPreset::AggressiveVoice,
                                                 CompPreset::SoftVoice, CompPreset::Radio};
constexpr std::array<EqPreset, 5> kEqPresets{EqPreset::MaleVoice, EqPreset::FemaleVoice, EqPreset::DeepVoice,
                                             EqPreset::BrightVoice, EqPreset::RadioVoice};

std::string_view name(CompPreset p) noexcept;
std::string_view name(EqPreset p) noexcept;

struct CompPresetValues { float threshold, ratio, attack, release, knee, makeup; };
struct EqBandValues { EqBandType type; float freq, gainDb, q; };

CompPresetValues values(CompPreset p) noexcept;
std::array<EqBandValues, 4> values(EqPreset p) noexcept;
float suggestedHpfHz(EqPreset p) noexcept;

void apply(CompPreset p, ChannelDspParams& params) noexcept;
void apply(EqPreset p, ChannelDspParams& params) noexcept; // also sets the suggested HPF

} // namespace pf8::dsp
