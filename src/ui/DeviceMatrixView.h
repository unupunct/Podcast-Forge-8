#pragma once
// DEVICE MATRIX (DEVICE_MANAGEMENT.md §4): channel rows × Microphone | Input | Headphones | Status,
// output-role rows (monitor / stream outputs), a pool of unassigned devices, drag and drop, a
// per-cell device menu, and Auto Assign with explicit confirmation.
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <memory>

#include "engine/EngineController.h"
#include "ui/LookAndFeel.h"
#include "ui/Widgets.h"

namespace pf8::ui {

class DeviceMatrixView;

// One assignable cell. Kinds: channel mic, channel headphones, output role.
class DeviceCell : public juce::Component, public juce::DragAndDropTarget, public juce::SettableTooltipClient
{
public:
    enum class Kind { Mic, Headphones, Output };
    DeviceCell(DeviceMatrixView& owner, Kind kind, int index);

    void setContent(const juce::String& name, EndpointState state, const juce::String& detail, const std::string& endpointId);
    Flow flow() const noexcept { return kind_ == Kind::Mic ? Flow::Capture : Flow::Render; }
    Kind kind() const noexcept { return kind_; }
    int index() const noexcept { return index_; }
    const std::string& endpointId() const noexcept { return endpointId_; }

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;

    bool isInterestedInDragSource(const SourceDetails&) override;
    void itemDragEnter(const SourceDetails&) override { hover_ = true; repaint(); }
    void itemDragExit(const SourceDetails&) override { hover_ = false; repaint(); }
    void itemDropped(const SourceDetails&) override;

private:
    DeviceMatrixView& owner_;
    Kind kind_;
    int index_;
    juce::String name_, detail_;
    EndpointState state_ = EndpointState::None;
    std::string endpointId_;
    bool hover_ = false;
    bool dragStarted_ = false;
};

// Draggable list of devices that are not assigned anywhere.
class DevicePool : public juce::Component
{
public:
    explicit DevicePool(DeviceMatrixView& owner) : owner_(owner) {}
    void setDevices(std::vector<DeviceInfo> devices);
    void paint(juce::Graphics&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    int rowHeight() const noexcept { return 26; }
    int deviceCount() const noexcept { return static_cast<int>(devices_.size()); }

private:
    int rowAt(int y) const;
    DeviceMatrixView& owner_;
    std::vector<DeviceInfo> devices_;
    std::vector<int> rowToDevice_; // -1 = section header
    bool dragging_ = false;
};

class DeviceMatrixView : public juce::Component, public juce::DragAndDropContainer, public LayoutSelfCheck, private juce::Timer
{
public:
    explicit DeviceMatrixView(EngineController& controller);
    ~DeviceMatrixView() override;
    void resized() override;
    void paint(juce::Graphics&) override;
    void collectLayoutIssues(std::vector<std::string>& issues) const override;

    // Called by cells / pool.
    void assign(DeviceCell::Kind kind, int index, std::optional<std::string> endpointId);
    void dropOnto(DeviceCell& target, const juce::var& description);
    void showDeviceMenu(DeviceCell& cell);
    juce::var describeCell(const DeviceCell& cell) const;
    static juce::var describeDevice(const DeviceInfo& d);
    EngineController& controller() noexcept { return controller_; }

    void runAutoAssign();

private:
    void timerCallback() override;
    void refresh();

    EngineController& controller_;
    struct Row
    {
        juce::Label number;
        juce::Label name;
        std::unique_ptr<DeviceCell> mic, hp;
        juce::ComboBox input;
        juce::Label status;
        Meter meter{1};
    };
    std::array<Row, kNumChannels> rows_;
    struct OutRow
    {
        juce::Label title;
        std::unique_ptr<DeviceCell> cell;
        juce::Label status;
    };
    std::array<OutRow, kOutputRoles> outRows_;
    juce::Label headers_[5];
    juce::Label poolTitle_;
    DevicePool pool_{*this};
    juce::TextButton autoAssign_{"AUTO ASSIGN"}, rescan_{"RESCAN"};
    juce::Label hint_;
    uint64_t registryGeneration_ = ~0ull;
    std::string snapshot_;
};

} // namespace pf8::ui
