#include "editor_app.hh"

#include "linux_desktop.hh"

#include "editor/inline.hh"
#include "editor/margin.hh"
#include "editor/recent.hh"
#include "editor/save.hh"
#include "editor/utf8.hh"

#include "app_paths.hh"
#include "arc/fs/paths.hh"
#include "canvas.hh"
#include "host.hh"
#include "keys.hh"
#include "msdf.hh"
#include "output_target.hh"
#include "renderer.hh"
#include "text_font.hh"
#include "text_util.hh"
#include "widgets.hh"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <sys/stat.h>
#include <utility>
#include <vector>

namespace {

constexpr int kCaretTimer = 1;
constexpr double kDoubleClickSec = 0.4;

FontStyle toStyle(Face face) {
    if (face == Face::Bold) return FontStyle::Bold;
    if (face == Face::Italic) return FontStyle::Italic;
    return FontStyle::Roman;
}

// Same face layout measured with. Rendered headings 4–6 are Bold on every span.
Face rowFace(const Row& row, SpanRole role) {
    if (!row.sourceMode && row.kind == BlockKind::Heading && row.level >= 4) return Face::Bold;
    if (role == SpanRole::Em) return Face::Italic;
    if (role == SpanRole::Strong || role == SpanRole::StrongEm) return Face::Bold;
    return Face::Roman;
}

size_t mapSpanToSource(const Span& s, size_t displayOff) {
    if (displayOff <= s.displayBegin) return s.sourceBegin;
    if (displayOff >= s.displayEnd) return s.sourceEnd;
    if (s.displayEnd - s.displayBegin == s.sourceEnd - s.sourceBegin) {
        return s.sourceBegin + (displayOff - s.displayBegin);
    }
    return s.sourceBegin;
}

size_t rowIndexFor(const Page& page, size_t cursorByte, bool pinEnd, size_t pinByte) {
    if (pinEnd && cursorByte == pinByte) {
        for (size_t i = 0; i < page.rows.size(); ++i) {
            const Row& r = page.rows[i];
            if (r.sourceBegin != r.sourceEnd && r.sourceEnd == cursorByte) return i;
        }
    }
    size_t found = 0;
    bool any = false;
    for (size_t i = 0; i < page.rows.size(); ++i) {
        const Row& r = page.rows[i];
        if (r.sourceBegin == r.sourceEnd) {
            if (cursorByte == r.sourceBegin) {
                found = i;
                any = true;
            }
            continue;
        }
        if (cursorByte >= r.sourceBegin && cursorByte < r.sourceEnd) {
            found = i;
            any = true;
        }
    }
    if (!any && !page.rows.empty()) return page.rows.size() - 1;
    return found;
}

// End goes through visualRowEnd. A hard wrap keeps sourceEnd; a newline or
// soft-break space keeps that byte so the caret stays on this row.
size_t rowEndByte(const Page& page, size_t index, std::string_view text) {
    return visualRowEnd(page, index, text);
}

class CanvasMeasurer : public Measurer {
public:
    Canvas* canvas = nullptr;
    const TextFont* font = nullptr;

    float width(std::string_view text, float sizePx, Face face) const override {
        return canvas->textWidthStyled(text, sizePx, toStyle(face));
    }

    // Canvas has no line-height query. textStyled draws with this font, and
    // lineHeight is that font's em line spacing.
    float lineHeight(float sizePx) const override { return font->lineHeight(sizePx); }
};

std::string absolutePath(const std::string& path) {
    std::error_code ec;
    const std::filesystem::path abs = std::filesystem::absolute(std::filesystem::u8path(path), ec);
    if (ec) return path;
    return abs.u8string();
}

std::string fileTitle(const std::string& path) {
    if (path.empty()) return "untitled.md";
    const size_t slash = path.find_last_of('/');
    if (slash == std::string::npos) return path;
    return path.substr(slash + 1);
}

std::string trimMarker(std::string marker) {
    while (!marker.empty() && (marker.back() == ' ' || marker.back() == '\t')) marker.pop_back();
    size_t i = 0;
    while (i < marker.size() && (marker[i] == ' ' || marker[i] == '\t')) ++i;
    return marker.substr(i);
}

double nowSeconds() {
    using clock = std::chrono::steady_clock;
    return std::chrono::duration<double>(clock::now().time_since_epoch()).count();
}

// Column-local x of a source byte. Same face layout measured and textStyled draws.
float xInRow(const Row& row, size_t byte, const Measurer& measure) {
    float x = row.textX;
    for (const Span& s : row.spans) {
        const Face face = rowFace(row, s.role);
        size_t d = s.displayBegin;
        while (d < s.displayEnd) {
            if (mapSpanToSource(s, d) >= byte) return x;
            const size_t dn = utf8Next(row.display, d);
            x += measure.width(std::string_view(row.display).substr(d, dn - d), row.sizePx, face);
            d = dn;
        }
    }
    return x;
}

bool rowInBlock(const Row& row, const Block& block) {
    if (row.sourceBegin == row.sourceEnd) {
        return row.sourceBegin >= block.begin && row.sourceBegin < block.end;
    }
    return row.sourceBegin >= block.begin && row.sourceBegin < block.end &&
           row.sourceEnd <= block.end;
}

bool rowOverlapsSelection(const Row& row, size_t lo, size_t hi) {
    if (lo == hi) return false;
    if (row.sourceBegin == row.sourceEnd) return lo <= row.sourceBegin && hi > row.sourceBegin;
    return lo < row.sourceEnd && hi > row.sourceBegin;
}

}  // namespace

