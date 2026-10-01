#include "ui/MarkersView.h"

#include "record/FileSink.h"
#include "ui/Widgets.h"

namespace pf8::ui {

MarkersView::MarkersView(EngineController& controller) : controller_(controller)
{
    auto& h = table_.getHeader();
    h.addColumn("#", 1, 50);
    h.addColumn("Time", 2, 130);
    h.addColumn("Label (double-click to rename)", 3, 500);
    table_.setModel(this);
    table_.setRowHeight(24);
    addAndMakeVisible(table_);
    add_.onClick = [this] { controller_.recorder().addMarker(); };
    remove_.onClick = [this] {
        const int r = table_.getSelectedRow();
        if (r >= 0 && r < static_cast<int>(rows_.size())) controller_.recorder().markers().remove(rows_[static_cast<size_t>(r)].id);
        controller_.recorder().markers().save(controller_.recorder().metadataDir());
    };
    export_.onClick = [this] { exportAs(); };
    hint_.setFont(juce::FontOptions(12.0f));
    hint_.setColour(juce::Label::textColourId, colours::textDim);
    hint_.setText("F12 adds a marker while recording. Markers are saved instantly to the session's Metadata folder.",
                  juce::dontSendNotification);
    allowEllipsis(hint_);
    for (juce::Component* c : std::initializer_list<juce::Component*>{&add_, &remove_, &export_, &hint_}) addAndMakeVisible(c);
    startTimerHz(4);
}

MarkersView::~MarkersView() { stopTimer(); }

void MarkersView::timerCallback()
{
    auto now = controller_.recorder().markers().all();
    const bool changed = now.size() != rows_.size() ||
                         !std::equal(now.begin(), now.end(), rows_.begin(), [](const Marker& a, const Marker& b) {
                             return a.id == b.id && a.label == b.label && a.samplePos == b.samplePos;
                         });
    if (changed)
    {
        rows_ = std::move(now);
        table_.updateContent();
        repaint();
    }
    const bool rec = controller_.recorder().state() != Recorder::State::Idle;
    add_.setEnabled(rec);
    export_.setEnabled(!rows_.empty());
    remove_.setEnabled(!rows_.empty());
}

void MarkersView::paint(juce::Graphics& g) { g.fillAll(colours::background); }

void MarkersView::resized()
{
    auto b = getLocalBounds().reduced(10);
    auto top = b.removeFromTop(30);
    add_.setBounds(top.removeFromLeft(130));
    top.removeFromLeft(8);
    remove_.setBounds(top.removeFromLeft(100));
    top.removeFromLeft(8);
    export_.setBounds(top.removeFromLeft(110));
    top.removeFromLeft(12);
    hint_.setBounds(top);
    b.removeFromTop(6);
    table_.setBounds(b);
}

void MarkersView::paintRowBackground(juce::Graphics& g, int row, int, int, bool sel)
{
    g.fillAll(sel ? colours::panelRaised : (row % 2 ? colours::panel : colours::background));
}

void MarkersView::paintCell(juce::Graphics& g, int row, int col, int w, int h, bool)
{
    if (row < 0 || row >= static_cast<int>(rows_.size())) return;
    const auto& m = rows_[static_cast<size_t>(row)];
    juce::String t;
    if (col == 1) t = juce::String(m.id);
    if (col == 2) t = timecode(m.samplePos, controller_.settings().sampleRate);
    if (col == 3) t = juce::String::fromUTF8(m.label.c_str());
    g.setColour(col == 2 ? colours::warn : colours::text);
    g.setFont(juce::Font(juce::FontOptions(col == 2 ? juce::Font::getDefaultMonospacedFontName() : juce::String(), 14.0f, juce::Font::plain)));
    g.drawText(t, 6, 0, w - 8, h, juce::Justification::centredLeft, true);
}

void MarkersView::cellDoubleClicked(int row, int, const juce::MouseEvent&)
{
    if (row < 0 || row >= static_cast<int>(rows_.size())) return;
    const int id = rows_[static_cast<size_t>(row)].id;
    auto* w = new juce::AlertWindow("Rename marker", "", juce::MessageBoxIconType::NoIcon, this);
    w->addTextEditor("label", juce::String::fromUTF8(rows_[static_cast<size_t>(row)].label.c_str()));
    w->addButton("OK", 1, juce::KeyPress(juce::KeyPress::returnKey));
    w->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    w->enterModalState(true, juce::ModalCallbackFunction::create([this, id, w](int r) {
                           if (r != 1) return;
                           controller_.recorder().markers().rename(id, w->getTextEditorContents("label").toStdString());
                           controller_.recorder().markers().save(controller_.recorder().metadataDir());
                       }),
                       true);
}

void MarkersView::exportAs()
{
    juce::PopupMenu m;
    for (int i = 0; i < 4; ++i) m.addItem(i + 1, toString(static_cast<MarkerExport>(i)));
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&export_), [this](int r) {
        if (r <= 0) return;
        const auto fmt = static_cast<MarkerExport>(r - 1);
        auto dir = controller_.recorder().metadataDir();
        if (dir.empty())
        {
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, "Export markers", "Record a session first.");
            return;
        }
        const auto file = dir / (std::string("Markers") + extension(fmt));
        const bool ok = writeFileAtomically(file, controller_.recorder().markers().exportText(fmt));
        juce::AlertWindow::showMessageBoxAsync(ok ? juce::MessageBoxIconType::InfoIcon : juce::MessageBoxIconType::WarningIcon,
                                               "Export markers",
                                               (ok ? "Saved " : "Could not save ") + juce::String(file.wstring().c_str()));
    });
}

} // namespace pf8::ui
