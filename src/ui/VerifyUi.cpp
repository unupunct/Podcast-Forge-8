#include "ui/VerifyUi.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/Json.h"
#include "ui/ChannelStripView.h"
#include "ui/MainWindow.h"
#include "ui/DspEditor.h"
#include "ui/MasterStripView.h"
#include "ui/MicWizard.h"
#include "ui/Widgets.h"

namespace pf8::ui {
namespace {

struct Config
{
    int w, h;
    float scale;
};
// Physical resolution and Windows display scale. Content size ≈ logical minus window chrome/taskbar.
const Config kConfigs[] = {{1920, 1080, 1.0f}, {1920, 1080, 1.25f}, {1920, 1080, 1.5f}, {2560, 1440, 1.0f},
                           {2560, 1440, 1.5f}, {3840, 2160, 1.5f},  {3840, 2160, 2.0f}};

float textWidth(const juce::Font& f, const juce::String& s)
{
    juce::GlyphArrangement ga;
    ga.addLineOfText(f, s, 0.0f, 0.0f);
    return ga.getBoundingBox(0, -1, true).getWidth();
}

std::string nameOf(juce::Component& c)
{
    juce::String n = c.getName();
    if (n.isEmpty())
    {
        if (auto* b = dynamic_cast<juce::Button*>(&c)) n = "button '" + b->getButtonText() + "'";
        else if (auto* l = dynamic_cast<juce::Label*>(&c)) n = "label '" + l->getText().substring(0, 30) + "'";
        else n = typeid(c).name();
    }
    return n.toStdString();
}

bool isInternalContainer(juce::Component& c)
{
    return dynamic_cast<juce::Viewport*>(&c) || dynamic_cast<juce::ListBox*>(&c) || dynamic_cast<juce::ComboBox*>(&c) ||
           dynamic_cast<juce::TabbedButtonBar*>(&c) || dynamic_cast<juce::Slider*>(&c) || dynamic_cast<juce::TextEditor*>(&c) ||
           dynamic_cast<juce::TableHeaderComponent*>(&c);
}

void check(juce::Component& c, const std::string& path, std::vector<std::string>& issues)
{
    if (!c.isVisible()) return;
    const std::string here = path + "/" + nameOf(c);
    if (c.getWidth() <= 0 || c.getHeight() <= 0) issues.push_back(here + ": zero size");

    if (auto* self = dynamic_cast<LayoutSelfCheck*>(&c))
    {
        std::vector<std::string> own;
        self->collectLayoutIssues(own);
        for (auto& o : own) issues.push_back(here + ": " + o);
    }
    if (auto* b = dynamic_cast<juce::TextButton*>(&c))
    {
        const auto f = c.getLookAndFeel().getTextButtonFont(*b, b->getHeight());
        if (textWidth(f, b->getButtonText()) > static_cast<float>(b->getWidth() - 4))
            issues.push_back(here + ": text doesn't fit (" + std::to_string(b->getWidth()) + " px)");
    }
    if (auto* l = dynamic_cast<juce::Label*>(&c); l && !l->isBeingEdited() && !ellipsisAllowed(*l))
    {
        const auto border = l->getBorderSize();
        const float avail = static_cast<float>(l->getWidth() - border.getLeftAndRight());
        const float need = textWidth(l->getFont(), l->getText());
        const float minScale = l->getMinimumHorizontalScale() > 0.0f ? l->getMinimumHorizontalScale() : 0.7f;
        // Multi-line labels (explicit newlines) are checked per line.
        juce::StringArray lines;
        lines.addLines(l->getText());
        float widest = 0.0f;
        for (auto& line : lines) widest = juce::jmax(widest, textWidth(l->getFont(), line));
        if (lines.size() <= 1 && need * minScale > avail + 0.5f)
            issues.push_back(here + ": text clipped (" + std::to_string(static_cast<int>(need)) + " > " + std::to_string(static_cast<int>(avail)) + " px)");
        else if (lines.size() > 1 && widest * minScale > avail + 0.5f)
            issues.push_back(here + ": a line is clipped");
        if (lines.size() > 1 && l->getFont().getHeight() * static_cast<float>(lines.size()) > static_cast<float>(l->getHeight()) + 1.0f)
            issues.push_back(here + ": lines don't fit vertically");
    }
    if (isInternalContainer(c)) return;

    const auto parentBounds = c.getLocalBounds().expanded(1);
    std::vector<juce::Component*> visibleKids;
    for (auto* child : c.getChildren())
    {
        if (!child->isVisible()) continue;
        visibleKids.push_back(child);
        if (!parentBounds.contains(child->getBounds()))
            issues.push_back(here + "/" + nameOf(*child) + ": outside its parent");
    }
    // Controls inside a console strip must not overlap.
    if (dynamic_cast<ChannelStripView*>(&c) || dynamic_cast<MasterStripView*>(&c))
        for (size_t i = 0; i < visibleKids.size(); ++i)
            for (size_t j = i + 1; j < visibleKids.size(); ++j)
            {
                const auto inter = visibleKids[i]->getBounds().getIntersection(visibleKids[j]->getBounds());
                if (inter.getWidth() > 1 && inter.getHeight() > 1)
                    issues.push_back(here + ": " + nameOf(*visibleKids[i]) + " overlaps " + nameOf(*visibleKids[j]));
            }
    for (auto* child : visibleKids) check(*child, here, issues);
}

// Long, realistic names (and OFFLINE states) so text-heavy cases are exercised.
Assignments sampleAssignments()
{
    const char* mics[8] = {"HyperX QuadCast S USB Microphone", "Rode NT-USB+", "Shure MV7+ Podcast Microphone", "Blue Yeti Stereo Microphone",
                           "Elgato Wave:3", "Audio-Technica AT2020USB-XP", "Samson Q9U Dynamic", "Logitech G Yeti GX"};
    const char* names[8] = {"Host 1", "Host 2", "Guest 1", "Guest 2", "Guest 3", "Guest 4", "Guest 5 (remote studio)", "Guest 6"};
    Assignments a = Assignments::defaults();
    for (int i = 0; i < 8; ++i)
    {
        DeviceIdentity mic;
        mic.endpointId = "{0.0.1.00000000}.{verify-mic-" + std::to_string(i) + "}";
        mic.flow = Flow::Capture;
        mic.friendlyName = mics[i];
        a.ch[static_cast<size_t>(i)].mic = mic;
        DeviceIdentity hp;
        hp.endpointId = "{0.0.0.00000000}.{verify-hp-" + std::to_string(i) + "}";
        hp.flow = Flow::Render;
        hp.friendlyName = std::string("USB Headset #") + std::to_string(i + 1) + " (Realtek USB2.0 Audio)";
        a.ch[static_cast<size_t>(i)].headphones = hp;
        a.ch[static_cast<size_t>(i)].name = names[i];
    }
    return a;
}

} // namespace

VerifyUiResult runVerifyUi(EngineController& controller, const std::filesystem::path& outDir)
{
    VerifyUiResult result;
    result.outputDir = outDir;
    std::error_code ec;
    std::filesystem::create_directories(outDir, ec);

    controller.applyAssignments(sampleAssignments());
    controller.waitIdle();

    LookAndFeel laf;
    juce::LookAndFeel::setDefaultLookAndFeel(&laf);

    // Canary: a deliberately broken layout must be reported, or the checker itself is broken.
    {
        juce::Component canary;
        canary.setSize(300, 200);
        canary.setVisible(true); // offscreen roots are invisible by default and would be skipped
        juce::Label longLabel({}, "This label text is far too long for fifty pixels");
        juce::TextButton narrow("CONFIGURE");
        Knob tiny("GAIN", 0, 1, 0, nullptr);
        canary.addAndMakeVisible(longLabel);
        canary.addAndMakeVisible(narrow);
        canary.addAndMakeVisible(tiny);
        longLabel.setBounds(0, 0, 50, 20);
        narrow.setBounds(0, 30, 30, 20);
        tiny.setBounds(0, 60, 30, 30);
        std::vector<std::string> canaryIssues;
        check(canary, "canary", canaryIssues);
        if (canaryIssues.size() < 3)
            result.issues.push_back("checker self-test failed: expected 3 canary issues, got " + std::to_string(canaryIssues.size()));
    }

    json::Array screens;
    {
        MainComponent main(controller);
        main.setVisible(true);
        for (const auto& cfg : kConfigs)
        {
            const int lw = static_cast<int>(static_cast<float>(cfg.w) / cfg.scale);
            const int lh = static_cast<int>(static_cast<float>(cfg.h) / cfg.scale);
            // Maximised window content: minus borders, title bar and taskbar.
            const int cw = juce::jmax(1280, lw - 16), chh = juce::jmax(720, lh - 90);
            main.setSize(cw, chh);
            // Main tabs, plus every bottom-dock tab of the mixer page.
            struct View { int tab, dock; const char* name; };
            // Every main tab (named after its title), then the mixer page's other dock tabs.
            std::vector<View> views = {{0, 0, "mixer"}};
            std::vector<std::string> tabNames;
            for (int t = 1; t < main.tabs().getNumTabs(); ++t)
                tabNames.push_back(main.tabs().getTabNames()[t].toLowerCase().replaceCharacter(' ', '-').toStdString());
            for (int t = 1; t < main.tabs().getNumTabs(); ++t) views.push_back({t, -1, tabNames[static_cast<size_t>(t - 1)].c_str()});
            if (auto* page = dynamic_cast<MixerPage*>(main.tabs().getTabContentComponent(0)))
                for (int d = 1; d < page->dock().getNumTabs(); ++d)
                    views.push_back({0, d, nullptr});
            for (size_t vi = 0; vi < views.size(); ++vi)
            {
                const int tab = views[vi].tab;
                main.tabs().setCurrentTabIndex(tab, false);
                std::string viewName = views[vi].name ? views[vi].name : "";
                if (auto* page = dynamic_cast<MixerPage*>(main.tabs().getTabContentComponent(0)); page && views[vi].dock >= 0)
                {
                    page->dock().setCurrentTabIndex(views[vi].dock, false);
                    if (viewName.empty()) viewName = "mixer-" + page->dock().getTabNames()[views[vi].dock].toLowerCase().toStdString();
                }
                main.resized();
                if (auto* page = main.tabs().getCurrentContentComponent()) page->resized();
                std::vector<std::string> issues;
                check(main, "", issues);
                const std::string tag = viewName + "_" + std::to_string(cfg.w) + "x" + std::to_string(cfg.h) + "@" +
                                        std::to_string(static_cast<int>(cfg.scale * 100)) + "pct";
                for (auto& i : issues) result.issues.push_back(tag + ": " + i);

                auto image = main.createComponentSnapshot(main.getLocalBounds(), true, cfg.scale);
                const auto file = outDir / (tag + ".png");
                juce::File jf(juce::String(file.wstring().c_str()));
                jf.deleteFile(); // our own verification output, regenerated each run
                if (auto stream = jf.createOutputStream())
                {
                    juce::PNGImageFormat png;
                    png.writeImageToStream(image, *stream);
                }
                ++result.screensRendered;
                json::Object s;
                s["screen"] = tag;
                s["logical"] = std::to_string(cw) + "x" + std::to_string(chh);
                s["issues"] = static_cast<int>(issues.size());
                screens.emplace_back(std::move(s));
            }
        }
    }
    // Dialog screens at their minimum and typical sizes, at every scale.
    {
        struct Dlg { const char* name; int w, h; };
        const Dlg dialogs[] = {{"dsp-editor", 1100, 680}, {"dsp-editor", 1440, 880}, {"mic-wizard", 860, 600}};
        for (const auto& d : dialogs)
            for (float scale : {1.0f, 1.5f, 2.0f})
            {
                std::unique_ptr<juce::Component> comp;
                if (std::string(d.name) == "dsp-editor")
                {
                    auto* e = new DspEditor(controller, 0);
                    controller.engine().dsp(0).compOn = true;
                    comp.reset(e);
                }
                else
                {
                    auto* w = new MicWizard(controller, 0);
                    dsp::MicAnalysis r;
                    r.haveNoise = r.haveSpeech = r.signalDetected = true;
                    r.noiseFloorDb = -58.3f;
                    r.peakDb = -9.4f;
                    r.averageDb = -27.1f;
                    r.clipCount = 12;
                    r.recommendedTrimDb = 3.4f;
                    r.hpfHz = 100.0f;
                    r.advice = {"The microphone clipped 12 times: lower the gain ON THE MICROPHONE / interface. Software trim cannot repair clipping.",
                                "Low-frequency rumble detected: high-pass set to 100 Hz."};
                    w->showResultsForTest(r);
                    comp.reset(w);
                }
                comp->setVisible(true);
                comp->setSize(d.w, d.h);
                std::vector<std::string> issues;
                check(*comp, "", issues);
                const std::string tag = std::string(d.name) + "_" + std::to_string(d.w) + "x" + std::to_string(d.h) + "@" +
                                        std::to_string(static_cast<int>(scale * 100)) + "pct";
                for (auto& i : issues) result.issues.push_back(tag + ": " + i);
                auto image = comp->createComponentSnapshot(comp->getLocalBounds(), true, scale);
                juce::File jf(juce::String((outDir / (tag + ".png")).wstring().c_str()));
                jf.deleteFile();
                if (auto stream = jf.createOutputStream()) juce::PNGImageFormat().writeImageToStream(image, *stream);
                ++result.screensRendered;
                json::Object s;
                s["screen"] = tag;
                s["issues"] = static_cast<int>(issues.size());
                screens.emplace_back(std::move(s));
            }
        controller.engine().setAnalysisChannel(-1);
    }
    juce::LookAndFeel::setDefaultLookAndFeel(nullptr);

    json::Object report;
    report["screens"] = std::move(screens);
    json::Array iss;
    for (auto& i : result.issues) iss.emplace_back(i);
    report["issues"] = std::move(iss);
    report["ok"] = result.issues.empty();
    juce::File(juce::String((outDir / "report.json").wstring().c_str())).replaceWithText(json::serialize(json::Value(std::move(report)), true));
    return result;
}

} // namespace pf8::ui