EditorApp::EditorApp() = default;
EditorApp::~EditorApp() = default;

void EditorApp::loadRecentList() {
    const RecentLoad loaded = loadRecent(app_paths::stateDir() + "recent.txt");
    if (!loaded.ok) {
        recentWritable_ = false;
        recent_.clear();
        return;
    }
    recent_ = loaded.paths;
}

void EditorApp::storeRecent() {
    if (!recentWritable_) return;
    std::string error;
    saveRecent(app_paths::stateDir() + "recent.txt", recent_, error);
}

void EditorApp::touchRecent(const std::string& path) {
    if (path.empty()) return;
    recent_ = rememberPath(std::move(recent_), path, 40);
    storeRecent();
}

void EditorApp::dropRecent(const std::string& path) {
    recent_ = forgetPath(std::move(recent_), path);
    storeRecent();
}

bool EditorApp::openFile(const std::string& path, bool fromLaunch) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        const std::string message =
            std::string("Could not read ") + path + ": " + std::strerror(errno);
        if (fromLaunch) {
            host_->showErrorMessage("mnrdonw", message);
            return false;
        }
        homeError_ = message;
        dropRecent(path);
        if (recent_.empty()) {
            browsing_ = true;
            const std::string kept = homeError_;
            reloadBrowse();
            homeError_ = kept;
        }
        stale_ = true;
        return true;
    }
    std::string text;
    char buf[4096];
    while (true) {
        const size_t n = std::fread(buf, 1, sizeof buf, f);
        text.append(buf, n);
        if (n < sizeof buf) break;
    }
    if (std::ferror(f)) {
        const std::string message =
            std::string("Could not read ") + path + ": " + std::strerror(errno);
        std::fclose(f);
        if (fromLaunch) {
            host_->showErrorMessage("mnrdonw", message);
            return false;
        }
        homeError_ = message;
        dropRecent(path);
        stale_ = true;
        return true;
    }
    std::fclose(f);
    doc_.load(std::move(text));
    path_ = absolutePath(path);
    saveError_.clear();
    discardArmed_ = false;
    homeError_.clear();
    scroll_ = 0;
    preferredValid_ = false;
    endPin_ = false;
    screen_ = Screen::Write;
    touchRecent(path_);
    stale_ = true;
    return true;
}

void EditorApp::openNew() {
    doc_.load("");
    path_.clear();
    saveError_.clear();
    discardArmed_ = false;
    homeError_.clear();
    scroll_ = 0;
    preferredValid_ = false;
    endPin_ = false;
    screen_ = Screen::Write;
    stale_ = true;
}

void EditorApp::goHome() {
    screen_ = Screen::Home;
    homeScroll_ = 0;
    homeError_.clear();
    discardArmed_ = false;
    browsing_ = recent_.empty();
    if (browsing_) reloadBrowse();
    stale_ = true;
}

void EditorApp::adoptListing(const std::vector<arc::fs::Entry>& entries) {
    browseRows_.clear();
    const std::string up = arc::fs::parent(browseDir_);
    if (up != browseDir_) browseRows_.push_back(BrowseRow{"..", up, true});
    for (const arc::fs::Entry& entry : entries) {
        std::string label = arc::fs::filename(entry.path);
        if (entry.isDir) label.push_back('/');
        browseRows_.push_back(BrowseRow{std::move(label), entry.path, entry.isDir});
    }
}

void EditorApp::reloadBrowse() {
    if (browseDir_.empty()) browseDir_ = arc::fs::userDocuments();
    if (browseDir_.empty()) {
        browseRows_.clear();
        homeError_ = "no home directory";
        return;
    }
    const arc::Result<std::vector<arc::fs::Entry>> listed = arc::fs::listDir(browseDir_);
    if (!listed.ok()) {
        browseRows_.clear();
        homeError_ = listed.error().message;
        return;
    }
    adoptListing(listed.value());
    homeError_.clear();
}

void EditorApp::enterDirectory(const std::string& path) {
    const arc::Result<std::vector<arc::fs::Entry>> listed = arc::fs::listDir(path);
    if (!listed.ok()) {
        homeError_ = listed.error().message;
        stale_ = true;
        return;
    }
    browseDir_ = path;
    homeScroll_ = 0;
    adoptListing(listed.value());
    homeError_.clear();
    stale_ = true;
}

bool EditorApp::readLaunchFile(int argc, char** argv) {
    if (argc < 2 || argv == nullptr || argv[1] == nullptr || argv[1][0] == '\0') {
        doc_.load("");
        path_.clear();
        screen_ = Screen::Home;
        browsing_ = recent_.empty();
        if (browsing_) reloadBrowse();
        return true;
    }
    return openFile(argv[1], true);
}

