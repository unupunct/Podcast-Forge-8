#include "ui/SoundboardView.h"
#include "core/Paths.h"

#include <cmath>
#include <thread>

#include "core/Log.h"
#include "media/AudioFileLoader.h"

namespace pf8::ui {
namespace {

const juce::Colour kPadColours[8] = {juce::Colour(0xff3fa9f5), juce::Colour(0xff3ecf6e), juce::Colour(0xfff2b134), juce::Colour(0xfff0524f),
                                     juce::Colour(0xffb36bff), juce::Colour(0xff35c7a6), juce::Colour(0xffff8a3d), juce::Colour(0xff9aa4b2)};

juce::Colour padColour(const std::string& hex)
{
    return juce::Colour::fromString(juce::String("ff") + juce::String(hex).trimCharactersAtStart("#"));
}

juce::String mmss(double s)
{
    const int t = static_cast<int>(std::ceil(s));
    return juce::String(t / 60) + ":" + juce::String(t % 60).paddedLeft('0', 2);
}

} // namespace

CartPad::CartPad(EngineController& controller, int cart) : controller_(controller), cart_(cart)
{
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
    setTooltip("Click: play / restart.  Shift+click: fade out.  Ctrl+click: stop.  Right-click: settings.");
}

void CartPad::paint(juce::Graphics& g)
{
    auto& sb = controller_.engine().soundboard();
    const auto& s = sb.settings(cart_);
    const auto& st = sb.state(cart_);
    const auto buf = sb.buffer(cart_);
    const bool loaded = buf && buf->error.empty() && buf->frames > 0;
    const bool playing = st.playing.load();
    const auto col = padColour(s.colour);
    auto b = getLocalBounds().toFloat().reduced(2.0f);
    g.setColour(loaded ? col.withAlpha(playing ? 0.9f : 0.28f) : colours::panelRaised);
    g.fillRoundedRectangle(b, 5.0f);
    g.setColour(playing ? juce::Colours::white : (loaded ? col : colours::outline));
    g.drawRoundedRectangle(b, 5.0f, playing ? 2.0f : 1.0f);

    auto t = b.reduced(8.0f, 4.0f);
    const float fs = juce::jlimit(11.0f, 16.0f, b.getHeight() * 0.22f);
    g.setColour(playing ? juce::Colours::black : colours::text);
    g.setFont(juce::Font(juce::FontOptions(fs, juce::Font::bold)));
    g.drawText(juce::String::fromUTF8(s.name.c_str()), t.removeFromTop(t.getHeight() * 0.5f), juce::Justification::centredLeft, true);
    g.setFont(juce::Font(juce::FontOptions(fs * 0.75f)));
    juce::String info;
    if (buf && !buf->error.empty()) info = juce::String::fromUTF8(buf->error.c_str());
    else if (!loaded) info = "empty - right-click to load";
    else
    {
        const double len = static_cast<double>(buf->frames) / controller_.settings().sampleRate;
        const double pos = static_cast<double>(st.position.load()) / controller_.settings().sampleRate;
        info = playing ? "-" + mmss(len - pos) : mmss(len);
        if (s.loop.load()) info << "  LOOP";
        if (!s.hotkey.empty()) info << "  [" << juce::String::fromUTF8(s.hotkey.c_str()) << "]";
    }
    g.setColour(playing ? juce::Colours::black.withAlpha(0.8f) : colours::textDim);
    g.drawText(info, t, juce::Justification::centredLeft, true);
    if (playing && loaded)
    {
        const float prog = static_cast<float>(st.position.load()) / static_cast<float>(std::max<int64_t>(1, buf->frames));
        g.setColour(juce::Colours::black.withAlpha(0.35f));
        g.fillRect(b.removeFromBottom(4.0f).withWidth(b.getWidth() * juce::jlimit(0.0f, 1.0f, prog)));
    }
}

void CartPad::mouseUp(const juce::MouseEvent& e)
{
    if (!e.mouseWasClicked()) return;
    auto& sb = controller_.engine().soundboard();
    if (e.mods.isPopupMenu())
    {
        showMenu();
        return;
    }
    const auto buf = sb.buffer(cart_);
    if (!buf || !buf->error.empty())
    {
        if (onLoadRequest) onLoadRequest(cart_);
        return;
    }
    if (e.mods.isShiftDown()) sb.fadeOut(cart_);
    else if (e.mods.isCtrlDown()) sb.stop(cart_);
    else sb.play(cart_);
}

void CartPad::showMenu()
{
    auto& sb = controller_.engine().soundboard();
    auto& s = sb.settings(cart_);
    juce::PopupMenu m, colourMenu, volMenu, inMenu, outMenu;
    m.addItem(1, "Load file (WAV, MP3, FLAC)...");
    m.addItem(2, "Rename...");
    for (int i = 0; i < 8; ++i) colourMenu.addColouredItem(100 + i, "colour " + juce::String(i + 1), kPadColours[i]);
    m.addSubMenu("Colour", colourMenu);
    const float volDb = 20.0f * std::log10(std::max(1e-4f, s.volume.get()));
    for (int db : {0, -3, -6, -10, -15, -20}) volMenu.addItem(200 - db, juce::String(db) + " dB", true, std::abs(volDb - db) < 0.5f);
    m.addSubMenu("Volume", volMenu);
    for (int ms : {0, 50, 250, 1000, 3000}) inMenu.addItem(300 + ms / 10, juce::String(ms) + " ms", true, std::abs(s.fadeInMs.get() - ms) < 1.0f);
    m.addSubMenu("Fade in", inMenu);
    for (int ms : {250, 750, 1500, 3000, 6000}) outMenu.addItem(1300 + ms / 10, juce::String(ms) + " ms", true, std::abs(s.fadeOutMs.get() - ms) < 1.0f);
    m.addSubMenu("Fade out", outMenu);
    m.addItem(3, "Loop", true, s.loop.load());
    m.addItem(4, "Hotkey...");
    m.addSeparator();
    m.addItem(5, "Clear");
    juce::Component::SafePointer<CartPad> safe(this);
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this), [safe](int r) {
        if (!safe || r == 0) return;
        auto& sbr = safe->controller_.engine().soundboard();
        auto& st = sbr.settings(safe->cart_);
        if (r == 1 && safe->onLoadRequest) safe->onLoadRequest(safe->cart_);
        else if (r == 2 || r == 4)
        {
            auto* w = new juce::AlertWindow(r == 2 ? "Rename cart" : "Cart hotkey", r == 4 ? "Key name, e.g. F5 or Ctrl+1 (active from the hotkey manager)" : "",
                                            juce::MessageBoxIconType::NoIcon, safe.getComponent());
            w->addTextEditor("v", juce::String::fromUTF8((r == 2 ? st.name : st.hotkey).c_str()));
            w->addButton("OK", 1, juce::KeyPress(juce::KeyPress::returnKey));
            w->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
            w->enterModalState(true, juce::ModalCallbackFunction::create([safe, w, r](int ok) {
                                   if (!safe || ok != 1) return;
                                   auto& s2 = safe->controller_.engine().soundboard().settings(safe->cart_);
                                   (r == 2 ? s2.name : s2.hotkey) = w->getTextEditorContents("v").toStdString();
                                   safe->repaint();
                               }),
                               true);
        }
        else if (r == 3) st.loop = !st.loop.load();
        else if (r == 5)
        {
            sbr.stop(safe->cart_);
            sbr.setBuffer(safe->cart_, nullptr);
        }
        else if (r >= 100 && r < 108) st.colour = "#" + kPadColours[r - 100].toDisplayString(false).toStdString();
        else if (r >= 180 && r <= 220) st.volume.set(std::pow(10.0f, -(static_cast<float>(r) - 200.0f) / 20.0f));
        else if (r >= 300 && r < 1300) st.fadeInMs.set(static_cast<float>((r - 300) * 10));
        else if (r >= 1300) st.fadeOutMs.set(static_cast<float>((r - 1300) * 10));
        safe->repaint();
    });
}

