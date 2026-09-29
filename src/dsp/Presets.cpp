#include "dsp/Presets.h"

namespace pf8::dsp {

std::string_view name(CompPreset p) noexcept
{
    switch (p)
    {
        case CompPreset::Speech: return "Speech";
        case CompPreset::Podcast: return "Podcast";
        case CompPreset::AggressiveVoice: return "Aggressive Voice";
        case CompPreset::SoftVoice: return "Soft Voice";
        case CompPreset::Radio: return "Radio";
    }
    return "?";
}

std::string_view name(EqPreset p) noexcept
{
    switch (p)
    {
        case EqPreset::MaleVoice: return "Male Voice";
        case EqPreset::FemaleVoice: return "Female Voice";
        case EqPreset::DeepVoice: return "Deep Voice";
        case EqPreset::BrightVoice: return "Bright Voice";
        case EqPreset::RadioVoice: return "Radio Voice";
    }
    return "?";
}

CompPresetValues values(CompPreset p) noexcept
{
    switch (p)
    {
        case CompPreset::Speech: return {-18, 3, 10, 120, 6, 4};
        case CompPreset::Podcast: return {-20, 4, 5, 100, 6, 6};
        case CompPreset::AggressiveVoice: return {-24, 8, 2, 60, 3, 9};
        case CompPreset::SoftVoice: return {-16, 2, 20, 200, 10, 3};
        case CompPreset::Radio: return {-26, 6, 1, 50, 4, 10};
    }
    return {-20, 4, 5, 100, 6, 6};
}

std::array<EqBandValues, 4> values(EqPreset p) noexcept
{
    using T = EqBandType;
    switch (p)
    {
        case EqPreset::MaleVoice:
            return {{{T::LowShelf, 120, 1.5f, 0.7f}, {T::Peak, 300, -2.5f, 1.2f}, {T::Peak, 3500, 2.5f, 1.0f}, {T::HighShelf, 10000, 1.5f, 0.7f}}};
        case EqPreset::FemaleVoice:
            return {{{T::LowShelf, 180, 1.0f, 0.7f}, {T::Peak, 400, -2.0f, 1.2f}, {T::Peak, 5000, 2.0f, 1.0f}, {T::HighShelf, 12000, 2.0f, 0.7f}}};
        case EqPreset::DeepVoice:
            return {{{T::LowShelf, 100, -1.5f, 0.7f}, {T::Peak, 250, -3.5f, 1.0f}, {T::Peak, 2500, 3.0f, 0.9f}, {T::HighShelf, 9000, 2.0f, 0.7f}}};
        case EqPreset::BrightVoice:
            return {{{T::LowShelf, 150, 2.0f, 0.7f}, {T::Peak, 500, -1.0f, 1.0f}, {T::Peak, 6000, -2.0f, 1.5f}, {T::HighShelf, 11000, -1.5f, 0.7f}}};
        case EqPreset::RadioVoice:
            return {{{T::LowShelf, 90, 3.0f, 0.7f}, {T::Peak, 350, -3.0f, 1.0f}, {T::Peak, 3000, 4.0f, 0.8f}, {T::HighShelf, 8000, 3.0f, 0.7f}}};
    }
    return {};
}

float suggestedHpfHz(EqPreset p) noexcept
{
    switch (p)
    {
        case EqPreset::MaleVoice:
        case EqPreset::DeepVoice: return 70.0f;
        case EqPreset::FemaleVoice:
        case EqPreset::BrightVoice: return 100.0f;
        case EqPreset::RadioVoice: return 80.0f;
    }
    return 80.0f;
}

void apply(CompPreset p, ChannelDspParams& params) noexcept
{
    const auto v = values(p);
    params.compThresholdDb.set(v.threshold);
    params.compRatio.set(v.ratio);
    params.compAttackMs.set(v.attack);
    params.compReleaseMs.set(v.release);
    params.compKneeDb.set(v.knee);
    params.compMakeupDb.set(v.makeup);
    params.compAutoMakeup = false;
    params.compOn = true;
}

void apply(EqPreset p, ChannelDspParams& params) noexcept
{
    const auto bands = values(p);
    for (size_t i = 0; i < 4; ++i)
    {
        params.eq[i].on = true;
        params.eq[i].type = bands[i].type;
        params.eq[i].freq.set(bands[i].freq);
        params.eq[i].gainDb.set(bands[i].gainDb);
        params.eq[i].q.set(bands[i].q);
    }
    params.hpfHz.set(suggestedHpfHz(p));
    params.hpfOn = true;
    params.eqOn = true;
}

} // namespace pf8::dsp