bool EditorApp::initGpu() {
    try {
        renderer_ = std::make_unique<Renderer>(host_->surfaceProvider(), host_->assetReader(), 3,
                                               OutputTarget::SdrSrgb, PresentPolicy::Vsync);
    } catch (const std::exception& ex) {
        host_->showErrorMessage("mnrdonw", ex.what());
        return false;
    }

    font_ = std::make_unique<MsdfFont>();
    AssetReader& assets = host_->assetReader();
    const std::string cache = app_paths::stateDir() + "newcm10.msdfcache";
    const char* regular = "fonts/newcomputermodern/NewCM10-Regular.otf";
    const char* bold = "fonts/newcomputermodern/NewCM10-Bold.otf";
    const char* italic = "fonts/newcomputermodern/NewCM10-Italic.otf";
    if (!font_->generate(assets, regular, cache.c_str()) ||
        (!font_->hasStyle(FontStyle::Bold) && !font_->addStyle(assets, bold, FontStyle::Bold)) ||
        (!font_->hasStyle(FontStyle::Italic) &&
         !font_->addStyle(assets, italic, FontStyle::Italic))) {
        host_->showErrorMessage("mnrdonw", "Could not load the New Computer Modern faces");
        renderer_.reset();
        font_.reset();
        return false;
    }
    font_->saveCache(cache.c_str());
    renderer_->initMsdf(*font_);
    font_->releaseAtlasPixels();
    if (!renderer_->msdfReady()) {
        host_->showErrorMessage("mnrdonw", "Text pipeline failed to start");
        renderer_.reset();
        font_.reset();
        return false;
    }
    return true;
}

bool EditorApp::create(int argc, char** argv) {
    loadRecentList();
    registerLinuxDesktopEntry();
    host_ = make_host();
    if (!host_) return false;
    if (!readLaunchFile(argc, argv)) return false;
    if (!host_->init(this)) {
        host_->showErrorMessage("mnrdonw", "Failed to open a window");
        return false;
    }
    if (!initGpu()) return false;
    host_->startTimer(kCaretTimer, 500);
    host_->showWindow();
    running_ = true;
    stale_ = true;
    return true;
}

void EditorApp::onHostResized() {
    if (renderer_) renderer_->notifyResized();
    stale_ = true;
}

void EditorApp::onHostExposed() { stale_ = true; }

void EditorApp::onHostLayoutInvalidated() { stale_ = true; }

void EditorApp::shutdown() { running_ = false; }

void EditorApp::onTimer(int timerId) {
    if (timerId != kCaretTimer) return;
    if (screen_ != Screen::Write) return;
    caretOn_ = !caretOn_;
    stale_ = true;
}

void EditorApp::pokeCaret() {
    caretOn_ = true;
    if (host_) host_->startTimer(kCaretTimer, 500);
    stale_ = true;
}

void EditorApp::clampScroll() {
    const float maxScroll = std::max(0.f, page_.height - geom_.viewH);
    if (scroll_ < 0.f) scroll_ = 0.f;
    if (scroll_ > maxScroll) scroll_ = maxScroll;
}

void EditorApp::ensureCaretVisible() {
    if (page_.rows.empty()) return;
    const Row& row = page_.rows[rowIndexFor(page_, doc_.cursor(), endPin_, endPinByte_)];
    if (row.y < scroll_) scroll_ = row.y;
    else if (row.y + row.h > scroll_ + geom_.viewH) scroll_ = row.y + row.h - geom_.viewH;
    clampScroll();
}

void EditorApp::layoutFrame() {
    const float shortSide = std::min(canvas_->w(), canvas_->h());
    geom_.ui = computeUiMetrics(shortSide);
    geom_.body = geom_.ui.text.body;
    geom_.nameSize = geom_.ui.text.secondary;
    geom_.lineH = measurer_->lineHeight(geom_.body);
    geom_.margin = pageMargin(geom_.ui.scale, canvas_->w(), canvas_->h());
    geom_.contentX = canvas_->left() + geom_.margin;
    geom_.contentY = canvas_->top() + geom_.margin;
    geom_.contentW = std::max(0.f, canvas_->w() - 2.f * geom_.margin);

    geom_.lm.body = geom_.body;
    geom_.lm.h3 = geom_.body * kUiTypeRatio;
    geom_.lm.h2 = geom_.lm.h3 * kUiTypeRatio;
    geom_.lm.h1 = geom_.lm.h2 * kUiTypeRatio;
    geom_.lm.blockGap = geom_.ui.space(16.f);
    geom_.lm.listGap = geom_.ui.space(4.f);
    geom_.lm.gutter = 2.f * geom_.body;
    float wrapW = geom_.contentW - geom_.lm.gutter;
    if (wrapW < 1.f) wrapW = std::max(1.f, geom_.contentW);
    geom_.lm.columnW = wrapW;
    geom_.columnW = wrapW + geom_.lm.gutter;
    geom_.columnX = geom_.contentX;

    const float nameH = measurer_->lineHeight(geom_.nameSize);
    float y = geom_.contentY;
    y += nameH;
    if (!saveError_.empty() || discardArmed_) y += nameH;
    y += geom_.ui.space(8.f);
    geom_.columnTop = y;
    geom_.viewH = std::max(1.f, (canvas_->bottom() - geom_.margin) - geom_.columnTop);
}

