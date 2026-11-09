// WinLove UI — tema renkleri ve ölçüler
// Tüm ölçüler DIP (device independent pixel) cinsindendir. Direct2D hedefi
// DPI'a göre ölçeklendiğinde (SetDpi) bu değerler her ekranda keskin kalır.
#pragma once
#include <d2d1_1.h>

namespace wl {

// 0xRRGGBB + alfa -> D2D1_COLOR_F
constexpr D2D1_COLOR_F Rgb(unsigned hex, float a = 1.0f) {
    return D2D1_COLOR_F{ ((hex >> 16) & 0xFF) / 255.0f,
                         ((hex >> 8) & 0xFF) / 255.0f,
                         (hex & 0xFF) / 255.0f, a };
}

struct Theme {
    bool dark;
    D2D1_COLOR_F bg;        // pencere zemini
    D2D1_COLOR_F surface;   // sağ panel, kartlar, durum çubuğu, girişler
    D2D1_COLOR_F sunk;      // segment zemini, rozet zemini, menü seçili
    D2D1_COLOR_F hover;     // satır/düğme üzerine gelince (yarı saydam)
    D2D1_COLOR_F line;      // ayraç çizgileri, kart kenarı
    D2D1_COLOR_F line2;     // onay kutusu kenarı, belirgin kenar
    D2D1_COLOR_F text;      // birincil metin
    D2D1_COLOR_F text2;     // ikincil metin
    D2D1_COLOR_F text3;     // üçüncül metin, etiketler (kontrast >= 4.5:1)
    D2D1_COLOR_F ac;        // vurgu dolgusu (birincil düğme, seçili onay, ilerleme)
    D2D1_COLOR_F acFg;      // vurgu metni (bağlantı, sayaçlar, yüzde)
    D2D1_COLOR_F acSoft;    // seçili satır zemini, odak halkası
    D2D1_COLOR_F onAccent;  // vurgu dolgusu üstündeki metin
    D2D1_COLOR_F ok;        // doğrulandı
    D2D1_COLOR_F warn;      // "Dikkat", yükleniyor
    D2D1_COLOR_F risk;      // "Riskli"
    D2D1_COLOR_F closeHover;// başlık çubuğu kapat düğmesi
};

inline const Theme& LightTheme() {
    static const Theme t{
        false,
        Rgb(0xF7F7F8), Rgb(0xFFFFFF), Rgb(0xF1F1F3), Rgb(0x18181B, 0.04f),
        Rgb(0xE8E8EB), Rgb(0xD8D8DD),
        Rgb(0x18181B), Rgb(0x52525B), Rgb(0x6B6B74),
        Rgb(0x2563EB), Rgb(0x1D4ED8), Rgb(0xEEF3FE), Rgb(0xFFFFFF),
        Rgb(0x15803D), Rgb(0xA15C00), Rgb(0xC0262D), Rgb(0xD92D3A) };
    return t;
}

inline const Theme& DarkTheme() {
    static const Theme t{
        true,
        Rgb(0x111113), Rgb(0x18181B), Rgb(0x1F1F23), Rgb(0xFFFFFF, 0.04f),
        Rgb(0x26262A), Rgb(0x34343A),
        Rgb(0xEDEDEF), Rgb(0xA9A9B1), Rgb(0x8D8D96),
        Rgb(0x2563EB), Rgb(0x7AA7FF), Rgb(0x3B82F6, 0.14f), Rgb(0xFFFFFF),
        Rgb(0x4ADE80), Rgb(0xF0B450), Rgb(0xFF7A7F), Rgb(0xD92D3A) };
    return t;
}

// ---------------------------------------------------------------------------
// Ölçüler (DIP)
// ---------------------------------------------------------------------------
namespace m {
    // Pencere iskeleti
    constexpr float TitleBarH      = 44;   // logo + Görüntü/Ön Ayar/Araçlar + arama + pencere düğmeleri
    constexpr float CaptionBtnW    = 46;   // küçült / büyüt / kapat
    constexpr float MenuBtnH       = 30;   // üst menü düğmesi
    constexpr float MenuBtnPadX    = 10;
    constexpr float SearchMaxW     = 440;  // Ctrl K arama kutusu (ortada)
    constexpr float SearchH        = 34;
    constexpr float ContentPadX    = 40;   // içerik sol/sağ boşluğu
    constexpr float HeaderTopPad   = 26;   // görüntü başlığının üstü
    constexpr float TabsTopPad     = 16;   // görüntü başlığı ile sekmeler arası
    constexpr float StatusBarH     = 30;
    constexpr float StatusBarPadX  = 20;

    // Sekmeler (grup başlıklı)
    constexpr float GroupCaptionH  = 16;   // BAŞLAT / YÜKLE / KALDIR / YAPILANDIR / BİTİR
    constexpr float TabH           = 40;
    constexpr float TabGap         = 18;   // grup içi sekmeler arası
    constexpr float GroupGap       = 18;   // ayraç çizgisinin iki yanı
    constexpr float TabUnderlineH  = 2;
    constexpr float CountGap       = 6;    // sekme adı ile sayaç arası

    // Sağ panel (Değişiklikler / WIM bilgisi)
    constexpr float SidePanelW     = 320;  // Bileşenler, Ayarlar
    constexpr float SidePanelWideW = 340;  // Görüntü
    constexpr float SidePanelPad   = 24;
    constexpr float JournalRowH    = 30;

    // Listeler
    constexpr float ListRowH       = 56;   // bileşen satırı (2 satır metin)
    constexpr float TableRowH      = 52;   // sürüm tablosu satırı
    constexpr float TableHeadH     = 34;
    constexpr float SettingRowH    = 64;   // ayar satırı (en az)
    constexpr float RowSelBarW     = 2;    // seçili satırın soldaki mavi şeridi
    constexpr float CatTopPad      = 22;   // kategori başlığının üstü

    // Denetimler
    constexpr float BtnH           = 40;
    constexpr float BtnBigH        = 44;   // "Uygula"
    constexpr float BtnPadX        = 18;
    constexpr float BtnRadius      = 10;
    constexpr float InputH         = 36;
    constexpr float InputRadius    = 9;
    constexpr float PillH          = 30;   // filtre hapları (Tümü / Seçilenler / ...)
    constexpr float CheckSize      = 18;
    constexpr float CheckRadius    = 5;
    constexpr float SwitchW        = 40;
    constexpr float SwitchH        = 22;
    constexpr float SwitchThumb    = 16;
    constexpr float SegH           = 34;   // dış; iç düğme 28
    constexpr float SegPad         = 3;
    constexpr float SegRadius      = 9;
    constexpr float SegInnerRadius = 7;
    constexpr float ProgressH      = 4;    // değişiklik paneli
    constexpr float ProgressThickH = 6;    // yükleme paneli
    constexpr float CardRadius     = 12;
    constexpr float BadgeRadius    = 6;
    constexpr float DotSize        = 7;
    constexpr float FocusRing      = 3;    // acSoft halka kalınlığı
}

// ---------------------------------------------------------------------------
// Yazı tipi ölçüleri (DIP) — Painter bunları TextStyle olarak hazırlar
// ---------------------------------------------------------------------------
namespace type {
    constexpr const wchar_t* UiFamily       = L"Segoe UI Variable Text"; // yoksa "Segoe UI"
    constexpr const wchar_t* UiFallback     = L"Segoe UI";
    constexpr const wchar_t* MonoFamily     = L"Cascadia Mono";          // yoksa "Consolas"
    constexpr const wchar_t* MonoFallback   = L"Consolas";
}

} // namespace wl
