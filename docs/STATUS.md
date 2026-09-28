# Durum — "Neredeyiz?"

> Her oturumun sonunda güncellenir. En üstte güncel durum; geçmiş en altta kısa satırlar.

## Güncel
- **Faz:** 1 — UI çatısı. 1.1, 1.2, 1.3 ve 1.5 ✅; sıradaki 1.4.
- **Aktif iş:** yok. Kullanıcı gerçek pencereyi denemeli (aşağıdaki "Kullanıcı kontrolü").
- **Bir sonraki somut adım:** Faz 1.4, widget ağacı: `ui/widget/Widget` (bounds, measure/arrange, paint, hitTest, focus, pointer/klavye olayları, invalidate), basit layout (Stack/Dock), odak yönetimi (Tab/F6, yalnızca klavyede focus ring), animasyon saati (motion token'ları, reduce motion). Ardından geçici `TitleBar` widget'lara bölünür (D-012).
- **Build:** `./build.ps1 -Test` yeşil. 23 test / 1597 assertion (render testleri dahil).
- **Kullanıcı kontrolü (elle, tek seferlik):** `build\x64-debug\bin\WinLove.exe` →
  1. Maximize butonunun üstünde beklet → Windows 11 Snap Layouts açılmalı.
  2. Başlıktan sürükle / çift tıkla ekranı kapla / kenarlardan ve üst kenardan boyutlandır.
  3. Kapat butonuna hover → kırmızı; butondan basılı tutup dışarı çekince tıklama iptal olmalı.
  4. Ctrl+Shift+T → açık/koyu tema.
  5. Farklı DPI'lı monitöre sürükle → keskin kalmalı.
- **Bilinen sorunlar / açık sorular:**
  - Başlıktaki sayfa göstergesi ("| Kaynak seç") yok; Shell/sayfalar (1.7) ile gelecek.
  - Keycap'ler tasarımdan ~2px daha geniş: tasarım tarayıcıda JetBrains Mono yerine yedek fontla render edilmiş, gerçek font bizde. Bilinçli, sorun değil.
  - Section stilindeki büyük harf dönüşümü Türkçe'ye duyarlı değil (i→İ). 1.7'de locale-aware helper gelecek.
  - Handoff TODO: 24px ikonlar 16'dan türetilmiş, optik düzeltme yok.
  - Claude Code terminali admin değil → mount/apply testleri kullanıcının yönetici terminalinde.

## Faz 1'de şu ana kadar yapılanlar
- `ui/platform/Window`: `WM_NCCALCSIZE` ile özel başlık; `WM_NCHITTEST` → HTCAPTION / HTMIN/MAX/CLOSE (Snap Layouts) / üst kenar HTTOP; caption butonlarına basma olayları Windows'a verilmiyor (klasik buton çizmesin diye). Per-Monitor-V2 DPI, min 1100×700, DWM koyu çerçeve + kenar rengi + yuvarlak köşe.
- `ui/render`: RenderDevice (WARP yedekli), SwapChainTarget, OffscreenTarget (PNG), Graphics, Canvas, SvgPath.
- `ui/text`: gömülü fontlardan koleksiyon, TypeStyle başına TextFormat. `ui/icons/IconCache`.
- `app`: `App` (pencere + `--render` modu), `Resources` (fontlar/metinler exe içinde), geçici `shell/TitleBar`. Ctrl+Shift+T tema.
- Araçlar: `compare_design.py`, `capture_window.py`. Ölçüm: boşta 0 ms CPU / 3 sn, ~67 MB bellek (Debug).

## Ortam doğrulaması (2026-09-28)
VS 2026 Community (MSVC 14.50/14.51), Windows SDK 10.0.26100, ADK Deployment Tools, dismapi.dll 10.0.26100, Python 3.14 (fonttools, pillow, playwright; tarayıcı: Playwright Chromium / Chrome), Git. CMake yalnızca VS içinde (PATH'te değil). C: ~330 GB boş. Ana ekran 144 DPI (%150).

## Geçmiş
- 2026-09-28 — Faz 1.1–1.3 + 1.5: ilk pencere, özel başlık çubuğu, render altyapısı, görsel doğrulama araçları.
- 2026-09-28 — Faz 0 tamamlandı (iskelet, build, üreticiler, fontlar, marka, testler).
- 2026-09-28 — UI handoff paketi alındı (palet "Bakır", 115 ikon, 27 ekran). Proje planı ve dokümanlar yazıldı.