void EditorApp::layoutNow() {
    layoutFrame();

    blocks_ = parseBlocks(doc_.text());
    page_ = layoutPage(doc_.text(), blocks_, doc_.cursor(), geom_.lm, *measurer_);

    clampScroll();
    if (!preferredValid_ && !page_.rows.empty()) {
        preferredX_ = caretX(page_, doc_.cursor(), *measurer_);
        preferredValid_ = true;
    }
}

void EditorApp::placeCaret(size_t pos, bool extend, bool vertical, bool keepEndPin) {
    if (!keepEndPin) endPin_ = false;
    if (extend) doc_.setSelection(doc_.anchor(), pos);
    else doc_.setSelection(pos, pos);
    layoutNow();
    if (!vertical && !page_.rows.empty()) {
        if (endPin_ && doc_.cursor() == endPinByte_) {
            const size_t idx = rowIndexFor(page_, doc_.cursor(), true, endPinByte_);
            preferredX_ = xInRow(page_.rows[idx], doc_.cursor(), *measurer_);
        } else {
            preferredX_ = caretX(page_, doc_.cursor(), *measurer_);
        }
        preferredValid_ = true;
    }
    ensureCaretVisible();
    pokeCaret();
}

int EditorApp::run() {
    while (running_) {
        beginFrame();
        const bool canDraw = renderer_ && renderer_->width() > 0 && renderer_->height() > 0;
        // haveWork is true only when a frame is already pending and drawable.
        // A zero-size surface must not spin the pump.
        host_->pump(stale_ && canDraw);
        if (host_->quitRequested() || !running_) break;
        if (!renderer_ || renderer_->width() == 0 || renderer_->height() == 0) continue;
        if (!inputArrived() && !stale_) continue;

        curves_.clear();
        quads_.clear();
        shapes_.clear();
        const SafeInsets insets = host_->safeInsets();
        Canvas canvas(curves_, renderer_->width(), renderer_->height(), nullptr,
                      static_cast<float>(insets.top), static_cast<float>(insets.bottom),
                      static_cast<float>(insets.left), static_cast<float>(insets.right));
        canvas.useMsdf(font_.get(), &quads_);
        canvas.useShapes(&shapes_);
        CanvasMeasurer measurer;
        measurer.canvas = &canvas;
        measurer.font = font_.get();
        canvas_ = &canvas;
        measurer_ = &measurer;

        if (inputArrived()) handleInput();
        if (stale_) draw();
        canvas_ = nullptr;
        measurer_ = nullptr;
    }
    return 0;
}

EditorApp::HomeHit EditorApp::layoutHome() {
    layoutFrame();
    HomeHit hit;
    const float newW = canvas_->textWidthStyled("New", geom_.body, FontStyle::Roman);
    hit.newX = geom_.contentX;
    hit.newW = newW;
    hit.showBrowse = !browsing_;
    if (hit.showBrowse) {
        hit.browseW = canvas_->textWidthStyled("Browse", geom_.body, FontStyle::Roman);
        hit.browseX = geom_.contentX + newW + geom_.ui.space(32.f);
    }
    float y = geom_.contentY + geom_.lineH;
    if (!homeError_.empty()) y += measurer_->lineHeight(geom_.nameSize);
    y += geom_.ui.space(8.f);
    hit.flowTop = y;
    hit.flowView = std::max(0.f, (canvas_->bottom() - geom_.margin) - y);
    hit.rowH = geom_.body / 0.42f;

    if (!browsing_) {
        std::vector<float> widths;
        widths.reserve(recent_.size());
        for (const std::string& path : recent_) {
            float w = canvas_->textWidthStyled(fileTitle(path), geom_.body, FontStyle::Roman);
            if (w > geom_.contentW) w = geom_.contentW;
            widths.push_back(w);
        }
        homeSlots_ = packFlow(geom_.contentW, geom_.ui.space(32.f), geom_.lineH,
                              geom_.ui.space(16.f), widths);
    }
    return hit;
}

void EditorApp::clampHomeScroll(const HomeHit& hit, float contentHeight) {
    const float maxScroll = std::max(0.f, contentHeight - hit.flowView);
    if (homeScroll_ < 0.f) homeScroll_ = 0.f;
    if (homeScroll_ > maxScroll) homeScroll_ = maxScroll;
}

void EditorApp::handleEscape() {
    if (!doc_.dirty()) {
        goHome();
        return;
    }
    if (!discardArmed_) {
        discardArmed_ = true;
        stale_ = true;
        return;
    }
    doc_.load("");
    path_.clear();
    saveError_.clear();
    discardArmed_ = false;
    goHome();
}

void EditorApp::saveNow() {
    std::string dest = path_;
    if (dest.empty()) {
        const std::string dir = arc::fs::userDocuments();
        if (dir.empty()) {
            saveError_ = "no home directory";
            stale_ = true;
            return;
        }
        dest = arc::fs::join(dir, "untitled.md");
        struct stat st;
        if (stat(dest.c_str(), &st) == 0) {
            saveError_ = "untitled.md already exists";
            stale_ = true;
            return;
        }
    }
    const SaveResult saved = saveBuffer(doc_.text(), dest);
    if (saved.ok) {
        doc_.noteSaved();
        path_ = saved.path;
        saveError_.clear();
        discardArmed_ = false;
        touchRecent(path_);
    } else {
        saveError_ = saved.error;
    }
    layoutNow();
    ensureCaretVisible();
    stale_ = true;
}

