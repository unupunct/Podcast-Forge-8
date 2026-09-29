#include "devices/WinEndpointEnumerator.h"

#include <windows.h>
#include <initguid.h>
#include <audioclient.h>
#include <devicetopology.h>
#include <ksmedia.h>
#include <mmdeviceapi.h>
#include <mmreg.h>
#include <propvarutil.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <wrl/client.h>

#include <string>

namespace pf8 {
namespace {

using Microsoft::WRL::ComPtr;

// Property keys defined locally so we don't depend on which SDK header declares them.
constexpr PROPERTYKEY kFriendlyName   = {{0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}}, 14};
constexpr PROPERTYKEY kDeviceDesc     = {{0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}}, 2};
constexpr PROPERTYKEY kManufacturer   = {{0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}}, 13};
constexpr PROPERTYKEY kEnumeratorName = {{0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}}, 24};
constexpr PROPERTYKEY kContainerId    = {{0x8c7ed206, 0x3f8a, 0x4827, {0xb3, 0xab, 0xae, 0x9e, 0x1f, 0xae, 0xfc, 0x6c}}, 2};
constexpr PROPERTYKEY kFormFactor     = {{0x1da5d803, 0xd492, 0x4edd, {0x8c, 0x23, 0xe0, 0xc0, 0xff, 0xee, 0x7f, 0x0e}}, 0};
constexpr PROPERTYKEY kDeviceFormat   = {{0xf19f064d, 0x082c, 0x4e27, {0xbc, 0x73, 0x68, 0x82, 0xa1, 0xbb, 0x8e, 0x4c}}, 0};

std::string narrow(const wchar_t* w)
{
    if (!w || !*w) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n > 0 ? n - 1 : 0), '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}

std::string guidString(const GUID& g)
{
    wchar_t buf[64]{};
    StringFromGUID2(g, buf, 64);
    return narrow(buf);
}

std::string readString(IPropertyStore* store, const PROPERTYKEY& key)
{
    PROPVARIANT v;
    PropVariantInit(&v);
    std::string out;
    if (SUCCEEDED(store->GetValue(key, &v)))
    {
        if (v.vt == VT_LPWSTR) out = narrow(v.pwszVal);
        else if (v.vt == VT_CLSID && v.puuid) out = guidString(*v.puuid);
    }
    PropVariantClear(&v);
    return out;
}

FormFactor mapFormFactor(UINT v)
{
    switch (v)
    {
        case 1: return FormFactor::Speakers;
        case 2: return FormFactor::LineLevel;
        case 3: return FormFactor::Headphones;
        case 4: return FormFactor::Microphone;
        case 5: return FormFactor::Headset;
        case 6: return FormFactor::Handset;
        case 7: return FormFactor::Digital;
        case 8: return FormFactor::Spdif;
        case 9: return FormFactor::Hdmi;
        case 10: return FormFactor::Unknown;
        default: return FormFactor::Other;
    }
}

DeviceState mapState(DWORD s)
{
    switch (s)
    {
        case DEVICE_STATE_ACTIVE:     return DeviceState::Active;
        case DEVICE_STATE_DISABLED:   return DeviceState::Disabled;
        case DEVICE_STATE_UNPLUGGED:  return DeviceState::Unplugged;
        default:                      return DeviceState::NotPresent;
    }
}

// IDeviceTopology → connector → connected part → owning device's interface path.
std::string parentInterfacePath(IMMDevice* device)
{
    ComPtr<IDeviceTopology> topo;
    if (FAILED(device->Activate(__uuidof(IDeviceTopology), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(topo.GetAddressOf()))))
        return {};
    ComPtr<IConnector> conn;
    if (FAILED(topo->GetConnector(0, &conn))) return {};
    ComPtr<IConnector> other;
    if (FAILED(conn->GetConnectedTo(&other))) return {};
    ComPtr<IPart> part;
    if (FAILED(other.As(&part))) return {};
    ComPtr<IDeviceTopology> otherTopo;
    if (FAILED(part->GetTopologyObject(&otherTopo))) return {};
    LPWSTR id = nullptr;
    if (FAILED(otherTopo->GetDeviceId(&id)) || !id) return {};
    std::string s = narrow(id);
    CoTaskMemFree(id);
    // The id looks like "{2}.\\?\usb#vid_...#{guid}\global"; keep from "\\?\" on.
    const size_t p = s.find("\\\\?\\");
    return p == std::string::npos ? s : s.substr(p);
}

