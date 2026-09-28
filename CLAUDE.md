# WinLove — AI Giriş Noktası

> Bu dosya her AI oturumunun **ilk okuduğu** dosyadır. Kısa tutulur; ayrıntı `docs/` altındadır.
> Oturuma başlarken sırayla oku: **bu dosya → `docs/STATUS.md` → aktif sayfanın `docs/pages/NN-*.md` dosyası.**
> Gerekirse: `docs/ARCHITECTURE.md`, `docs/ENGINE.md`, `docs/WORKFLOW.md`, `docs/CONVENTIONS.md`, `docs/TESTING.md`, `docs/DECISIONS.md`.

## Proje nedir
WinLove, NTLite seviyesinde bir **Windows imaj özelleştirme aracı**dır (yalnızca çevrimdışı imajlar; çalışan sistemi düzenleme kapsam dışı, D-021) (ISO/WIM/ESD/VHD → bileşen kaldırma, özellikler, güncellemeler, sürücüler, kayıt defteri, servisler, katılımsız kurulum, ISO oluşturma).
- Dil: **C++23** (`/std:c++latest`), MSVC (VS 2026), CMake + Ninja, statik CRT. Yalnızca Windows 10/11 x64 hedef (ARM64 imajları *işlenebilir*, uygulama x64 çalışır).
- Arayüz: **Direct2D + DirectWrite + DirectComposition** ile tamamen custom-draw. Win32 common control, WinUI, WPF, Qt, Electron **yok**.
- Motor: kendi `wl::image` katmanımız → arkada `dismapi.dll`, `wimgapi.dll`, offline registry ve gerekirse kendi native uygulamalarımız. (`docs/ENGINE.md`)

## Altın kurallar (asla çiğneme)
1. **Bir sayfa bitmeden diğerine geçilmez.** Sıra ve durum `docs/ROADMAP.md`'de. Bitmiş sayılması için `docs/WORKFLOW.md` → "Bitti Tanımı"nın tamamı karşılanmalı **ve kullanıcı onaylamalı**.
2. **Katman sınırları:** `base` ← `core` / `ui` ← `app`. `core` UI bilmez; `ui` Windows imajı bilmez; `app` ikisini birleştirir. `tools/check_layers.py` her build'de denetler. (`docs/ARCHITECTURE.md`)
3. **Tasarımın tek kaynağı `WinLove-UI-Handoff/`** (salt okunur). Renk/ölçü/font kodda sabit yazılmaz; `ui/generated/Tokens.g.h` üzerinden gelir. UI metni kodda sabit yazılmaz; `resources/strings/{tr,en}.json` (yaşayan kaynak, yeni anahtarlar buraya) → `Str::` enum. `src/*/generated/` elle düzenlenmez. Tasarımdan sapma gerekirse `docs/DECISIONS.md`'ye yazılır.
4. **Tasarım dili:** Fluent/WinUI/Mica görünümü yok; minimalist, kompakt (24px kontrol/satır, 12px metin, 2–4px radius). Büyük öğe ekleme.
5. **İmaj güvenliği:** Test ISO'sunun orijinaline asla yazılmaz. Tüm işlemler repo içindeki `build\lab\` altında kopyalar üzerinde yapılır; kullanıcının diskinde (ör. `C:\` kökünde) klasör açılmaz, kullanıcının son kullanılanlar listesi test yollarıyla kirletilmez. Bir oturumda mount edilen her imaj oturum sonunda unmount edilir (`wlcli cleanup`). (`docs/TESTING.md`)
6. **Önce motor, sonra UI:** Bir özelliğin motoru `wlcli` üzerinden gerçek imajda çalıştığı kanıtlanmadan UI'ı yazılmaz.
7. **Oturum sonu protokolü:** `docs/STATUS.md` güncellenir (ne yapıldı, ne kaldı, bir sonraki somut adım, bilinen sorunlar). Öğrenilen DISM/WIM tuhaflıkları `docs/ENGINE.md` → "Saha notları"na eklenir.
8. Build kırık bırakılmaz; testler geçmeden iş "bitti" denmez. Başarısız bir şeyi başarılı gibi raporlama.
9. Commit/push yalnızca kullanıcı isteyince.

