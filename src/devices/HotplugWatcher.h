#pragma once
// Receives Windows endpoint notifications (IMMNotificationClient) and forwards a single "something
// changed" signal. The callback runs on a Windows COM thread and must only post work elsewhere.
#include <functional>

namespace pf8 {

class HotplugWatcher
{
public:
    using Callback = std::function<void()>;

    // Registers immediately; the calling thread must have COM initialised.
    explicit HotplugWatcher(Callback onChange);
    ~HotplugWatcher();
    HotplugWatcher(const HotplugWatcher&) = delete;
    HotplugWatcher& operator=(const HotplugWatcher&) = delete;

    bool registered() const noexcept;

private:
    struct Impl;
    Impl* impl_;
};

} // namespace pf8