// Manufacturer of the device owning `path`, and the nearest USB device node above it (walking
// past composite-interface nodes "USB\VID_..&PID_..&MI_nn\..").
void interfaceDetails(const std::string& path, std::string& manufacturer, std::string& usbInstanceId)
{
    if (path.empty()) return;
    const int n = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, w.data(), n);

    HDEVINFO set = SetupDiCreateDeviceInfoList(nullptr, nullptr);
    if (set == INVALID_HANDLE_VALUE) return;
    SP_DEVICE_INTERFACE_DATA ifData{sizeof(ifData)};
    if (SetupDiOpenDeviceInterfaceW(set, w.c_str(), 0, &ifData))
    {
        SP_DEVINFO_DATA devData{sizeof(devData)};
        DWORD needed = 0;
        SetupDiGetDeviceInterfaceDetailW(set, &ifData, nullptr, 0, &needed, &devData);
        wchar_t buf[256]{};
        if (manufacturer.empty() &&
            SetupDiGetDeviceRegistryPropertyW(set, &devData, SPDRP_MFG, nullptr, reinterpret_cast<BYTE*>(buf),
                                              sizeof(buf) - sizeof(wchar_t), nullptr))
            manufacturer = narrow(buf);

        DEVINST node = devData.DevInst;
        for (int depth = 0; depth < 6 && node != 0; ++depth)
        {
            wchar_t id[MAX_DEVICE_ID_LEN]{};
            if (CM_Get_Device_IDW(node, id, MAX_DEVICE_ID_LEN, 0) != CR_SUCCESS) break;
            std::string s = narrow(id);
            std::string upper = s;
            for (auto& ch : upper) ch = static_cast<char>(toupper(static_cast<unsigned char>(ch)));
            if (upper.rfind("USB\\VID_", 0) == 0 && upper.find("&MI_") == std::string::npos)
            {
                usbInstanceId = s;
                break;
            }
            DEVINST parent = 0;
            if (CM_Get_Parent(&parent, node, 0) != CR_SUCCESS) break;
            node = parent;
        }
    }
    SetupDiDestroyDeviceInfoList(set);
}

void readDeviceFormat(IPropertyStore* store, EndpointRaw& e)
{
    PROPVARIANT v;
    PropVariantInit(&v);
    if (SUCCEEDED(store->GetValue(kDeviceFormat, &v)) && v.vt == VT_BLOB && v.blob.cbSize >= sizeof(WAVEFORMATEX))
    {
        const auto* wf = reinterpret_cast<const WAVEFORMATEX*>(v.blob.pBlobData);
        e.mixRate = static_cast<int>(wf->nSamplesPerSec);
        e.mixChannels = wf->nChannels;
    }
    PropVariantClear(&v);
}