## Dizin haritası
```
WinLover/
├─ CLAUDE.md                 ← buradasın
├─ docs/                     ← plan, mimari, iş akışı, durum (Türkçe)
│  └─ pages/                 ← her sayfanın spec + kabul kriterleri
├─ WinLove-UI-Handoff/       ← tasarım paketi (SALT OKUNUR, tek kaynak)
├─ src/
│  ├─ base/                  ← wl — Result/Error, UTF-8 (core ve ui'ın ortak tek bağımlılığı)
│  ├─ core/                  ← wl::core — imaj motoru, işlemler, log, görevler (UI yok)
│  ├─ ui/                    ← wl::ui   — D2D/DWrite/DComp framework + widget'lar (imaj bilgisi yok)
│  ├─ app/                   ← wl::app  — shell, sayfalar, uygulama durumu, WinLove.exe
│  └─ cli/                   ← wlcli.exe — motoru UI'sız test etme aracı
├─ tests/                    ← unit (doctest) + integration (gerçek imaj, admin)
├─ tools/                    ← gen_*.py (tokens/ikon/string → C++), check_layers.py, build_brand.py, render karşılaştırma
├─ third_party/              ← vendored: doctest, nlohmann/json, pugixml
├─ resources/                ← fontlar (OFL), brand (ico, logo), strings/*.json, manifest, catalog
└─ build.ps1                 ← tek komutla build/test
```

## Komutlar
```powershell
./build.ps1                    # gen + katman denetimi + Debug build (preset x64-debug)
./build.ps1 -Config Release
./build.ps1 -Test              # unit testler
./build.ps1 -Test -Integration # gerçek imaj testleri (yönetici PowerShell gerekir)
./build.ps1 -Gen               # yalnızca üreticiler (Tokens.g.h, Icons.g.h, StringKeys.g.h)
./build.ps1 -Target WinLove    # tek hedef (hızlı döngü)
./build.ps1 -Dist              # Release + test + dist\WinLove.exe — KULLANICIYA DENETİLECEK SÜRÜM (her sayfa teslimi öncesi çalıştır)
python tools/build_brand.py    # handoff'tan logo/ico yeniden üret (nadiren)
build/x64-debug/bin/WinLove.exe --render=shot.png --page=images --theme=light --scale=1.5 --lang=en   # pencere açmadan PNG
   #   durum simülasyonu: --hover-at=x,y --press-at=x,y --tooltip-at=x,y --tab=N --nav-collapsed --maximized  (tam liste: src/app/App.h)
python tools/compare_design.py 01-welcome-source --theme=dark --crop=0,0,1440,40   # tasarım | WinLove | fark → build/visual/
python tools/capture_window.py out.png [--maximized] [-- --theme=light]              # gerçek pencereyi aç, YALNIZCA onu yakala, kapat
build/x64-debug/bin/wlcli.exe help                                                         # motor testi (komutlar Faz 2'de)
```
> Betik çalıştırma kapalıysa: `powershell -ExecutionPolicy Bypass -File build.ps1 -Test` (Claude Code'da PowerShell aracı sorunsuz çalıştırır).
> Görsel doğrulamada asla tüm ekranı yakalama (ImageGrab vb.): kullanıcının diğer pencereleri görüntüye girer. Yalnızca `--render` veya `capture_window.py` (PrintWindow) kullan.
> Not: Claude Code terminali yönetici değildir. Admin gereken komutlar (mount, apply, integration test) için kullanıcıdan `! <komut>` ile yönetici terminalinde çalıştırmasını iste veya yönetici olarak açılmış oturum kullan.

## Ortam
- VS 2026 Community (MSVC 14.5x), Windows SDK 10.0.26100, ADK Deployment Tools kurulu, Python 3.14, Git.
- CMake/Ninja VS ile gelen sürüm (PATH'te değil): `build.ps1` VS Dev ortamını kendisi yükler; Claude Code'dan PowerShell aracıyla çalıştır.
- Test ISO: `C:\Users\shades\Downloads\Win11_25H2_Turkish_x64_v2.iso` (salt okunur kabul et).
