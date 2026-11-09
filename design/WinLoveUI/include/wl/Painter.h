// WinLove UI — Direct2D / DirectWrite çizim yardımcıları
// Bağımlılık: yalnızca d2d1.lib, dwrite.lib (Windows SDK). Pencere, olay ve
// yerleşim yönetimi senin projende kalır; bu sınıf sadece "nasıl görünür"ü çizer.
#pragma once
#include <d2d1_1.h>
#include <dwrite_1.h>
#include <wrl/client.h>
#include <string_view>
#include "Theme.h"

namespace wl {

using Microsoft::WRL::ComPtr;

enum class TextStyle {
    Title,        // 28, Bold, -0.025em   — görüntü adı
    Heading,      // 18, Bold, -0.015em   — panel başlığı
    Panel,        // 15, SemiBold         — "Değişiklikler"
    Body,         // 14, Regular
    BodyMedium,   // 14, Medium           — liste satırı adı, sekme
    BodySemi,     // 14, SemiBold         — seçili sekme, düğme
    Small,        // 13, Regular          — açıklamalar
    SmallSemi,    // 13, SemiBold         — segment seçili, pill seçili
    Caption,      // 12.5, Regular        — etiketler, durum çubuğu
    Count,        // 12, Medium           — sekme sayacı
    Badge,        // 12, SemiBold         — "Bağlı" rozeti
    GroupCap,     // 11, SemiBold, +0.06em — BAŞLAT / YÜKLE ... (büyük harfi çağıran verir)
    SectionCap,   // 12, SemiBold, +0.04em — KAYNAK / UYGULAMALAR ...
    Mono,         // 12.5, Cascadia Mono
    _Count
};

enum class HAlign { Leading, Center, Trailing };

enum class Icon {
    Search, ChevronRight, ChevronDown, Check, Close, Plus, Minus,
    Reset, Sun, Moon, ArrowRight, Minimize, Maximize, Warning
};

enum class ButtonKind { Primary, Secondary, Ghost };

struct State {
    bool hover = false;
    bool pressed = false;
    bool disabled = false;
    bool focus = false;   // klavye odağı: acSoft halka
};

class Painter {
public:
    // Bir kez: fabrikaları ver, metin biçimleri oluşturulur.
    HRESULT Init(ID2D1Factory1* d2d, IDWriteFactory* dwrite);

    // Her karede: hedef ve tema. Tema değişince fırçalar güncellenir.
    void Begin(ID2D1DeviceContext* dc, const Theme& theme);
    const Theme& T() const { return *theme_; }

    // --- temel ---------------------------------------------------------
    void Fill(const D2D1_RECT_F& r, const D2D1_COLOR_F& c, float radius = 0);
    void Stroke(const D2D1_RECT_F& r, const D2D1_COLOR_F& c, float radius = 0, float width = 1);
    void HLine(float x0, float x1, float y, const D2D1_COLOR_F& c, float width = 1);
    void VLine(float x, float y0, float y1, const D2D1_COLOR_F& c, float width = 1);
    void Dot(float cx, float cy, float size, const D2D1_COLOR_F& c);

    // Tek satır metin; sığmazsa "…" ile kısaltılır. Dikeyde ortalanır.
    void Text(std::wstring_view s, const D2D1_RECT_F& r, TextStyle st,
              const D2D1_COLOR_F& c, HAlign h = HAlign::Leading);
    // Çok satırlı metin (açıklamalar). Dönüş: kullanılan yükseklik.
    float Paragraph(std::wstring_view s, const D2D1_RECT_F& r, TextStyle st,
                    const D2D1_COLOR_F& c, float lineHeight = 0);
    float Measure(std::wstring_view s, TextStyle st);

    void DrawIcon(Icon ic, const D2D1_RECT_F& r, const D2D1_COLOR_F& c, float stroke = 1.8f);

    // --- bileşenler ----------------------------------------------------
    void Button(const D2D1_RECT_F& r, std::wstring_view label, ButtonKind k, State s = {});
    void MenuButton(const D2D1_RECT_F& r, std::wstring_view label, bool active, State s = {});
    void CaptionButton(const D2D1_RECT_F& r, Icon ic, bool isClose, State s = {});

    // Sekme: ad + isteğe bağlı sayaç (count < 0 ise gösterilmez). Genişliği döndürür.
    float TabWidth(std::wstring_view label, int count);
    void Tab(const D2D1_RECT_F& r, std::wstring_view label, int count, bool active, State s = {});
    void GroupCaption(const D2D1_RECT_F& r, std::wstring_view upperText);

    void SearchBox(const D2D1_RECT_F& r, std::wstring_view placeholder,
                   std::wstring_view text, std::wstring_view shortcut, State s = {});
    void Pill(const D2D1_RECT_F& r, std::wstring_view label, bool on, State s = {});

    void Checkbox(const D2D1_RECT_F& r, bool checked, bool mixed = false, State s = {});
    // anim: 0 = kapalı, 1 = açık (geçiş için ara değer verebilirsin)
    void Switch(const D2D1_RECT_F& r, float anim, State s = {});
    // Segmentli seçim. hoverIndex = -1 yoksa.
    void Segmented(const D2D1_RECT_F& r, const std::wstring_view* labels, int n,
                   int selected, int hoverIndex = -1);
    void Progress(const D2D1_RECT_F& r, float value01);

    // Rozet: "Bağlı", "Yükleniyor 42%". Genişliği döndürür.
    float Badge(float x, float y, std::wstring_view s, const D2D1_COLOR_F& bg, const D2D1_COLOR_F& fg);
    // "● Dikkat" gibi nokta + metin. Genişliği döndürür.
    float StatusText(float x, const D2D1_RECT_F& row, std::wstring_view s,
                     const D2D1_COLOR_F& dot, const D2D1_COLOR_F& fg);

    // Liste satırı zemini: hover / seçili (acSoft + 2px mavi şerit) + alt ayraç.
    void RowBackground(const D2D1_RECT_F& r, bool selected, bool hover, bool bottomLine = true);

    IDWriteTextFormat* Format(TextStyle st) const { return fmt_[(int)st].Get(); }

private:
    struct StyleDef { const wchar_t* family; float size; DWRITE_FONT_WEIGHT weight; float trackingEm; };
    static const StyleDef kStyles[(int)TextStyle::_Count];

    ID2D1SolidColorBrush* Br(const D2D1_COLOR_F& c);
    HRESULT MakeLayout(std::wstring_view s, TextStyle st, float w, float h, IDWriteTextLayout** out);

    ComPtr<ID2D1Factory1> d2d_;
    ComPtr<IDWriteFactory> dw_;
    ComPtr<IDWriteTextFormat> fmt_[(int)TextStyle::_Count];
    ComPtr<IDWriteInlineObject> ellipsis_[(int)TextStyle::_Count];
    ID2D1DeviceContext* dc_ = nullptr;
    ComPtr<ID2D1SolidColorBrush> brush_;
    ComPtr<ID2D1StrokeStyle1> round_;
    const Theme* theme_ = &LightTheme();
};

} // namespace wl