void probe(IMMDevice* device, EndpointRaw& e)
{
    ComPtr<IAudioClient> client;
    if (FAILED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(client.GetAddressOf()))))
        return;

    WAVEFORMATEX* mix = nullptr;
    if (SUCCEEDED(client->GetMixFormat(&mix)) && mix)
    {
        e.mixRate = static_cast<int>(mix->nSamplesPerSec);
        e.mixChannels = mix->nChannels;
        CoTaskMemFree(mix);
    }

    const int channels = e.mixChannels > 0 ? e.mixChannels : 2;
    for (int rate : {44100, 48000, 88200, 96000})
    {
        struct Fmt { WORD bits; WORD valid; GUID sub; };
        const Fmt fmts[] = {{32, 32, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT}, {32, 24, KSDATAFORMAT_SUBTYPE_PCM},
                            {24, 24, KSDATAFORMAT_SUBTYPE_PCM}, {16, 16, KSDATAFORMAT_SUBTYPE_PCM}};
        for (const auto& f : fmts)
        {
            WAVEFORMATEXTENSIBLE wfx{};
            wfx.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
            wfx.Format.nChannels = static_cast<WORD>(channels);
            wfx.Format.nSamplesPerSec = static_cast<DWORD>(rate);
            wfx.Format.wBitsPerSample = f.bits;
            wfx.Format.nBlockAlign = static_cast<WORD>(channels * f.bits / 8);
            wfx.Format.nAvgBytesPerSec = wfx.Format.nSamplesPerSec * wfx.Format.nBlockAlign;
            wfx.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
            wfx.Samples.wValidBitsPerSample = f.valid;
            wfx.dwChannelMask = channels == 1 ? SPEAKER_FRONT_CENTER : (channels == 2 ? KSAUDIO_SPEAKER_STEREO : 0);
            wfx.SubFormat = f.sub;
            if (client->IsFormatSupported(AUDCLNT_SHAREMODE_EXCLUSIVE, &wfx.Format, nullptr) == S_OK)
            {
                e.exclusiveRates.push_back(rate);
                break;
            }
        }
    }
}

} // namespace

std::vector<EndpointRaw> enumerateEndpoints(bool probeExclusiveRates)
{
    std::vector<EndpointRaw> out;
    ComPtr<IMMDeviceEnumerator> enumerator;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator))))
        return out;

    ComPtr<IMMDeviceCollection> collection;
    if (FAILED(enumerator->EnumAudioEndpoints(eAll, DEVICE_STATEMASK_ALL, &collection))) return out;
    UINT count = 0;
    collection->GetCount(&count);

    for (UINT i = 0; i < count; ++i)
    {
        ComPtr<IMMDevice> device;
        if (FAILED(collection->Item(i, &device))) continue;

        EndpointRaw e;
        LPWSTR id = nullptr;
        if (SUCCEEDED(device->GetId(&id)) && id)
        {
            e.endpointId = narrow(id);
            CoTaskMemFree(id);
        }
        if (e.endpointId.empty()) continue;

        DWORD state = 0;
        device->GetState(&state);
        e.state = mapState(state);

        ComPtr<IMMEndpoint> endpoint;
        EDataFlow flow = eRender;
        if (SUCCEEDED(device.As(&endpoint))) endpoint->GetDataFlow(&flow);
        e.flow = flow == eCapture ? Flow::Capture : Flow::Render;

        ComPtr<IPropertyStore> store;
        if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &store)))
        {
            e.friendlyName = readString(store.Get(), kFriendlyName);
            e.deviceDesc = readString(store.Get(), kDeviceDesc);
            e.enumerator = readString(store.Get(), kEnumeratorName);
            e.containerId = readString(store.Get(), kContainerId);
            e.manufacturer = readString(store.Get(), kManufacturer);
            PROPVARIANT v;
            PropVariantInit(&v);
            if (SUCCEEDED(store->GetValue(kFormFactor, &v)) && v.vt == VT_UI4) e.formFactor = mapFormFactor(v.ulVal);
            PropVariantClear(&v);
            readDeviceFormat(store.Get(), e);
        }

        if (e.state == DeviceState::Active)
        {
            e.parentInterfacePath = parentInterfacePath(device.Get());
            interfaceDetails(e.parentInterfacePath, e.manufacturer, e.usbDeviceInstanceId);
            if (probeExclusiveRates) probe(device.Get(), e);
        }
        out.push_back(std::move(e));
    }
    return out;
}

} // namespace pf8

namespace pf8 {

std::optional<std::string> defaultEndpointId(Flow flow)
{
    ComPtr<IMMDeviceEnumerator> enumerator;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator))))
        return std::nullopt;
    ComPtr<IMMDevice> device;
    if (FAILED(enumerator->GetDefaultAudioEndpoint(flow == Flow::Capture ? eCapture : eRender, eConsole, &device)))
        return std::nullopt;
    LPWSTR id = nullptr;
    if (FAILED(device->GetId(&id)) || !id) return std::nullopt;
    std::string s = narrow(id);
    CoTaskMemFree(id);
    return s;
}

} // namespace pf8
