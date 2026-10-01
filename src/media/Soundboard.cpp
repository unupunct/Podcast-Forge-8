#include "media/Soundboard.h"

#include <algorithm>
#include <cstring>

namespace pf8 {

Soundboard::Soundboard(int sampleRate) : rate_(sampleRate)
{
    for (int i = 0; i < kCartCount; ++i) settings_[static_cast<size_t>(i)].name = "Cart " + std::to_string(i + 1);
}

void Soundboard::setBuffer(int cart, std::shared_ptr<const CartBuffer> b)
{
    if (cart < 0 || cart >= kCartCount) return;
    std::lock_guard lock(ownersMutex_);
    auto& owner = owners_[static_cast<size_t>(cart)];
    if (owner) retired_.emplace_back(owner, renders_.load(std::memory_order_acquire)); // freed once the audio thread let go
    owner = std::move(b);
    active_[static_cast<size_t>(cart)].store(owner && owner->error.empty() ? owner.get() : nullptr, std::memory_order_release);
    state_[static_cast<size_t>(cart)].length.store(owner ? owner->frames : 0);
}

std::shared_ptr<const CartBuffer> Soundboard::buffer(int cart) const
{
    std::lock_guard lock(ownersMutex_);
    return owners_[static_cast<size_t>(cart)];
}

void Soundboard::play(int cart) noexcept { playReq_[static_cast<size_t>(cart)].fetch_add(1, std::memory_order_release); }
void Soundboard::stop(int cart) noexcept { stopReq_[static_cast<size_t>(cart)].fetch_add(1, std::memory_order_release); }
void Soundboard::fadeOut(int cart) noexcept { fadeReq_[static_cast<size_t>(cart)].fetch_add(1, std::memory_order_release); }

void Soundboard::stopAll() noexcept
{
    for (int i = 0; i < kCartCount; ++i) stop(i);
}

void Soundboard::fadeAll() noexcept
{
    for (int i = 0; i < kCartCount; ++i)
        if (state_[static_cast<size_t>(i)].playing.load()) fadeOut(i);
}

void Soundboard::collectGarbage()
{
    std::lock_guard lock(ownersMutex_);
    const uint64_t done = renders_.load(std::memory_order_acquire);
    retired_.erase(std::remove_if(retired_.begin(), retired_.end(),
                                  [this, done](const auto& entry) {
                                      const auto& b = entry.first;
                                      // A render may have picked the buffer up just before it was
                                      // retired: wait until that render has completed.
                                      if (done < entry.second + 2) return false;
                                      for (auto& p : playing_)
                                          if (p.load(std::memory_order_acquire) == b.get()) return false;
                                      for (auto& a : active_)
                                          if (a.load(std::memory_order_acquire) == b.get()) return false;
                                      return true;
                                  }),
                   retired_.end());
}

void Soundboard::render(float* left, float* right, int frames) noexcept
{
    std::memset(left, 0, sizeof(float) * static_cast<size_t>(frames));
    std::memset(right, 0, sizeof(float) * static_cast<size_t>(frames));
    for (size_t c = 0; c < kCartCount; ++c)
    {
        auto& v = voices_[c];
        const auto& s = settings_[c];
        const uint32_t pr = playReq_[c].load(std::memory_order_acquire);
        const uint32_t sr = stopReq_[c].load(std::memory_order_acquire);
        const uint32_t fr = fadeReq_[c].load(std::memory_order_acquire);

        if (pr != v.seenPlay)
        {
            v.seenPlay = pr;
            if (const CartBuffer* b = active_[c].load(std::memory_order_acquire); b && b->frames > 0)
            {
                v.buf = b;
                v.pos = 0;
                v.active = true;
                v.stopAtTarget = false;
                const float fadeIn = std::max(0.0f, s.fadeInMs.get());
                if (fadeIn > 0.5f)
                {
                    v.gain = 0.0f;
                    v.target = 1.0f;
                    v.step = 1.0f / (fadeIn * 0.001f * static_cast<float>(rate_));
                }
                else
                {
                    v.gain = v.target = 1.0f;
                    v.step = 0.0f;
                }
            }
        }
        if (sr != v.seenStop)
        {
            v.seenStop = sr;
            if (v.active)
            {
                v.target = 0.0f;
                v.step = v.gain / (0.005f * static_cast<float>(rate_)); // 5 ms: no click
                v.stopAtTarget = true;
            }
        }
        if (fr != v.seenFade)
        {
            v.seenFade = fr;
            if (v.active)
            {
                const float ms = std::max(5.0f, s.fadeOutMs.get());
                v.target = 0.0f;
                v.step = v.gain / (ms * 0.001f * static_cast<float>(rate_));
                v.stopAtTarget = true;
            }
        }

        if (v.active && v.buf)
        {
            const float vol = std::max(0.0f, s.volume.get());
            const bool loop = s.loop.load(std::memory_order_relaxed);
            const float* src = v.buf->samples.data();
            for (int i = 0; i < frames; ++i)
            {
                if (v.pos >= v.buf->frames)
                {
                    if (loop) v.pos = 0;
                    else
                    {
                        v.active = false;
                        break;
                    }
                }
                if (v.gain != v.target)
                {
                    v.gain = v.gain < v.target ? std::min(v.target, v.gain + v.step) : std::max(v.target, v.gain - v.step);
                    if (v.stopAtTarget && v.gain <= 0.0f)
                    {
                        v.active = false;
                        break;
                    }
                }
                const float g = v.gain * vol;
                left[i] += src[2 * v.pos] * g;
                right[i] += src[2 * v.pos + 1] * g;
                ++v.pos;
            }
        }
        if (!v.active) v.buf = nullptr;
        playing_[c].store(v.buf, std::memory_order_release);
        state_[c].playing.store(v.active, std::memory_order_relaxed);
        state_[c].position.store(v.pos, std::memory_order_relaxed);
    }
    renders_.fetch_add(1, std::memory_order_release);
}

} // namespace pf8