SoundboardView::SoundboardView(EngineController& controller) : controller_(controller)
{
    for (int i = 0; i < kCartCount; ++i)
    {
        pads_[static_cast<size_t>(i)] = std::make_unique<CartPad>(controller, i);
        pads_[static_cast<size_t>(i)]->onLoadRequest = [this](int cart) {
            chooser_ = std::make_unique<juce::FileChooser>("Load a sound for cart " + juce::String(cart + 1), juce::File(), supportedAudioWildcard());
            chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                  [this, cart](const juce::FileChooser& fc) {
                                      if (fc.getResult().existsAsFile()) loadFile(cart, fc.getResult());
                                  });
        };
        addAndMakeVisible(*pads_[static_cast<size_t>(i)]);
    }
    stopAll_.onClick = [this] { controller_.engine().soundboard().stopAll(); };
    fadeAll_.onClick = [this] { controller_.engine().soundboard().fadeAll(); };
    stopAll_.setColour(juce::TextButton::buttonColourId, colours::mute.darker(0.5f));
    hint_.setText("Carts play into Main and the headphones (ROUTING row Carts). Right-click a pad to load a sound.", juce::dontSendNotification);
    hint_.setFont(juce::FontOptions(12.0f));
    hint_.setColour(juce::Label::textColourId, colours::textDim);
    allowEllipsis(hint_);
    for (juce::Component* c : std::initializer_list<juce::Component*>{&stopAll_, &fadeAll_, &hint_}) addAndMakeVisible(c);
    startTimerHz(15);
}