void EditorApp::handleHomeInput() {
    FrameInput& in = input();
    const HomeHit hit = layoutHome();
    float contentHeight = 0.f;
    if (browsing_) contentHeight = static_cast<float>(browseRows_.size()) * hit.rowH;
    else if (!homeSlots_.empty()) {
        const FlowSlot& last = homeSlots_.back();
        contentHeight = last.y + last.h;
    }
    clampHomeScroll(hit, contentHeight);

    auto inside = [](float x, float y, float rx, float ry, float rw, float rh) {
        return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
    };

    if (in.pointerWentDown) {
        homeError_.clear();
        const float x = in.pointerX;
        const float y = in.pointerY;
        if (inside(x, y, hit.newX, geom_.contentY, hit.newW, geom_.lineH)) {
            openNew();
            return;
        }
        if (hit.showBrowse &&
            inside(x, y, hit.browseX, geom_.contentY, hit.browseW, geom_.lineH)) {
            browsing_ = true;
            homeScroll_ = 0.f;
            if (browseDir_.empty()) browseDir_ = arc::fs::userDocuments();
            reloadBrowse();
            stale_ = true;
            return;
        }
        if (!browsing_) {
            for (const FlowSlot& slot : homeSlots_) {
                const float ry = hit.flowTop + slot.y - homeScroll_;
                if (!inside(x, y, geom_.contentX + slot.x, ry, slot.w, slot.h)) continue;
                if (slot.index < 0 || static_cast<size_t>(slot.index) >= recent_.size()) return;
                openFile(recent_[static_cast<size_t>(slot.index)], false);
                return;
            }
        } else {
            for (size_t i = 0; i < browseRows_.size(); ++i) {
                const float ry = hit.flowTop + static_cast<float>(i) * hit.rowH - homeScroll_;
                if (ry + hit.rowH < hit.flowTop || ry >= hit.flowTop + hit.flowView) continue;
                if (!inside(x, y, geom_.contentX, ry, geom_.contentW, hit.rowH)) continue;
                const BrowseRow& row = browseRows_[i];
                if (row.isDir) enterDirectory(row.path);
                else openFile(row.path, false);
                return;
            }
        }
        stale_ = true;
    }

    if (in.wheelDelta != 0.f) {
        const float before = homeScroll_;
        homeScroll_ += in.wheelDelta * 3.f * geom_.lineH;
        clampHomeScroll(hit, contentHeight);
        if (homeScroll_ != before) stale_ = true;
    }

    for (int key : in.keysWentDown) {
        if (key != key::Escape) continue;
        if (browsing_ && !recent_.empty()) {
            browsing_ = false;
            homeError_.clear();
            homeScroll_ = 0.f;
            stale_ = true;
        }
    }
}

