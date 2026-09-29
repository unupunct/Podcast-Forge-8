#include "devices/HotplugWatcher.h"

#include <windows.h>
#include <objbase.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>

#include <atomic>

#include "core/Log.h"

namespace pf8 {

using Microsoft::WRL::ComPtr;

namespace {

class NotificationClient : public IMMNotificationClient
{
public:
    explicit NotificationClient(HotplugWatcher::Callback cb) : cb_(std::move(cb)) {}

    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override
    {
        const ULONG r = --refs_;
        if (r == 0) delete this;
        return r;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override
    {
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IMMNotificationClient))
        {
            *out = static_cast<IMMNotificationClient*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }

    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR, DWORD) override { fire(); return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR) override { fire(); return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR) override { fire(); return S_OK; }
    // Default-device changes never move a Podcast Forge channel: ignored.
    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow, ERole, LPCWSTR) override { return S_OK; }
    // Format changes (e.g. the user changed the shared-mode rate in Sound settings) matter.
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY key) override
    {
        static const PROPERTYKEY kDeviceFormat = {{0xf19f064d, 0x082c, 0x4e27, {0xbc, 0x73, 0x68, 0x82, 0xa1, 0xbb, 0x8e, 0x4c}}, 0};
        if (IsEqualPropertyKey(key, kDeviceFormat)) fire();
        return S_OK;
    }

    void disable() { enabled_ = false; }

private:
    void fire()
    {
        if (enabled_ && cb_) cb_();
    }

    std::atomic<ULONG> refs_{1};
    std::atomic<bool> enabled_{true};
    HotplugWatcher::Callback cb_;
};

} // namespace

struct HotplugWatcher::Impl
{
    ComPtr<IMMDeviceEnumerator> enumerator;
    NotificationClient* client = nullptr;
    bool registered = false;
};

HotplugWatcher::HotplugWatcher(Callback onChange) : impl_(new Impl)
{
    impl_->client = new NotificationClient(std::move(onChange));
    if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&impl_->enumerator))))
        impl_->registered = SUCCEEDED(impl_->enumerator->RegisterEndpointNotificationCallback(impl_->client));
    if (!impl_->registered) PF8_LOG_WARN("device", "hot-plug notifications unavailable");
}

HotplugWatcher::~HotplugWatcher()
{
    impl_->client->disable();
    if (impl_->registered) impl_->enumerator->UnregisterEndpointNotificationCallback(impl_->client);
    impl_->client->Release();
    delete impl_;
}

bool HotplugWatcher::registered() const noexcept { return impl_->registered; }

} // namespace pf8
