#include "engine/EngineController.h"

#include <windows.h>
#include <objbase.h>

#include <algorithm>
#include <cmath>
#include <set>

#include "core/Clock.h"
#include "core/Log.h"
#include "core/SettingsDb.h"
#include "devices/HotplugWatcher.h"
#include "devices/WinEndpointEnumerator.h"
#include "engine/MasterClock.h"

namespace pf8 {

namespace {
std::string keyFor(Flow flow, const std::string& id) { return (flow == Flow::Capture ? "c:" : "r:") + id; }
constexpr const char* kAssignmentsKey = "assignments.v1";
} // namespace

const char* toString(EndpointState s) noexcept
{
    switch (s)
    {
        case EndpointState::None:          return "-";
        case EndpointState::Ok:            return "OK";
        case EndpointState::Offline:       return "OFFLINE";
        case EndpointState::Disconnected:  return "DISCONNECTED";
        case EndpointState::PossibleMatch: return "POSSIBLE MATCH";
        case EndpointState::InUse:         return "IN USE BY OTHER APP";
        case EndpointState::Failed:        return "FAILED";
    }
    return "?";
}

// Connects one WASAPI stream to its bridge. Runs on the stream's MMCSS thread.
class EngineController::Handler : public StreamCallback
{
public:
    Handler(EngineController& c, std::string key, bool master) : controller_(c), key_(std::move(key)), master_(master) {}

    std::shared_ptr<InputBridge> in;
    std::shared_ptr<OutputBridge> out;

    int deviceRate = 48000;

    void onStreamBlock(float* interleaved, int frames, int, int64_t qpc100ns, uint64_t devicePosition) noexcept override
    {
        const int64_t now = monotonicNs();
        const int64_t hwNs = qpc100ns > 0 ? qpc100ns * 100 : 0;
        if (in)
        {
            // Capture: qpc is the hardware time of the first frame in the packet.
            in->deviceWrite(interleaved, frames, hwNs != 0 ? hwNs : now);
            if (master_) in->driveEngine(controller_.engine_);
        }
        else if (out)
        {
            // Render: at hwNs the device had played devicePosition frames; this packet starts
            // after everything submitted so far.
            int64_t startNs = 0;
            if (hwNs != 0)
                startNs = hwNs + static_cast<int64_t>(1e9 * (static_cast<double>(submitted_) - static_cast<double>(devicePosition)) /
                                                      deviceRate);
            out->deviceRead(interleaved, frames, now, master_ ? &controller_.engine_ : nullptr, startNs);
            submitted_ += static_cast<uint64_t>(frames);
        }
    }

