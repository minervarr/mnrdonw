#pragma once

#include "editor/document.hh"
#include "editor/flow.hh"
#include "editor/layout.hh"
#include "editor/parse.hh"
#include "frame_input_view.hh"

#include "arc/fs/walk.hh"
#include "ui_metrics.hh"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

class Canvas;
class Host;
class Renderer;
class MsdfFont;

class EditorApp : public FrameInputView {
public:
    EditorApp();
    ~EditorApp() override;

    // Builds the host, loads argv[1] when present, and prepares the renderer.
    // A missing or unreadable path calls showErrorMessage and returns false.
    bool create(int argc, char** argv);
    int run();

    void onHostResized() override;
    void onHostExposed() override;
    void onHostLayoutInvalidated() override;
    void shutdown() override;
    void onTimer(int timerId) override;

private:
    struct Geom {
        UiMetrics ui{};
        LayoutMetrics lm{};
        float columnX = 0;
        float columnW = 0;
        float columnTop = 0;
        float viewH = 0;
        float body = 0;
        float lineH = 0;
        float nameSize = 0;
        float margin = 0;
        float contentX = 0;
        float contentY = 0;
        float contentW = 0;
    };

    struct BrowseRow {
        std::string label;
        std::string path;
        bool isDir = false;
    };

    struct HomeHit {
        float newX = 0;
        float newW = 0;
        float browseX = 0;
        float browseW = 0;
        bool showBrowse = false;
        float flowTop = 0;
        float flowView = 0;
        float rowH = 0;
    };

    bool readLaunchFile(int argc, char** argv);
    bool initGpu();
    void loadRecentList();
    void storeRecent();
    void touchRecent(const std::string& path);
    void dropRecent(const std::string& path);
    bool openFile(const std::string& path, bool fromLaunch);
    void openNew();
    void goHome();
    void adoptListing(const std::vector<arc::fs::Entry>& entries);
    void reloadBrowse();
    void enterDirectory(const std::string& path);
    void layoutFrame();
    HomeHit layoutHome();
    void clampHomeScroll(const HomeHit& hit, float contentHeight);
    void handleHomeInput();
    void handleEscape();
    void saveNow();
    void layoutNow();
    void handleInput();
    void drawHome();
    void drawWrite();
    void draw();
    void pokeCaret();
    void clampScroll();
    void ensureCaretVisible();
    void placeCaret(size_t pos, bool extend, bool vertical, bool keepEndPin = false);

    std::unique_ptr<Host> host_;
    std::unique_ptr<MsdfFont> font_;
    std::unique_ptr<Renderer> renderer_;
    Document doc_;
    std::vector<Block> blocks_;
    Page page_;
    Geom geom_;
    // Valid only while a Canvas for this frame is on the stack.
    Canvas* canvas_ = nullptr;
    Measurer* measurer_ = nullptr;
    std::vector<float> curves_;
    std::vector<float> quads_;
    std::vector<float> shapes_;

    std::string path_;
    std::string saveError_;
    std::string homeError_;
    std::string browseDir_;
    std::vector<std::string> recent_;
    std::vector<BrowseRow> browseRows_;
    std::vector<FlowSlot> homeSlots_;
    enum class Screen { Home, Write };
    Screen screen_ = Screen::Home;
    bool browsing_ = false;
    bool discardArmed_ = false;
    bool recentWritable_ = true;
    float homeScroll_ = 0;
    bool running_ = false;
    bool stale_ = true;
    bool caretOn_ = true;
    bool dragging_ = false;
    bool preferredValid_ = false;
    float preferredX_ = 0;
    float scroll_ = 0;
    // End on a hard wrap shares its byte with the next row's sourceBegin.
    // Drawing and ensureCaretVisible keep the row that ends on that byte.
    bool endPin_ = false;
    size_t endPinByte_ = 0;
    double lastClickSec_ = -1.0e9;
};
