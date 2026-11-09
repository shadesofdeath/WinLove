# WinLove UI — Direct2D / DirectWrite

WinLove arayüz tasarımının C++ karşılığı: tema renkleri, ölçüler ve bileşenleri çizen küçük bir yardımcı sınıf.
Bağımlılık yok; yalnızca Windows SDK (`d2d1.lib`, `dwrite.lib`).

Tasarım (canlı, etkileşimli): https://claude.ai/artifact/MRwC5evRGFH3pC8hiCKT86

```
include/wl/Theme.h      Açık/koyu renkler, tüm ölçüler (DIP), yazı tipi adları
include/wl/Painter.h    Çizim API'si
src/Painter.cpp         Uygulama
demo/main.cpp           Bileşenler ekranının çalışan örneği (özel başlık çubuğu dahil)
docs/LAYOUT.md          Ekran ekran yerleşim ve davranış ölçüleri
CMakeLists.txt          winlove_ui kütüphanesi + winlove_demo
```

## Projene ekleme

İki dosyayı kopyalaman yeterli: `include/wl/` ve `src/Painter.cpp`. Ya da CMake ile:

```cmake
add_subdirectory(WinLoveUI)
target_link_libraries(senin_uygulaman PRIVATE winlove_ui)
```

Visual Studio'da `/utf-8` açık olmalı (kaynaklarda Türkçe metin var). CMake bunu demo için ekliyor.

## Kullanım

```cpp
#include "wl/Painter.h"

wl::Painter p;
p.Init(d2dFactory1, dwriteFactory);          // bir kez

// WM_PAINT içinde
p.Begin(deviceContext, dark ? wl::DarkTheme() : wl::LightTheme());
p.Tab({x, y, x + p.TabWidth(L"Bileşenler", 9), y + wl::m::TabH}, L"Bileşenler", 9, true);
p.Checkbox({x, cy - 9, x + 18, cy + 9}, checked);
p.Button(r, L"Uygula", wl::ButtonKind::Primary, {hover});
p.Progress(r, 0.42f);
```

Painter yalnızca **çizer**. Yerleşim, hit-test ve durum (hover, seçim, işaretli) senin kodunda kalır; demo bunun nasıl yapılacağını gösteriyor (`g_hits` listesi).

### Bileşenler

| Fonksiyon | Tasarımdaki karşılığı |
|---|---|
| `MenuButton` | Üst menü: Görüntü · Ön Ayar · Araçlar |
| `CaptionButton` | Küçült / büyüt / kapat, tema düğmesi |
| `SearchBox` | Ctrl K arama kutusu, liste içi arama |
| `GroupCaption` + `Tab` | BAŞLAT / YÜKLE / KALDIR / YAPILANDIR / BİTİR grupları, sayaçlı sekmeler |
| `Pill` | Tümü / Seçilenler / Dikkat gerektiren |
| `Checkbox` | Bileşen seçimi (`mixed` = kategori kısmen seçili) |
| `Switch` | Ayar anahtarı (`anim` 0→1 ile geçiş) |
| `Segmented` | Tanılama verileri, WIM / ESD / SWM |
| `Progress` | Uygula ve Yükle ilerlemesi |
| `Badge` | "Bağlı", "Yükleniyor 42%" |
| `StatusText` | "● Dikkat", durum çubuğu |
| `RowBackground` | Liste/tablo satırı: hover, seçili (mavi şerit), ayraç |
| `DrawIcon` | Arama, ok, onay, kapat, artı, varsayılana dön, güneş/ay, uyarı |

## Netlik için

- **DPI:** `SetProcessDpiAwarenessContext(PER_MONITOR_AWARE_V2)` ve render target'a `SetDpi(dpi, dpi)`. Tüm ölçüler DIP; fare koordinatlarını `x * 96 / dpi` ile DIP'e çevir.
- **Metin:** `D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE` opak zeminde en keskin sonucu verir. Mica/şeffaf zemine çizersen `GRAYSCALE` kullan.
- **Çizgiler:** `HLine/VLine` dikdörtgen doldurur (yarım piksel bulanıklığı olmaz). `Stroke` kenarı kutunun içine alır.
- **Yazı tipi:** varsayılan `Segoe UI Variable Text` (Windows 11), yoksa `Segoe UI`. Tasarımdaki Inter'i istersen `IDWriteFactory5::CreateInMemoryFontFileLoader` ile gömüp `type::UiFamily`'yi değiştir.
- **Harf aralığı:** başlık (-0.025em) ve grup başlıkları (+0.06em) `IDWriteTextLayout1::SetCharacterSpacing` ile uygulanıyor. Büyük harf dönüşümünü sen ver (`L"YAPILANDIR"`).

## Özel başlık çubuğu

Demo, sistem başlığını kaldırıp kenarlık, gölge ve yuvarlak köşeyi koruyor:
`WM_NCCALCSIZE` → 0, `WM_NCHITTEST` → kenarlarda `HT*`, başlık boşluğunda `HTCAPTION`, `DwmExtendFrameIntoClientArea`, `DWMWA_WINDOW_CORNER_PREFERENCE`.
Windows 11 "Snap layouts" menüsü için büyüt düğmesinin üzerinde `HTMAXBUTTON` döndürmen ve `WM_NCLBUTTONDOWN/UP`'ı kendin işlemen gerekir (demoda yok).

## Not

Kod MinGW-w64 ile uyarısız derlendi. Gerçek Windows üzerinde çalıştırılıp ekran görüntüsüyle karşılaştırılmadı; ilk açılışta tasarımla yan yana kontrol et.