    void onStreamError(StreamStatus) noexcept override
    {
        // The stream thread is exiting; hand the event to the control thread.
        controller_.post([c = &controller_, k = key_] { c->onStreamEnded(k); });
    }

private:
    EngineController& controller_;
    std::string key_;
    bool master_;
    uint64_t submitted_ = 0;
};

struct EngineController::Endpoint
{
    std::string endpointId;
    Flow flow = Flow::Render;
    bool master = false;
    int pairs = 1;
    std::unique_ptr<Handler> handler;
    std::unique_ptr<WasapiStream> stream;
};

EngineController::EngineController(Settings settings, SettingsDb* db)
    : settings_(settings), db_(db), engine_(settings.sampleRate, settings.blockFrames)
{
    assignments_ = Assignments::defaults();
    thread_ = std::thread([this] { threadMain(); });
}

EngineController::~EngineController()
{
    post([this] {
        hotplug_.reset();
        engine_.stopInternalClock();
        std::map<std::string, std::unique_ptr<Endpoint>> closing;
        {
            std::lock_guard lock(stateMutex_);
            closing.swap(endpoints_);
        }
        for (auto& [k, e] : closing)
            if (e->stream) e->stream->stop();
        closing.clear();
        engine_.setGraph(std::make_unique<EngineGraph>());
    });
    {
        std::lock_guard lock(queueMutex_);
        quit_ = true;
    }
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
}

void EngineController::post(std::function<void()> job) { postDelayed(std::chrono::milliseconds(0), std::move(job)); }

void EngineController::postDelayed(std::chrono::milliseconds delay, std::function<void()> job)
{
    {
        std::lock_guard lock(queueMutex_);
        jobs_.emplace(std::chrono::steady_clock::now() + delay, std::move(job));
    }
    cv_.notify_one();
}

void EngineController::waitIdle()
{
    std::unique_lock lock(queueMutex_);
    idleCv_.wait(lock, [this] {
        return !busy_ && (jobs_.empty() || jobs_.begin()->first > std::chrono::steady_clock::now() + std::chrono::seconds(5));
    });
}

void EngineController::threadMain()
{
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    for (;;)
    {
        std::function<void()> job;
        {
            std::unique_lock lock(queueMutex_);
            for (;;)
            {
                if (jobs_.empty())
                {
                    if (quit_) break;
                    cv_.wait(lock);
                    continue;
                }
                const auto due = jobs_.begin()->first;
                if (due <= std::chrono::steady_clock::now() || quit_) break;
                cv_.wait_until(lock, due);
            }
            if (jobs_.empty()) break; // quit
            job = std::move(jobs_.begin()->second);
            jobs_.erase(jobs_.begin());
            busy_ = true;
        }
        try
        {
            job();
        }
        catch (const std::exception& e)
        {
            PF8_LOG_ERROR("control", "job threw: %s", e.what());
        }
        engine_.collectGarbage();
        {
            std::lock_guard lock(queueMutex_);
            busy_ = false;
        }
        idleCv_.notify_all();
    }
    CoUninitialize();
}

void EngineController::start()
{
    post([this] {
        loadAssignments();
        hotplug_ = std::make_unique<HotplugWatcher>([this] { onHotplug(); });
        registry_.rebuild(enumerateEndpoints());
        PF8_LOG_INFO("device", "initial scan: %zu endpoints", registry_.devices().size());
        reconcile();
    });
}

void EngineController::rescanDevices()
{
    post([this] {
        const auto diff = registry_.rebuild(enumerateEndpoints());
        for (const auto& id : diff.added) PF8_LOG_INFO("device", "device.added id=%s", id.c_str());
        for (const auto& id : diff.removed) PF8_LOG_WARN("device", "device.removed id=%s", id.c_str());
        for (const auto& id : diff.changed)
            if (auto d = registry_.find(id))
                PF8_LOG_INFO("device", "device.changed id=%s name=%s state=%s", id.c_str(), d->raw.friendlyName.c_str(),
                             toString(d->raw.state));
        reconcile();
    });
}

void EngineController::onHotplug()
{
    // Called on a Windows notification thread. USB headsets fire several events: debounce 300 ms.
    if (hotplugPending_.fetch_add(1) == 0)
        postDelayed(std::chrono::milliseconds(300), [this] {
            hotplugPending_.store(0);
            rescanDevices();
        });
}

void EngineController::onStreamEnded(const std::string& key)
{
    PF8_LOG_WARN("device", "stream ended key=%s", key.c_str());
    rescanDevices(); // the registry decides whether it's gone or merely failed
}

void EngineController::loadAssignments()
{
    if (!db_) return;
    if (auto text = db_->get(kAssignmentsKey))
    {
        if (auto a = assignmentsFromJson(*text))
        {
            std::lock_guard lock(stateMutex_);
            assignments_ = *a;
            PF8_LOG_INFO("device", "assignments loaded");
        }
        else
            PF8_LOG_WARN("device", "stored assignments unreadable; starting with none (the stored copy is kept)");
    }
}

void EngineController::saveAssignments()
{
    if (!db_) return;
    std::string text;
    {
        std::lock_guard lock(stateMutex_);
        text = toJson(assignments_);
    }
    if (!db_->set(kAssignmentsKey, text)) PF8_LOG_ERROR("settings", "failed to save assignments");
}

void EngineController::assignMic(int channel, std::optional<std::string> endpointId, int micChannel)
{
    post([this, channel, endpointId, micChannel] {
        if (channel < 0 || channel >= kNumChannels) return;
        {
            std::lock_guard lock(stateMutex_);
            auto& c = assignments_.ch[static_cast<size_t>(channel)];
            c.micChannel = micChannel;
            if (!endpointId) c.mic.reset();
            else if (auto d = registry_.find(*endpointId)) c.mic = identityOf(*d);
        }
        PF8_LOG_INFO("device", "assign mic ch=%d id=%s", channel + 1, endpointId ? endpointId->c_str() : "none");
        saveAssignments();
        reconcile();
    });
}

void EngineController::assignHeadphones(int channel, std::optional<std::string> endpointId, int pair)
{
    post([this, channel, endpointId, pair] {
        if (channel < 0 || channel >= kNumChannels) return;
        {
            std::lock_guard lock(stateMutex_);
            auto& c = assignments_.ch[static_cast<size_t>(channel)];
            c.hpPair = pair;
            if (!endpointId) c.headphones.reset();
            else if (auto d = registry_.find(*endpointId)) c.headphones = identityOf(*d);
        }
        PF8_LOG_INFO("device", "assign headphones ch=%d id=%s", channel + 1, endpointId ? endpointId->c_str() : "none");
        saveAssignments();
        reconcile();
    });
}

void EngineController::acceptPossibleMatch(int channel, bool mic)
{
    post([this, channel, mic] {
        if (channel < 0 || channel >= kNumChannels) return;
        std::string candidate;
        {
            std::lock_guard lock(stateMutex_);
            const auto& r = mic ? resolution_[static_cast<size_t>(channel)].mic : resolution_[static_cast<size_t>(channel)].headphones;
            if (r.kind != MatchKind::PossibleMatch) return;
            candidate = r.endpointId;
        }
        if (mic) assignMic(channel, candidate, assignments().ch[static_cast<size_t>(channel)].micChannel);
        else assignHeadphones(channel, candidate, assignments().ch[static_cast<size_t>(channel)].hpPair);
    });
}

void EngineController::setChannelName(int channel, std::string name)
{
    post([this, channel, name = std::move(name)] {
        if (channel < 0 || channel >= kNumChannels) return;
        {
            std::lock_guard lock(stateMutex_);
            assignments_.ch[static_cast<size_t>(channel)].name = name;
        }
        saveAssignments();
    });
}

Assignments EngineController::assignments() const
{
    std::lock_guard lock(stateMutex_);
    return assignments_;
}

std::unique_ptr<EngineController::Endpoint> EngineController::openEndpoint(const std::string& endpointId, Flow flow,
                                                                          bool master, int pairs)
{
    auto e = std::make_unique<Endpoint>();
    e->endpointId = endpointId;
    e->flow = flow;
    e->master = master;
    e->pairs = pairs;
    const std::string key = keyFor(flow, endpointId);
    e->handler = std::make_unique<Handler>(*this, key, master);

    StreamConfig sc;
    sc.endpointId = endpointId;
    sc.flow = flow;
    sc.mode = settings_.mode;
    sc.requestedRate = settings_.sampleRate;
    sc.requestedFrames = settings_.blockFrames;
    e->stream = std::make_unique<WasapiStream>(sc, e->handler.get());

    std::string error;
    if (!e->stream->open(error))
    {
        std::lock_guard lock(stateMutex_);
        failures_[key] = error;
        PF8_LOG_WARN("device", "open failed key=%s: %s", key.c_str(), error.c_str());
        return nullptr;
    }

    BridgeConfig bc;
    bc.deviceChannels = e->stream->channels();
    bc.deviceRate = e->stream->sampleRate();
    bc.devicePeriod = std::max(e->stream->periodFrames(), 1);
    bc.engineRate = settings_.sampleRate;
    bc.engineBlock = settings_.blockFrames;
    bc.engineBurst = master ? 0 : masterBurst_;
    if (flow == Flow::Render) bc.deviceBufferFrames = std::max(0, e->stream->bufferFrames() - bc.devicePeriod / 2);
    bc.master = master;
    e->handler->deviceRate = bc.deviceRate;
    if (flow == Flow::Capture)
        e->handler->in = std::make_shared<InputBridge>(bc);
    else
        e->handler->out = std::make_shared<OutputBridge>(bc, std::max(pairs, std::max(1, bc.deviceChannels / 2)));

    if (master)
    {
        const double engineFrames = static_cast<double>(bc.devicePeriod) * settings_.sampleRate / bc.deviceRate;
        masterBurst_ = static_cast<int>(std::ceil(engineFrames / settings_.blockFrames)) * settings_.blockFrames;
    }
    if (!e->stream->start())
    {
        std::lock_guard lock(stateMutex_);
        failures_[key] = "start failed";
        return nullptr;
    }
    {
        std::lock_guard lock(stateMutex_);
        failures_.erase(key);
    }
    PF8_LOG_INFO("device", "stream.open key=%s master=%d rate=%d ch=%d period=%d mode=%s", key.c_str(), master ? 1 : 0,
                 bc.deviceRate, bc.deviceChannels, bc.devicePeriod, toString(e->stream->grantedMode()));
    return e;
}

void EngineController::reconcile()
{
    const auto devices = registry_.devices();
    Assignments a = assignments();
    auto res = resolveAll(a, devices);

    // Fingerprint matches: the device came back on a new endpoint id — remember the new id.
    bool changedIds = false;
    for (size_t i = 0; i < res.size(); ++i)
    {
        if (res[i].mic.kind == MatchKind::Fingerprint && a.ch[i].mic && a.ch[i].mic->endpointId != res[i].mic.endpointId)
        {
            if (auto d = registry_.find(res[i].mic.endpointId)) a.ch[i].mic = identityOf(*d);
            PF8_LOG_INFO("device", "ch=%zu mic restored by fingerprint id=%s", i + 1, res[i].mic.endpointId.c_str());
            changedIds = true;
        }
        if (res[i].headphones.kind == MatchKind::Fingerprint && a.ch[i].headphones &&
            a.ch[i].headphones->endpointId != res[i].headphones.endpointId)
        {
            if (auto d = registry_.find(res[i].headphones.endpointId)) a.ch[i].headphones = identityOf(*d);
            PF8_LOG_INFO("device", "ch=%zu headphones restored by fingerprint id=%s", i + 1, res[i].headphones.endpointId.c_str());
            changedIds = true;
        }
    }

    // Endpoints the channels need.
    struct Need { Flow flow; std::string id; int pairs = 1; int order = 99; bool headphone = false; };
    std::map<std::string, Need> needed;
    for (size_t i = 0; i < res.size(); ++i)
    {
        const auto& r = res[i];
        if (r.mic.kind == MatchKind::Exact || r.mic.kind == MatchKind::Fingerprint)
        {
            auto& n = needed[keyFor(Flow::Capture, r.mic.endpointId)];
            n.flow = Flow::Capture;
            n.id = r.mic.endpointId;
            n.order = std::min(n.order, static_cast<int>(i));
        }
        if (r.headphones.kind == MatchKind::Exact || r.headphones.kind == MatchKind::Fingerprint)
        {
            auto& n = needed[keyFor(Flow::Render, r.headphones.endpointId)];
            n.flow = Flow::Render;
            n.id = r.headphones.endpointId;
            n.headphone = true;
            n.pairs = std::max(n.pairs, a.ch[i].hpPair + 1);
            n.order = std::min(n.order, static_cast<int>(i));
        }
    }

    // Master selection among the endpoints we'll run (failed ones are excluded).
    std::vector<MasterCandidate> candidates;
    for (const auto& [key, n] : needed)
    {
        bool failed;
        {
            std::lock_guard lock(stateMutex_);
            failed = failures_.count(key) > 0 && !endpoints_.count(key);
        }
        candidates.push_back({key, n.flow, !failed, key == keyFor(Flow::Render, a.preferredMaster), n.headphone, n.order});
    }
    const auto masterKey = selectMaster(candidates).value_or("");

    // Close endpoints that are no longer needed, dead, or whose master role changes.
    std::vector<std::unique_ptr<Endpoint>> closing;
    {
        std::lock_guard lock(stateMutex_);
        for (auto it = endpoints_.begin(); it != endpoints_.end();)
        {
            const auto& e = *it->second;
            const bool dead = e.stream->status() != StreamStatus::Running;
            const bool roleChanged = e.master != (it->first == masterKey);
            auto need = needed.find(it->first);
            const bool pairsChanged = need != needed.end() && e.flow == Flow::Render && need->second.pairs > e.pairs;
            if (need == needed.end() || dead || roleChanged || pairsChanged)
            {
                closing.push_back(std::move(it->second));
                it = endpoints_.erase(it);
            }
            else
                ++it;
        }
    }
    // Take the closing bridges out of the graph before stopping their streams.
    auto publishGraph = [&] {
        auto g = std::make_unique<EngineGraph>();
        std::lock_guard lock(stateMutex_);
        g->generation = ++generation_;
        std::map<std::string, int> inIdx, outIdx;
        for (const auto& [key, e] : endpoints_)
        {
            if (e->handler->in && static_cast<int>(g->inputs.size()) < EngineGraph::kMaxInputBridges)
            {
                inIdx[key] = static_cast<int>(g->inputs.size());
                g->inputs.push_back(e->handler->in);
            }
            if (e->handler->out && static_cast<int>(g->outputs.size()) < EngineGraph::kMaxOutputBridges)
            {
                outIdx[key] = static_cast<int>(g->outputs.size());
                g->outputs.push_back(e->handler->out);
            }
        }
        for (size_t i = 0; i < res.size(); ++i)
        {
            ChannelRoute route;
            if (res[i].mic.kind == MatchKind::Exact || res[i].mic.kind == MatchKind::Fingerprint)
            {
                auto it = inIdx.find(keyFor(Flow::Capture, res[i].mic.endpointId));
                if (it != inIdx.end())
                {
                    route.inputBridge = it->second;
                    route.inputChannel = a.ch[i].micChannel;
                }
            }
            if (res[i].headphones.kind == MatchKind::Exact || res[i].headphones.kind == MatchKind::Fingerprint)
            {
                auto it = outIdx.find(keyFor(Flow::Render, res[i].headphones.endpointId));
                if (it != outIdx.end())
                {
                    route.outputBridge = it->second;
                    route.outputPair = a.ch[i].hpPair;
                }
            }
            g->channels[i] = route;
        }
        engine_.setGraph(std::move(g));
    };
    if (!closing.empty()) publishGraph();
    for (auto& e : closing)
    {
        PF8_LOG_INFO("device", "stream.close id=%s", e->endpointId.c_str());
        e->stream->stop();
    }
    closing.clear(); // bridges stay alive through the retired graphs' shared_ptrs until collected

    // Open what's missing. The master first, so it can drive the tick as soon as others join.
    std::vector<std::string> order;
    if (!masterKey.empty()) order.push_back(masterKey);
    for (const auto& [key, n] : needed)
        if (key != masterKey) order.push_back(key);
    for (const auto& key : order)
    {
        bool open;
        {
            std::lock_guard lock(stateMutex_);
            open = endpoints_.count(key) > 0;
        }
        if (open) continue;
        const auto& n = needed[key];
        if (auto e = openEndpoint(n.id, n.flow, key == masterKey, n.pairs))
        {
            std::lock_guard lock(stateMutex_);
            endpoints_[key] = std::move(e);
        }
    }

    // A master that failed to open leaves the engine on the internal clock.
    bool masterRunning;
    {
        std::lock_guard lock(stateMutex_);
        masterRunning = !masterKey.empty() && endpoints_.count(masterKey) > 0;
        masterKey_ = masterRunning ? masterKey : std::string();
        resolution_ = res;
        if (changedIds) assignments_ = a;
    }
    if (masterRunning)
        engine_.stopInternalClock();
    else
    {
        masterBurst_ = settings_.blockFrames;
        engine_.startInternalClock();
    }
    publishGraph();
    if (changedIds) saveAssignments();
}

ControllerStatus EngineController::status() const
{
    ControllerStatus s;
    s.sampleRate = settings_.sampleRate;
    s.blockFrames = settings_.blockFrames;
    s.internalClock = engine_.internalClockRunning();
    s.running = true;

    std::lock_guard lock(stateMutex_);
    s.openStreams = static_cast<int>(endpoints_.size());
    auto it = endpoints_.find(masterKey_);
    if (it != endpoints_.end())
    {
        s.masterId = it->second->endpointId;
        if (auto d = registry_.find(s.masterId)) s.masterName = d->raw.friendlyName;
        s.masterMode = it->second->stream->grantedMode();
        s.masterPeriod = it->second->stream->periodFrames();
    }

    auto view = [&](const std::optional<DeviceIdentity>& saved, const Resolution& r, Flow flow, EndpointView& v) {
        if (!saved) { v.state = EndpointState::None; return; }
        v.assignedName = saved->friendlyName;
        v.endpointId = r.endpointId;
        const std::string key = keyFor(flow, r.endpointId);
        switch (r.kind)
        {
            case MatchKind::Exact:
            case MatchKind::Fingerprint:
            {
                auto e = endpoints_.find(key);
                if (e != endpoints_.end())
                {
                    v.state = EndpointState::Ok;
                    v.master = key == masterKey_;
                    v.deviceRate = e->second->stream->sampleRate();
                    v.deviceChannels = e->second->stream->channels();
                    v.periodFrames = e->second->stream->periodFrames();
                    v.mode = e->second->stream->grantedMode();
                    if (e->second->handler->in) v.bridge = e->second->handler->in->read();
                    if (e->second->handler->out) v.bridge = e->second->handler->out->read();
                }
                else
                {
                    auto f = failures_.find(key);
                    const bool inUse = f != failures_.end() && f->second.find("in use") != std::string::npos;
                    v.state = inUse ? EndpointState::InUse : EndpointState::Failed;
                    v.detail = f != failures_.end() ? f->second : "not open";
                }
                break;
            }
            case MatchKind::PossibleMatch:
                v.state = EndpointState::PossibleMatch;
                if (auto d = registry_.find(r.endpointId)) v.detail = d->raw.friendlyName;
                break;
            case MatchKind::None:
            {
                auto d = registry_.find(saved->endpointId);
                v.state = (d && d->raw.state == DeviceState::Unplugged) ? EndpointState::Disconnected : EndpointState::Offline;
                v.endpointId = saved->endpointId;
                break;
            }
        }
    };

    for (size_t i = 0; i < kNumChannels; ++i)
    {
        auto& c = s.channels[i];
        const auto& a = assignments_.ch[i];
        c.name = a.name;
        c.micChannel = a.micChannel;
        c.hpPair = a.hpPair;
        view(a.mic, resolution_[i].mic, Flow::Capture, c.mic);
        view(a.headphones, resolution_[i].headphones, Flow::Render, c.headphones);
    }
    return s;
}

} // namespace pf8
