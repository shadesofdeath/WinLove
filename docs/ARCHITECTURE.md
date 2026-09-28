# Mimari

## 1. Katmanlar

```
┌────────────────────────────────────────────────────────────┐
│ app  (WinLove.exe)                                          │
│   Shell (titlebar, nav, inspector, statusbar)               │
│   Pages: SourcePage, ImagesPage, FeaturesPage ...           │
│   AppState (Session + ChangeSet gözlemcisi), Localization   │
├──────────────────────────┬─────────────────────────────────┤
│ ui  (wl_ui.lib)           │ core  (wl_core.lib)              │
│  Platform: Window, DPI,   │  image: Source, ImageInfo,       │
│   input, titlebar hittest │   Session, backends (DISM/WIM)   │
│  Render: D2D/DComp device,│  ops: Operation, ChangeSet,      │
│   brush/text/icon cache   │   Planner, Applier               │
│  Theme: tokens runtime    │  catalog: bileşen veritabanı     │
│  Widget ağacı, layout,    │  registry: offline hive          │
│   focus, animasyon        │  tasks: TaskRunner, Cancel,      │
│  Widgets: Button, Tree... │   Progress                       │
│                           │  log, paths                      │
├──────────────────────────┴─────────────────────────────────┤
│ base  (wl_base.lib) — Result/Error, UTF-8                    │
└────────────────────────────────────────────────────────────┘
          cli (wlcli.exe) ──────────► yalnızca core
          tests ────────────────────► core + ui (render testleri)
```

