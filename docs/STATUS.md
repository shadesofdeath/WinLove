# Durum — "Neredeyiz?"

> Her oturumun sonunda güncellenir. En üstte güncel durum; geçmiş en altta kısa satırlar.

## Güncel
- **Faz:** 1 (UI çatısı) ✅ → Faz 2 (motor temeli) başlıyor.
- **Aktif iş:** Faz 2.1: `Log`, `TaskRunner`, `CancelToken`, `Progress`, UI dispatcher.
- **Bir sonraki somut adım:** `src/core/base/Log` (dosya + bellek halka tamponu + stdout), ardından `core/tasks/TaskRunner` (tek motor thread'i) ve UI'a sonuç taşıyan dispatcher (`WM_APP`). Sonra 2.2 (yetki) → 2.3 (WIM okuma, `wlcli info`).
- **Build:** `./build.ps1 -Test` yeşil: 32 test / 1652 assertion (widget davranışı + render + token + metin).
- **Kullanıcı kontrolü (elle, tek seferlik):** `build\x64-debug\bin\WinLove.exe` →
  1. Maximize butonunun üstünde beklet → Windows 11 Snap Layouts açılmalı.
  2. Başlıktan sürükle / çift tıkla / kenarlardan boyutlandır. 1200'ün altına daraltınca menü 44'e inmeli.
  3. Menüde gezin (tık, Tab ile menüye gir, ↑↓, Enter). Ctrl+B daralt/aç, Ctrl+1…9, Ctrl+Shift+T tema, Ctrl+Shift+G galeri.
  4. Butonlarda hover/basma geçişleri (galeri), küçült/kapat tooltip'leri, daraltılmış menüde tooltip.
- **Bilinen sorunlar / açık konular:**
  - ScrollBar/ScrollView ve Inspector paneli henüz yok; ilk ihtiyaç duyan sayfayla (P01/P02) gelecek.
  - DComp ve dirty-rect yok (D-011). Tüm kare çiziliyor, animasyon yokken 0 CPU.
  - UI Automation sağlayıcısı yok (rol/ad bilgisi widget'larda tutuluyor; Faz 4).
  - F6 ile bölgeler arası atlama ve Alt+Space özel yönlendirmesi yok.
  - Section stilinde büyük harf Türkçe'ye duyarlı değil (i→İ).
  - Claude Code terminali admin değil → mount/apply testleri kullanıcının yönetici terminalinde.

## Faz 1 özeti
- Pencere: özel başlık, Snap Layouts için HT* bölgeleri, DPI v2, DWM çerçeve renkleri, zamanlayıcı/imleç/ayar değişikliği.
- Render: D3D11 (+WARP) / D2D, swapchain + offscreen PNG, Canvas (Ink renk karışımı, piksel hizalı 1px, gölge efekti, opaklık katmanı), SVG path → geometri, gömülü fontlar.
- Widget sistemi: Widget/Host/Stack/Tween; widget'lar: Button, Label, Kbd, Splitter, EmptyState.
- Kabuk: TitleBar, NavRail, StatusBar, PageView, sayfa kataloğu, galeri, kısayollar.
- Doğrulama: `--render` + durum simülasyonu, `compare_design.py`, `capture_window.py`.

## Ortam doğrulaması (2026-09-28)
VS 2026 Community (MSVC 14.50/14.51), Windows SDK 10.0.26100, ADK Deployment Tools, dismapi.dll 10.0.26100, Python 3.14 (fonttools, pillow, playwright; tarayıcı: Playwright Chromium / Chrome), Git. CMake yalnızca VS içinde (PATH'te değil). C: ~330 GB boş. Ana ekran 144 DPI (%150). PowerShell betik politikası kısıtlı (`-ExecutionPolicy Bypass` gerekir).

## Geçmiş
- 2026-09-28 — Faz 1 tamamlandı: widget sistemi, temel widget'lar, uygulama kabuğu, galeri, otomatik daralma.
- 2026-09-28 — Faz 1.1–1.3 + 1.5: ilk pencere, özel başlık çubuğu, render altyapısı, görsel doğrulama araçları.
- 2026-09-28 — Faz 0 tamamlandı (iskelet, build, üreticiler, fontlar, marka, testler).
- 2026-09-28 — UI handoff paketi alındı (palet "Bakır", 115 ikon, 27 ekran). Proje planı ve dokümanlar yazıldı.