SoundboardView::~SoundboardView() { stopTimer(); }

void SoundboardView::loadFile(int cart, const juce::File& file)
{
    const std::filesystem::path path(file.getFullPathName().toWideCharPointer());
    const int rate = controller_.settings().sampleRate;
    juce::Component::SafePointer<SoundboardView> safe(this);
    auto& sb = controller_.engine().soundboard();
    std::thread([path, rate, safe, &sb, cart] {
        auto buf = loadAudioFile(path, {rate, 600.0});
        const std::string name = paths::utf8(path.stem());
        juce::MessageManager::callAsync([safe, buf, name, &sb, cart] {
            sb.setBuffer(cart, buf);
            if (buf->error.empty()) sb.settings(cart).name = name;
            PF8_LOG_INFO("media", "cart %d loaded %s frames=%lld %s", cart + 1, buf->sourcePath.c_str(), static_cast<long long>(buf->frames),
                         buf->error.c_str());
            if (safe) safe->repaint();
        });
    }).detach();
}

void SoundboardView::timerCallback()
{
    for (int i = 0; i < kCartCount; ++i)
        if (controller_.engine().soundboard().state(i).playing.load()) pads_[static_cast<size_t>(i)]->refresh();
    static int tick = 0;
    if (++tick % 15 == 0)
        for (auto& p : pads_) p->refresh();
}

void SoundboardView::paint(juce::Graphics& g) { g.fillAll(colours::background); }

void SoundboardView::resized()
{
    auto b = getLocalBounds().reduced(8);
    auto top = b.removeFromTop(28);
    stopAll_.setBounds(top.removeFromLeft(110));
    top.removeFromLeft(6);
    fadeAll_.setBounds(top.removeFromLeft(110));
    top.removeFromLeft(12);
    hint_.setBounds(top);
    b.removeFromTop(6);
    constexpr int cols = 8, rows = 3;
    const float w = static_cast<float>(b.getWidth()) / cols, h = static_cast<float>(b.getHeight()) / rows;
    for (int i = 0; i < kCartCount; ++i)
    {
        const int c = i % cols, r = i / cols;
        pads_[static_cast<size_t>(i)]->setBounds(juce::Rectangle<int>(b.getX() + static_cast<int>(w * static_cast<float>(c)),
                                                                      b.getY() + static_cast<int>(h * static_cast<float>(r)), static_cast<int>(w),
                                                                      static_cast<int>(h)));
    }
}

void SoundboardView::collectLayoutIssues(std::vector<std::string>& issues) const
{
    if (pads_[0]->getHeight() < 36) issues.push_back("cart pads below 36 px");
}

} // namespace pf8::ui