void EditorApp::handleInput() {
    layoutNow();
    FrameInput& in = input();
    if (screen_ == Screen::Home) {
        host_->setCursor(CursorShape::Arrow);
        handleHomeInput();
        return;
    }

    const bool overColumn = in.pointerX >= geom_.columnX &&
                            in.pointerX < geom_.columnX + geom_.columnW &&
                            in.pointerY >= geom_.columnTop &&
                            in.pointerY < geom_.columnTop + geom_.viewH;
    host_->setCursor(overColumn ? CursorShape::Text : CursorShape::Arrow);

    auto refreshCaret = [this]() {
        endPin_ = false;
        layoutNow();
        if (!page_.rows.empty()) {
            preferredX_ = caretX(page_, doc_.cursor(), *measurer_);
            preferredValid_ = true;
        }
        ensureCaretVisible();
        pokeCaret();
    };

    if (in.textEdited) {
        discardArmed_ = false;
        doc_.replaceAll(in.editedText, in.editedCursorByte);
        refreshCaret();
        return;
    }

    auto hitAtPointer = [this, &in]() {
        return hitTest(page_, in.pointerX - geom_.columnX,
                       in.pointerY - geom_.columnTop + scroll_, *measurer_);
    };

    if (in.pointerWentDown) {
        if (overColumn && !page_.rows.empty()) {
            const size_t hit = hitAtPointer();
            const double t = nowSeconds();
            if (t - lastClickSec_ <= kDoubleClickSec) {
                const std::pair<size_t, size_t> word = wordAt(doc_.text(), hit);
                doc_.setSelection(word.first, word.second);
                dragging_ = false;
                refreshCaret();
            } else {
                placeCaret(hit, false, false);
                dragging_ = true;
            }
            lastClickSec_ = t;
        } else {
            dragging_ = false;
        }
    } else if (dragging_ && in.pointerDown && !page_.rows.empty()) {
        const size_t hit = hitAtPointer();
        if (hit != doc_.cursor()) {
            endPin_ = false;
            doc_.setSelection(doc_.anchor(), hit);
            layoutNow();
            if (!page_.rows.empty()) {
                preferredX_ = caretX(page_, doc_.cursor(), *measurer_);
                preferredValid_ = true;
            }
            ensureCaretVisible();
            pokeCaret();
        }
    }
    if (in.pointerWentUp) dragging_ = false;

    if (in.wheelDelta != 0.f) {
        const float before = scroll_;
        scroll_ += in.wheelDelta * 3.f * geom_.lineH;
        clampScroll();
        if (scroll_ != before) stale_ = true;
    }

    bool mutated = false;
    // A tap's key-up lands in the same pump as the key-down. ctrlDown is the
    // state after that pump, so a Ctrl chord is also Control's edge this frame.
    bool ctrl = in.ctrlDown;
    for (int key : in.keysWentDown) {
        if (key == key::Control) {
            ctrl = true;
            continue;
        }
        if (key == key::Shift || key == key::Alt || key == 0) continue;
        const bool extend = in.shiftDown;
        if (ctrl) {
            if (key == key::V) {
                doc_.insertText(host_->getClipboardText());
                mutated = true;
            } else if (key == 'S') {
                saveNow();
            } else if (key == 'Z') {
                if (doc_.undo()) mutated = true;
            } else if (key == 'Y') {
                if (doc_.redo()) mutated = true;
            } else if (key == key::Left) {
                placeCaret(prevWord(doc_.text(), doc_.cursor()), extend, false);
            } else if (key == key::Right) {
                placeCaret(nextWord(doc_.text(), doc_.cursor()), extend, false);
            } else if (key == key::Home) {
                placeCaret(0, extend, false);
            } else if (key == key::End) {
                placeCaret(doc_.text().size(), extend, false);
            }
            continue;
        }

        if (key == key::Escape) {
            handleEscape();
            continue;
        }
        if (page_.rows.empty()) continue;
        const size_t cursor = doc_.cursor();
        const size_t rowIndex = rowIndexFor(page_, cursor, endPin_, endPinByte_);
        const Row& row = page_.rows[rowIndex];
        if (key == key::Left) {
            if (!extend && doc_.anchor() != cursor) {
                placeCaret(std::min(doc_.anchor(), cursor), false, false);
            } else {
                placeCaret(utf8Prev(doc_.text(), cursor), extend, false);
            }
        } else if (key == key::Right) {
            if (!extend && doc_.anchor() != cursor) {
                placeCaret(std::max(doc_.anchor(), cursor), false, false);
            } else {
                placeCaret(utf8Next(doc_.text(), cursor), extend, false);
            }
        } else if (key == key::Up || key == key::Down) {
            if (!preferredValid_) {
                if (endPin_ && cursor == endPinByte_) {
                    preferredX_ = xInRow(row, cursor, *measurer_);
                } else {
                    preferredX_ = caretX(page_, cursor, *measurer_);
                }
                preferredValid_ = true;
            }
            const int dir = key == key::Down ? 1 : -1;
            size_t dest = moveVertical(page_, cursor, preferredX_, dir, *measurer_);
            if (endPin_ && cursor == endPinByte_) {
                size_t i = rowIndex;
                if (dir > 0 && i + 1 < page_.rows.size()) ++i;
                else if (dir < 0 && i > 0) --i;
                const Row& destRow = page_.rows[i];
                dest = hitTest(page_, preferredX_, destRow.y + destRow.h * 0.5f, *measurer_);
            }
            placeCaret(dest, extend, true);
        } else if (key == key::Home) {
            placeCaret(row.sourceBegin, extend, false);
        } else if (key == key::End) {
            const size_t endByte = rowEndByte(page_, rowIndex, doc_.text());
            endPin_ = false;
            if (rowIndex + 1 < page_.rows.size() && endByte == row.sourceEnd &&
                endByte == page_.rows[rowIndex + 1].sourceBegin) {
                endPin_ = true;
                endPinByte_ = endByte;
            }
            placeCaret(endByte, extend, false, true);
        } else if (key == key::PageUp || key == key::PageDown) {
            if (!preferredValid_) {
                preferredX_ = caretX(page_, cursor, *measurer_);
                preferredValid_ = true;
            }
            const int dir = key == key::PageDown ? 1 : -1;
            float y = row.y + row.h * 0.5f + static_cast<float>(dir) * geom_.viewH;
            if (y < 0.f) y = 0.f;
            if (y > page_.height) y = page_.height;
            placeCaret(hitTest(page_, preferredX_, y, *measurer_), extend, true);
        } else if (key == key::Enter) {
            doc_.insertText("\n");
            mutated = true;
        } else if (key == key::Tab) {
            doc_.insertText("  ");
            mutated = true;
        } else if (key == key::Backspace) {
            doc_.backspace();
            mutated = true;
        } else if (key == key::Delete) {
            doc_.deleteForward();
            mutated = true;
        }
    }

    if (!ctrl) {
        for (char32_t cp : in.typedCodepoints) {
            doc_.insertCodepoint(cp);
            mutated = true;
        }
    }
    if (mutated) {
        discardArmed_ = false;
        refreshCaret();
    }
}