**Bağımlılık kuralı:** `app → ui, core`; `cli → core`; `ui → base`; `core → base`; `ui ↛ core`; `core ↛ ui`. CMake hedefleri link düzeyinde, `tools/check_layers.py` `#include` düzeyinde zorlar (her build'de çalışır).

## 2. core — imaj motoru

### 2.1 Temel tipler (`base/` ve `core/`)
- `base/Result.h`: `wl::Result<T>` = `std::expected<T, wl::Error>`; `Error { code, message, context, hresult }`; `wl::fail(...)`. İstisna modül sınırını geçmez. `ui` de aynı tipi kullanır; bu yüzden `core`'da değil `base`'dedir.
- `Log`: seviye (Trace/Debug/Info/Warn/Error), kaynak (dism, wim, reg, app...), zaman damgası. Hedefler: dosya (`%LOCALAPPDATA%\WinLove\logs\`), bellek içi halka tampon (Loglar sayfası okur), stdout (cli).
- `CancelToken`, `Progress { double fraction; std::wstring stage; }` callback'leri.

### 2.2 İmaj modeli (`core/image`)
- `Source`: kullanıcının açtığı şey (ISO / WIM / ESD / SWM / VHD / Live). ISO ise önce çıkartılır (veya `virtdisk` ile bağlanır) → `sources/install.wim|esd`.
- `ImageInfo`: index, ad, sürüm, mimari, build, diller, boyut (wimgapi XML'inden).
- `Session`: bir index'in mount edilmiş hali. RAII değil **açık yaşam döngüsü** (mount/commit/discard), çünkü crash sonrası `cleanup` gerekir. Aktif mount'lar `%LOCALAPPDATA%\WinLove\mounts.json`'a yazılır; açılışta sahipsiz mount'lar bulunur ve kullanıcıya sorulur.
- `IImageBackend` arayüzü; ilk uygulamalar `DismApiBackend` (dismapi.dll) ve `WimgApiBackend` (wimgapi.dll). Ayrıntı: `docs/ENGINE.md`.

### 2.3 Değişiklik modeli (`core/ops`) — projenin kalbi
NTLite gibi: kullanıcı hiçbir şeyi anında değiştirmez; **ChangeSet**'e ekler, "Uygula" ile işlenir.
```
Operation (soyut)
 ├─ id, kind (RemovePackage, DisableFeature, AddDriver, SetRegValue, SetService, ...)
 ├─ target (paket adı, özellik adı, ...)
 ├─ risk, estimatedSizeDelta
 ├─ validate(const ImageSnapshot&) -> Result<void>
 └─ apply(Session&, Progress&, CancelToken&) -> Result<void>
ChangeSet: sıralı Operation listesi + undo/redo + JSON serileştirme (preset = ChangeSet dosyası)
Planner:   ChangeSet → ApplyPlan (doğru sıralama: SSU→LCU, önce kaldırma sonra ekleme, registry en son; bağımlılık/çakışma kontrolü)
Applier:   ApplyPlan'ı Session üzerinde çalıştırır, her adımı loglar, hata politikası (durdur / atla)
```
Presetler, Uygula özeti, geri al/yinele, "kuyruktaki değişiklik sayısı" rozetleri hep ChangeSet'ten türetilir.

### 2.4 Katalog (`core/catalog`)
Bileşen adları, açıklamaları, risk seviyeleri, bağımlılıklar, "kaldırılabilir mi" bilgisi kodda değil `resources/catalog/*.json` içinde tutulur. İmajdan okunan ham veri (paket listesi) katalog ile eşleşir → UI'daki ağaç. Katalog sürümlüdür ve test edilir.

### 2.5 Görevler (`core/tasks`)
- `TaskRunner`: tek bir **motor iş parçacığı** (DISM oturumu thread-affine davranır; paralel DISM işlemi yok). Kuyruklanmış işler sırayla çalışır.
- UI thread'i asla bloklanmaz. İş sonucu ve ilerleme UI'a `PostMessage(WM_APP_TASK)` ile iletilir; UI tarafında `Dispatcher` callback'i ana thread'de çalıştırır.

## 3. ui — çizim framework'ü

| Modül | Sorumluluk |
|---|---|
| `platform/Window` | HWND, `WM_NCCALCSIZE` ile custom titlebar, `WM_NCHITTEST` (Snap Layouts için HTMAXBUTTON), Per-Monitor-V2 DPI, min boyut |
| `render/RenderDevice`, `SwapChainTarget`, `OffscreenTarget`, `Graphics` | D3D11 (donanım, yoksa WARP) + HWND flip swapchain (`B8G8R8A8`, opak), D2D DeviceContext; offscreen hedef → PNG. `Graphics` = device + fontlar + text + ikonlar; **device lost**'ta bütün olarak yeniden kurulur. DComp henüz yok (D-011) |
| `render/Canvas`, `render/SvgPath` | Çizim API'si (yalnızca token renkleri): `fillRect`, `fillRoundRect`, `strokeRoundRect`, `hairlineH/V` (tam 1 fiziksel piksel), `line`, `drawText`, `drawIcon`, clip. SVG path verisi → `ID2D1PathGeometry` |
| `text/FontLibrary`, `text/TextStyles` | Gömülü font baytlarından özel koleksiyon (tipografik aile modeli: Plex 400/500/600 tek aile). Her `TypeStyle` için bir `IDWriteTextFormat`: token satır yüksekliği + font metriklerinden baseline, `…` ile kesme, tek satır. `TextLayout` LRU cache → 1.4 |
| `theme/Palette` | `tokens` → renk (dark/light/hc), WCAG kontrast, `bestContrast`. Yoğunluk ve kullanıcı vurgu rengi → P16 |
| `icons/IconCache` | Üretilmiş tablo (`Icons.g.h`: handoff ikonları + `brand-mark`) → tembel `ID2D1PathGeometry` önbelleği, round cap/join |
| `widget/Widget` | Temel sınıf: çocuklar (sahiplik), mutlak `bounds` (pencere DIP), `measure`/`layout`, `paint`/`paintOverlay`, `hitTest`, `windowZone` (başlık sürükleme/caption), `cursor`, olaylar (hover/press/click/double-click/key/focus), `tick` (animasyon), tooltip, UIA rol+ad, `forceVisualState` (galeri/render) |
| `widget/Host` | Bir pencerenin ağacı: işaretçi yönlendirme (hover, basılıyken yakalama, dışarıda bırakınca iptal), Tab/Shift+Tab + `tabStop=false` ile roving focus, odak halkası yalnızca klavyede, 400 ms tooltip (gölge + kenara çarpınca kayma), animasyon karesi isteme. Silinen/gizlenen widget'ları `forget` ile güvenle bırakır |
| `widget/Stack` | Yatay/dikey yerleşim: `Sizing::fixed/autoSize/fill`, `CrossAlign`, gap, `Insets` |
| `anim/Tween` | cubic-bezier easing (motion token'ları), Windows "animasyonları göster" kapalıysa anında; `--render` için `forceInstantMotion` |
| `widgets/*` | Şu an: Button, Label, Kbd, Splitter, EmptyState. Checkbox, Toggle, TextBox, ComboBox, Menu, TreeView (sanal), DataGrid (sanal), InfoBar, Toast, Dialog, Progress, Spinner, LogView, ScrollBar… ihtiyaç duyan ilk sayfayla birlikte yazılır ve galeriye eklenir |
| `a11y` | UI Automation provider'ları (widget başına rol/ad/durum) |

Render akışı: input → state değişir → `invalidate(rect)` → sonraki `WM_PAINT`/vsync'te yalnızca kirli bölge çizilir → `Present1` dirty rect ile. Animasyon varken kare saati çalışır, yokken uyur (boşta %0 CPU).

**Görsel doğrulama:** `WinLove.exe --render=x.png --theme=<dark|light|hc> --scale=<1|1.5|2>` pencere göstermeden offscreen render eder. `tools/compare_design.py` bunu `WinLove-UI-Handoff/04_screens/*.svg` ile yan yana + fark görüntüsü yapar; `tools/capture_window.py` gerçek pencereyi yakalar. AI her UI adımını bunlarla doğrular (`docs/TESTING.md`).

## 4. app

- `AppState`: aktif `Source`, seçili index, `Session` durumu, `ChangeSet`, arka plan görevleri. Basit gözlemci (`Signal<>`) ile sayfalara duyurur.
- `Page` arayüzü: `id()`, `title()`, `buildView()`, `onEnter()/onLeave()`, `inspectorContent()`, `commands()` (komut paleti için), `pendingCount()` (nav rozeti).
- `App` (`app/App.cpp`): Graphics + Window + SwapChainTarget'ı kurar; pencere modu veya `--render` modu. Komut satırı `app/App.h` başında.
- `Shell` (`app/shell`): kök widget. TitleBar 32 (PaletteTrigger + CaptionButton widget'ları, breadcrumb = sayfa başlığı), NavRail 200/44 (gruplar `pages/PageInfo`'dan, daralma animasyonu, <1200'de otomatik daralma), PageView (başlık + açıklama + gövde), StatusBar 24 (mount segmenti, imaj bağlıyken görünen "Uygula · n" CTA). Uygulama kısayolları `Shell::handleShortcut` (odaktaki widget tüketmezse).
- `pages/PageInfo`: sayfa kataloğu (sıra, grup, metin anahtarları, ikon, yol haritası adımı). Menü, breadcrumb, Ctrl+1…9, `--page=` ve ileride komut paleti buradan beslenir. Yeni sayfa = buraya satır + `Shell::showPage`'de gövdesi.
- `Resources`: fontlar ve `strings/*.json` exe'ye gömülüdür (`WinLove.rc`, RCDATA); tek dosya dağıtım.
- `Localization`: `strings.tr.json` / `strings.en.json`; eksik anahtar derlemede (tools/gen) ve testte yakalanır.
- Ayarlar: `%LOCALAPPDATA%\WinLove\settings.json`.
- Uygulama manifesti: `requireAdministrator` **değil** `asInvoker`; admin gerektiren an gelince s4 ekranı ("Yönetici olarak yeniden başlat"). Böylece UI geliştirme admin'siz yapılabilir.

## 5. Veri akışı örneği (Özellik kapatma)
1. FeaturesPage açılır → `TaskRunner.enqueue(backend.listFeatures(session))` → sonuç UI'a gelir → tablo dolar.
2. Kullanıcı toggle'ı kapatır → `ChangeSet.add(DisableFeature{"MediaPlayback"})` → nav rozeti ve statusbar sayacı güncellenir (görüntü hemen "Kapatılacak" durumu gösterir, imaj değişmez).
3. Uygula → `Planner` plan üretir → özet ekranı → onay → `Applier` motor thread'inde çalışır → ilerleme + log canlı akar → bitti ekranı.

## 6. Üçüncü parti (vendored, `third_party/`)
doctest (test), nlohmann/json (JSON), pugixml (WIM XML, unattend.xml). Yeni bağımlılık eklemek `DECISIONS.md` kaydı gerektirir.
