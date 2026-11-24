#include "ui/widgets/ColorPicker.h"

#include "ui/widget/Host.h"
#include "ui/widgets/SearchBox.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cwctype>
#include <utility>

namespace wl::ui {

namespace {

using tokens::Color;
using tokens::TypeStyle;

constexpr float kMarker = 6.0f;          // the field's marker: a ring this wide around the colour
constexpr float kHueMarker = 7.0f;       // the hue bar's knob
constexpr float kHueGrab = 4.0f;         // the bar takes the pointer this far above and below it
constexpr float kFieldStep = 0.01f;      // arrow keys: one step of saturation / brightness
constexpr float kHueStep = 1.0f;         // and of hue (degrees); Shift: ten steps
constexpr int kHueStops = 7;             // red, yellow, green, cyan, blue, magenta, red

float noteHeight() noexcept {
    return 2 * tokens::kTypeStyles[static_cast<std::size_t>(TypeStyle::Caption)].lineHeight;
}

// The marker on the field / the knob on the bar: the colour inside, a white ring, a soft dark edge so it
// shows on white as on black.
void paintMarker(Canvas& canvas, PointF center, float radius, Rgba inside) {
    canvas.fillEllipse(center, radius, inside);
    canvas.strokeCircle(center, radius + 1.0f, Color::ShadowKey, 1.0f);
    canvas.strokeCircle(center, radius, Color::TextOnAccent, 2.0f);
}

} // namespace

// ---- parts ------------------------------------------------------------------------------------------

class ColorPicker::Field : public Widget {
public:
    Field(ColorPicker& owner, const std::wstring& name) : m_owner(owner) {
        setFocusable(true);
        setAccessible(AccessRole::Group, name);
    }
    [[nodiscard]] Cursor cursor() const override { return Cursor::Hand; }
    [[nodiscard]] float focusRadius() const override { return tokens::radius::r2; }
    void paint(Canvas& canvas) override {
        const RectF r = bounds();
        const Hsv hsv = m_owner.hsv();
        // The hue at full strength, white over it from the left, black over that from the bottom.
        canvas.fillRoundRect(r, tokens::radius::r2, rgbColor(fromHsv({hsv.h, 1, 1})));
        Rgba white = rgbColor(fromHsv({hsv.h, 0, 1}));
        Rgba clearWhite = white;
        clearWhite.a = 0;
        Rgba black = rgbColor(fromHsv({hsv.h, 0, 0}));
        Rgba clearBlack = black;
        clearBlack.a = 0;
        const std::array<Rgba, 2> across{white, clearWhite};
        const std::array<Rgba, 2> down{clearBlack, black};
        canvas.fillLinearGradient(r, tokens::radius::r2, across, /*vertical=*/false);
        canvas.fillLinearGradient(r, tokens::radius::r2, down, /*vertical=*/true);
        canvas.strokeRoundRect(r, tokens::radius::r2, Color::LineSubtle);
        const PointF at{r.x + hsv.s * r.width, r.y + (1 - hsv.v) * r.height};
        paintMarker(canvas, at, kMarker, rgbColor(m_owner.color()));
    }
    void onPointerDown(PointF p) override { pick(p, false); }
    void onPointerMove(PointF p) override {
        if (captured()) {
            pick(p, false);
        }
    }
    void onPointerUp(PointF p) override {
        if (p.x >= 0) { // (-1, -1): the press was cancelled (focus lost) — the preview stays as it is
            pick(p, true);
        }
    }
    bool onKeyDown(const KeyEvent& key) override {
        const float step = key.shift ? 10 * kFieldStep : kFieldStep;
        Hsv hsv = m_owner.hsv();
        switch (key.virtualKey) {
        case VK_LEFT: hsv.s -= step; break;
        case VK_RIGHT: hsv.s += step; break;
        case VK_UP: hsv.v += step; break;
        case VK_DOWN: hsv.v -= step; break;
        default: return false;
        }
        hsv.s = std::clamp(hsv.s, 0.0f, 1.0f);
        hsv.v = std::clamp(hsv.v, 0.0f, 1.0f);
        m_owner.change(hsv, true);
        return true;
    }

private:
    void pick(PointF p, bool commit) {
        const RectF r = bounds();
        Hsv hsv = m_owner.hsv();
        hsv.s = std::clamp((p.x - r.x) / std::max(r.width, 1.0f), 0.0f, 1.0f);
        hsv.v = std::clamp(1 - (p.y - r.y) / std::max(r.height, 1.0f), 0.0f, 1.0f);
        m_owner.change(hsv, commit);
    }
    ColorPicker& m_owner;
};

class ColorPicker::HueBar : public Widget {
public:
    HueBar(ColorPicker& owner, const std::wstring& name) : m_owner(owner) {
        setFocusable(true);
        setAccessible(AccessRole::Group, name);
    }
    [[nodiscard]] Cursor cursor() const override { return Cursor::Hand; }
    [[nodiscard]] RectF focusRect() const override { return bar(); }
    [[nodiscard]] float focusRadius() const override { return kHueHeight / 2; }
    void paint(Canvas& canvas) override {
        const RectF r = bar();
        std::array<Rgba, kHueStops> stops{};
        for (int i = 0; i < kHueStops; ++i) {
            stops[static_cast<std::size_t>(i)] = rgbColor(fromHsv({60.0f * static_cast<float>(i), 1, 1}));
        }
        canvas.fillLinearGradient(r, r.height / 2, stops, /*vertical=*/false);
        const float h = m_owner.hsv().h;
        const PointF at{r.x + std::clamp(h / 360.0f, 0.0f, 1.0f) * r.width, r.y + r.height / 2};
        paintMarker(canvas, at, kHueMarker, rgbColor(fromHsv({h, 1, 1})));
    }
    void onPointerDown(PointF p) override { pick(p, false); }
    void onPointerMove(PointF p) override {
        if (captured()) {
            pick(p, false);
        }
    }
    void onPointerUp(PointF p) override {
        if (p.x >= 0) {
            pick(p, true);
        }
    }
    bool onKeyDown(const KeyEvent& key) override {
        const float step = key.shift ? 10 * kHueStep : kHueStep;
        Hsv hsv = m_owner.hsv();
        switch (key.virtualKey) {
        case VK_LEFT:
        case VK_DOWN: hsv.h = std::fmod(hsv.h - step + 360.0f, 360.0f); break;
        case VK_RIGHT:
        case VK_UP: hsv.h = std::fmod(hsv.h + step, 360.0f); break;
        default: return false;
        }
        m_owner.change(hsv, true);
        return true;
    }

private:
    [[nodiscard]] RectF bar() const {
        const RectF b = bounds();
        return {b.x, b.y + kHueGrab, b.width, kHueHeight};
    }
    void pick(PointF p, bool commit) {
        const RectF r = bar();
        Hsv hsv = m_owner.hsv();
        // 360 is red again: the right end keeps 359.9 so the knob stays where it was let go.
        hsv.h = std::clamp((p.x - r.x) / std::max(r.width, 1.0f), 0.0f, 0.9997f) * 360.0f;
        if (hsv.s <= 0 && hsv.v <= 0) {
            hsv.s = hsv.v = 1; // black has no hue to show: picking one means the colour
        } else if (hsv.s <= 0) {
            hsv.s = 1;
        }
        m_owner.change(hsv, commit);
    }
    ColorPicker& m_owner;
};

// The #RRGGBB box: Esc is the popup's (back to the colour it opened with), not "clear the text".
class ColorPicker::HexBox : public SearchBox {
public:
    HexBox(ColorPicker& owner, std::wstring placeholder) : SearchBox(std::move(placeholder)), m_owner(owner) {
        setPlain(true);
        setMaxLength(7);
        onChange = [this](const std::wstring& text) { m_owner.typed(text, false); };
        onSubmit = [this] { m_owner.typed(this->text(), true); };
    }
    bool onKeyDown(const KeyEvent& key) override {
        if (key.virtualKey == VK_ESCAPE) {
            return false;
        }
        return SearchBox::onKeyDown(key);
    }

private:
    ColorPicker& m_owner;
};

// ---- ColorPicker ------------------------------------------------------------------------------------

ColorPicker::ColorPicker(Texts texts) : m_texts(std::move(texts)) {
    setAccessible(AccessRole::Group, m_texts.field);
    m_field = &add<Field>(*this, m_texts.field);
    m_hue = &add<HueBar>(*this, m_texts.hue);
    m_box = &add<HexBox>(*this, m_texts.hex);
    m_box->setAccessible(AccessRole::Edit, m_texts.hex);
    setColor(kDefaultAccent);
}

void ColorPicker::setColor(std::uint32_t rgb) {
    rgb &= 0xFFFFFFu;
    const Hsv hsv = toHsv(rgb);
    m_hsv.s = hsv.s;
    m_hsv.v = hsv.v;
    if (hsv.s > 0 && hsv.v > 0) {
        m_hsv.h = hsv.h; // a grey keeps the hue it was on
    }
    m_rgb = rgb;
    refreshDerived();
    m_box->setText(hexColor(rgb));
    invalidate();
}

void ColorPicker::change(Hsv hsv, bool commit) {
    m_hsv = hsv;
    m_rgb = fromHsv(hsv);
    refreshDerived();
    if (!m_box->focused()) {
        m_box->setText(hexColor(m_rgb));
    }
    invalidate();
    const auto callback = commit ? onCommit : onPreview; // a copy: it may close the popup
    if (callback) {
        callback(m_rgb);
    }
}

void ColorPicker::typed(const std::wstring& text, bool commit) {
    // While typing only a whole #RRGGBB previews ("#0F7" on the way to "#0F766E" is not #00FF77); Enter
    // takes the short form too.
    std::size_t digits = 0;
    for (const wchar_t ch : text) {
        digits += std::iswxdigit(ch) ? 1 : 0;
    }
    const auto rgb = commit || digits == 6 ? parseHexColor(text) : std::nullopt;
    if (!rgb) {
        if (commit) {
            m_box->setText(hexColor(m_rgb)); // Enter on half a colour: back to the one shown
        }
        return;
    }
    const std::wstring shown = m_box->text();
    setColor(*rgb);
    if (!commit) {
        m_box->setText(shown); // keep what is being typed ("#7a3" is not rewritten to "#77AA33" mid-word)
    }
    const auto callback = commit ? onCommit : onPreview;
    if (callback) {
        callback(m_rgb);
    }
}

void ColorPicker::refreshDerived() {
    m_derived = accentTokens(m_rgb);
}

const std::wstring& ColorPicker::note(ThemeKind theme) const noexcept {
    static const std::wstring kNone;
    if (theme == ThemeKind::HighContrast) {
        return kNone; // high contrast keeps its own colours
    }
    if (theme != ThemeKind::Light && m_derived.lightened) {
        return m_texts.lightened;
    }
    return m_derived.darkened ? m_texts.darkened : kNone;
}

Widget& ColorPicker::firstFocus() noexcept {
    return *m_field;
}

SizeF ColorPicker::measure(SizeF /*available*/) {
    return {kWidth, kFieldHeight + tokens::spacing::s5 + kHueHeight + tokens::spacing::s5 + tokens::size::control +
                        tokens::spacing::s4 + noteHeight()};
}

void ColorPicker::layout() {
    const RectF b = bounds();
    m_field->setBounds({b.x, b.y, kWidth, kFieldHeight});
    float y = b.y + kFieldHeight + tokens::spacing::s5;
    m_hue->setBounds({b.x, y - kHueGrab, kWidth, kHueHeight + 2 * kHueGrab});
    y += kHueHeight + tokens::spacing::s5;
    m_swatch = {b.x, y, tokens::size::control, tokens::size::control};
    m_box->setBounds({m_swatch.right() + tokens::spacing::s4, y, kHexWidth, tokens::size::control});
    y += tokens::size::control + tokens::spacing::s4;
    m_note = {b.x, y, kWidth, noteHeight()};
}

void ColorPicker::paint(Canvas& canvas) {
    // The fill the colour gives in this theme, with white text on it — what a primary button will be.
    const ThemeKind theme = canvas.theme();
    if (theme == ThemeKind::HighContrast) {
        canvas.fillRoundRect(m_swatch, tokens::radius::r2, Color::AccentBase);
        canvas.drawText(m_texts.sample, m_swatch, TypeStyle::BodyStrong, Color::TextOnAccent, TextAlign::Center);
    } else {
        const AccentSet& set = theme == ThemeKind::Light ? m_derived.light : theme == ThemeKind::Black ? m_derived.black : m_derived.dark;
        canvas.fillRoundRect(m_swatch, tokens::radius::r2, toRgba(set.base));
        canvas.drawText(m_texts.sample, m_swatch, TypeStyle::BodyStrong, toRgba(set.onAccent), TextAlign::Center);
    }
    if (const std::wstring& text = note(theme); !text.empty()) {
        canvas.drawTextWrapped(text, m_note, TypeStyle::Caption, Color::TextSecondary);
    }
}

// ---- ColorPickerPopup -------------------------------------------------------------------------------

ColorPickerPopup::ColorPickerPopup(RectF anchor, ColorPicker::Texts texts, std::uint32_t rgb) : m_anchor(anchor) {
    // It holds the focus when it opens (no ring on a part opened by a click); the arrows go to the field,
    // Tab into the parts.
    setFocusable(true);
    setAccessible(AccessRole::Group, texts.field);
    m_picker = &add<ColorPicker>(std::move(texts));
    m_picker->setColor(rgb);
    m_picker->onPreview = [this](std::uint32_t c) {
        if (const auto preview = onPreview) {
            preview(c);
        }
    };
    m_picker->onCommit = [this](std::uint32_t c) {
        if (const auto commit = onCommit) {
            commit(c);
        }
    };
}

// Torn down by the host without close() (the window closes with it open, a render ends): the owner still
// holds a pointer to it and must hear that it is gone.
ColorPickerPopup::~ColorPickerPopup() {
    if (auto closed = std::exchange(onClosed, nullptr)) {
        closed();
    }
}

void ColorPickerPopup::detach() noexcept {
    onPreview = nullptr;
    onCommit = nullptr;
    onCancel = nullptr;
    onClosed = nullptr;
}

void ColorPickerPopup::close() {
    auto closed = std::exchange(onClosed, nullptr); // once: the destructor runs it otherwise
    if (Host* h = host()) {
        h->popModal(this); // destroys this
    }
    if (closed) {
        closed();
    }
}

void ColorPickerPopup::layout() {
    const RectF window = bounds();
    const SizeF size = m_picker->measure({});
    const float pad = tokens::size::panelPad;
    const float width = size.width + 2 * pad;
    const float height = size.height + 2 * pad;
    const float x = std::clamp(m_anchor.x, window.x + 4, std::max(window.x + 4, window.right() - width - 4));
    float y = m_anchor.bottom() + tokens::spacing::s2;
    if (y + height > window.bottom() - 4) {
        y = m_anchor.y - tokens::spacing::s2 - height; // above when it does not fit below
    }
    m_panel = {std::round(x), std::round(std::max(y, window.y + 4)), width, height};
    m_picker->setBounds({m_panel.x + pad, m_panel.y + pad, size.width, size.height});
}

void ColorPickerPopup::paint(Canvas& canvas) {
    canvas.panel(m_panel, tokens::radius::menu, tokens::elevation::menu);
}

Widget* ColorPickerPopup::hitTest(PointF p) {
    return Widget::hitTest(p); // the picker's parts first; anywhere else is this (a click outside closes it)
}

void ColorPickerPopup::onPointerDown(PointF p) {
    if (!m_panel.contains(p)) {
        close();
    }
}

bool ColorPickerPopup::onKeyDown(const KeyEvent& key) {
    if (key.virtualKey == VK_ESCAPE) {
        if (const auto cancel = onCancel) {
            cancel();
        }
        close();
        return true;
    }
    if (host() && host()->focused() == this) {
        const UINT vk = key.virtualKey;
        if (vk == VK_LEFT || vk == VK_RIGHT || vk == VK_UP || vk == VK_DOWN) {
            return m_picker->firstFocus().onKeyDown(key);
        }
    }
    return false;
}

bool ColorPickerPopup::onContextMenu(PointF p) {
    if (!m_panel.contains(p)) {
        close();
    }
    return true;
}

} // namespace wl::ui