void EditorApp::drawHome() {
    canvas_->clear(col::bg);
    const HomeHit hit = layoutHome();
    float contentHeight = 0.f;
    if (browsing_) contentHeight = static_cast<float>(browseRows_.size()) * hit.rowH;
    else if (!homeSlots_.empty()) {
        const FlowSlot& last = homeSlots_.back();
        contentHeight = last.y + last.h;
    }
    clampHomeScroll(hit, contentHeight);

    const FrameInput& in = input();
    canvas_->textStyled("New", hit.newX, geom_.contentY, geom_.body, col::dim, FontStyle::Roman);
    if (hit.showBrowse) {
        canvas_->textStyled("Browse", hit.browseX, geom_.contentY, geom_.body, col::dim,
                            FontStyle::Roman);
    }
    if (!homeError_.empty()) {
        const float errorY = geom_.contentY + geom_.lineH;
        canvas_->textStyled(homeError_, geom_.contentX, errorY, geom_.nameSize, col::dim,
                            FontStyle::Roman);
    }

    if (!browsing_) {
        for (const FlowSlot& slot : homeSlots_) {
            if (static_cast<size_t>(slot.index) >= recent_.size()) continue;
            const float ry = hit.flowTop + slot.y - homeScroll_;
            if (ry + slot.h <= hit.flowTop || ry >= hit.flowTop + hit.flowView) continue;
            const std::string name = fileTitle(recent_[static_cast<size_t>(slot.index)]);
            const bool hover = in.pointerX >= geom_.contentX + slot.x &&
                               in.pointerX < geom_.contentX + slot.x + slot.w &&
                               in.pointerY >= ry && in.pointerY < ry + slot.h;
            const std::string shown =
                truncateToWidth(*canvas_, name, slot.w, geom_.body, FontStyle::Roman);
            canvas_->textStyled(shown, geom_.contentX + slot.x, ry, geom_.body,
                                hover ? col::accent : col::text, FontStyle::Roman);
        }
        return;
    }

    std::vector<std::string> labels;
    labels.reserve(browseRows_.size());
    int hover = -1;
    for (size_t i = 0; i < browseRows_.size(); ++i) {
        labels.push_back(browseRows_[i].label);
        const float ry = hit.flowTop + static_cast<float>(i) * hit.rowH - homeScroll_;
        if (in.pointerX >= geom_.contentX && in.pointerX < geom_.contentX + geom_.contentW &&
            in.pointerY >= ry && in.pointerY < ry + hit.rowH && ry + hit.rowH > hit.flowTop &&
            ry < hit.flowTop + hit.flowView) {
            hover = static_cast<int>(i);
        }
    }
    widgets::ScrollListStyle style;
    style.background = col::bg;
    style.hoverBg = col::bg;
    style.hoverText = col::accent;
    style.rowText = col::text;
    const Rect area{geom_.contentX, hit.flowTop, geom_.contentW, hit.flowView};
    widgets::drawScrollList(*canvas_, area, labels, -1, homeScroll_, hit.rowH, hover,
                            widgets::kTextFit, style);
}

void EditorApp::drawWrite() {
    layoutNow();
    canvas_->clear(col::bg);

    std::string title = fileTitle(path_);
    if (doc_.dirty()) title += " *";
    const float nameH = measurer_->lineHeight(geom_.nameSize);
    const float nameY = geom_.contentY;
    canvas_->textStyled(title, geom_.columnX, nameY, geom_.nameSize, col::dim, FontStyle::Roman);
    if (!saveError_.empty()) {
        canvas_->textStyled(saveError_, geom_.columnX, nameY + nameH, geom_.nameSize, col::dim,
                            FontStyle::Roman);
    } else if (discardArmed_) {
        canvas_->textStyled("unsaved", geom_.columnX, nameY + nameH, geom_.nameSize, col::dim,
                            FontStyle::Roman);
    }

    canvas_->setClip(geom_.columnX, geom_.columnTop, geom_.columnW, geom_.viewH);

    if (page_.active < blocks_.size()) {
        const Block& block = blocks_[page_.active];
        float top = 0.f;
        float bot = 0.f;
        bool any = false;
        for (const Row& row : page_.rows) {
            if (!rowInBlock(row, block)) continue;
            const float y0 = geom_.columnTop + (row.y - scroll_);
            if (!any) {
                top = y0;
                bot = y0 + row.h;
                any = true;
            } else {
                top = std::min(top, y0);
                bot = std::max(bot, y0 + row.h);
            }
        }
        if (any && bot > top) {
            canvas_->rect(geom_.columnX, top, geom_.columnW, bot - top, col::panel);
        }
    }

    for (size_t i = 0; i < page_.rows.size();) {
        const Row& row = page_.rows[i];
        if (row.kind != BlockKind::Fence || row.sourceMode) {
            ++i;
            continue;
        }
        float top = geom_.columnTop + (row.y - scroll_);
        float bot = top + row.h;
        size_t j = i + 1;
        while (j < page_.rows.size() && page_.rows[j].kind == BlockKind::Fence &&
               !page_.rows[j].sourceMode) {
            bot = geom_.columnTop + (page_.rows[j].y - scroll_) + page_.rows[j].h;
            ++j;
        }
        canvas_->rect(geom_.columnX, top, geom_.columnW, bot - top, col::panel);
        i = j;
    }

    const size_t caretRow =
        page_.rows.empty() ? 0 : rowIndexFor(page_, doc_.cursor(), endPin_, endPinByte_);
    const size_t selLo = std::min(doc_.anchor(), doc_.cursor());
    const size_t selHi = std::max(doc_.anchor(), doc_.cursor());
    Color selection = col::accent;
    selection.a = 0.35f;
    const float quoteW = geom_.ui.stroke(2.f);
    const float ruleH = geom_.ui.stroke(1.f);
    const float caretW = geom_.ui.stroke(1.f);
    const float codePad = geom_.ui.space(2.f);
    const std::string& text = doc_.text();

    for (size_t i = 0; i < page_.rows.size(); ++i) {
        const Row& row = page_.rows[i];
        const float rowTop = geom_.columnTop + (row.y - scroll_);
        if (rowTop + row.h <= geom_.columnTop) continue;
        if (rowTop >= geom_.columnTop + geom_.viewH) break;

        if (row.kind == BlockKind::Quote && !row.sourceMode) {
            float barX = geom_.columnX + row.textX - geom_.ui.space(8.f) - quoteW;
            if (barX < geom_.columnX) barX = geom_.columnX;
            canvas_->rect(barX, rowTop, quoteW, row.h, col::accent);
        }
        if (row.kind == BlockKind::Rule && !row.sourceMode) {
            canvas_->rect(geom_.columnX, rowTop + (row.h - ruleH) * 0.5f, geom_.columnW, ruleH,
                          col::dim);
        }
        if (!row.sourceMode) {
            float x = row.textX;
            for (const Span& span : row.spans) {
                if (span.role != SpanRole::Code) {
                    size_t d = span.displayBegin;
                    while (d < span.displayEnd) {
                        const size_t dn = utf8Next(row.display, d);
                        x += measurer_->width(std::string_view(row.display).substr(d, dn - d),
                                              row.sizePx, rowFace(row, span.role));
                        d = dn;
                    }
                    continue;
                }
                const float spanX = x;
                size_t d = span.displayBegin;
                while (d < span.displayEnd) {
                    const size_t dn = utf8Next(row.display, d);
                    x += measurer_->width(std::string_view(row.display).substr(d, dn - d),
                                          row.sizePx, rowFace(row, span.role));
                    d = dn;
                }
                const float spanW = x - spanX;
                if (spanW > 0.f) {
                    canvas_->rect(geom_.columnX + spanX - codePad, rowTop, spanW + codePad * 2.f,
                                  row.h, col::track);
                }
            }
        }
        if (rowOverlapsSelection(row, selLo, selHi)) {
            float x0 = geom_.columnX + row.textX;
            float x1 = geom_.columnX + geom_.columnW;
            if (row.sourceBegin != row.sourceEnd) {
                x0 = geom_.columnX +
                     xInRow(row, std::max(selLo, row.sourceBegin), *measurer_);
                x1 = geom_.columnX +
                     xInRow(row, std::min(selHi, row.sourceEnd), *measurer_);
                if (selHi >= row.sourceEnd) x1 = geom_.columnX + geom_.columnW;
            }
            if (x1 > x0) canvas_->rect(x0, rowTop, x1 - x0, row.h, selection);
        }
        if (caretOn_ && i == caretRow) {
            const float cx = geom_.columnX + xInRow(row, doc_.cursor(), *measurer_);
            canvas_->rect(cx, rowTop, caretW, row.h, col::accent);
        }
    }

    for (const Row& row : page_.rows) {
        const float rowTop = geom_.columnTop + (row.y - scroll_);
        if (rowTop + row.h <= geom_.columnTop) continue;
        if (rowTop >= geom_.columnTop + geom_.viewH) break;
        if (row.kind == BlockKind::Rule && !row.sourceMode) continue;

        if (!row.sourceMode && row.kind == BlockKind::ListItem &&
            row.contentBegin > row.sourceBegin) {
            const std::string marker =
                trimMarker(text.substr(row.sourceBegin, row.contentBegin - row.sourceBegin));
            if (!marker.empty()) {
                const float markerW =
                    canvas_->textWidthStyled(marker, row.sizePx, FontStyle::Roman);
                const float right = geom_.columnX + row.textX - geom_.ui.space(4.f);
                canvas_->textStyled(marker, right - markerW, rowTop, row.sizePx, col::text,
                                    FontStyle::Roman);
            }
        }

        float x = geom_.columnX + row.textX;
        for (const Span& span : row.spans) {
            const Face face = rowFace(row, span.role);
            size_t d = span.displayBegin;
            while (d < span.displayEnd) {
                const size_t dn = utf8Next(row.display, d);
                const std::string_view glyph =
                    std::string_view(row.display).substr(d, dn - d);
                canvas_->textStyled(glyph, x, rowTop, row.sizePx, col::text, toStyle(face));
                x += measurer_->width(glyph, row.sizePx, face);
                d = dn;
            }
        }
    }

    canvas_->clearClip();
}

void EditorApp::draw() {
    if (screen_ == Screen::Home) drawHome();
    else drawWrite();
    renderer_->draw(curves_, 0, {}, {}, quads_, shapes_);
    stale_ = false;
}
