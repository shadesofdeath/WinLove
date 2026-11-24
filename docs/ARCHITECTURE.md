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
- `Source`: kullanıcının açtığı şey (ISO / WIM / ESD / SWM / çıkarılmış klasör; 2026-11-11'den beri VHD / VHDX, D-141). ISO ise önce çıkartılır (veya `virtdisk` ile bağlanır) → `sources/install.wim|esd`. VHD / VHDX `core/image/VirtualDisk` ile: üstüne WinLove'un fark diski bağlanır, Windows birimi boş bir klasöre birim bağlama noktası olarak konur ve o klasör bağlı bir WIM'in klasörü gibi kullanılır; kaydet = fark diskini birleştir, kaydetmeden çöz = fark diskini sil; kayıtlar `%LOCALAPPDATA%\WinLove\vhd-mounts.json` (ENGINE §4b).
- `ImageInfo`: index, ad, sürüm, mimari, build, diller, boyut (wimgapi XML'inden).
- `Session`: bir index'in mount edilmiş hali. RAII değil **açık yaşam döngüsü** (mount/commit/discard), çünkü crash sonrası `cleanup` gerekir. Aktif mount'lar `%LOCALAPPDATA%\WinLove\mounts.json`'a yazılır; açılışta sahipsiz mount'lar bulunur ve kullanıcıya sorulur.
- `IImageBackend` arayüzü; ilk uygulamalar `DismApiBackend` (dismapi.dll) ve `WimgApiBackend` (wimgapi.dll). Ayrıntı: `docs/ENGINE.md`.

### 2.3 Değişiklik modeli (`core/ops`) — projenin kalbi
Referans araç gibi: kullanıcı hiçbir şeyi anında değiştirmez; **ChangeSet**'e ekler, "Uygula" ile işlenir.
```
Operation (değer tipi — D-019): { kind, target, value, risk, sizeDelta }
  kind: RemovePackage | RemoveCapability | RemoveAppx | DisableFeature | EnableFeature
        | AddDriver | AddPackage | SetRegistryValue | SetServiceStart
ChangeSet (core/ops/ChangeSet): (kind, target) başına tek işlem; ters işlem (aç↔kapat) ikisini de siler;
           undo/redo (200 adım); version() sayacı; JSON = preset dosyası ("winlove.changeset" v1)
Planner:   ChangeSet → ApplyPlan: fazlar Remove → Features → Drivers → Updates → Settings, faz içinde
           kullanıcı sırası; yüksek risk uyarıları. (SSU→LCU sıralaması P08 ile)
Applier:   ApplyPlan'ı DismSession üzerinde adım adım çalıştırır, loglar, ilerleme = adım + adım içi;
           ErrorPolicy Stop/Skip; iptal adımlar arasında (imaj bağlı kalır). DISM oturumu tembel açılır: kovan /
           dosya işi için kapanan oturum ancak sonraki DISM çağrısında açılır; açılamazsa koşu durur
           (ApplyReport::sessionLost, D-131)
```
Presetler, Uygula özeti, geri al/yinele, "kuyruktaki değişiklik sayısı" rozetleri hep ChangeSet'ten türetilir.

### 2.4 Katalog (`core/catalog`)
Bileşen adları, açıklamaları, risk seviyeleri, bağımlılıklar, "kaldırılabilir mi" bilgisi kodda değil `resources/catalog/*.json` içinde tutulur. İmajdan okunan ham veri (paket listesi) katalog ile eşleşir → UI'daki ağaç. Katalog sürümlüdür ve test edilir.

### 2.5 Görevler (`core/tasks`)
- `TaskRunner`: tek bir **motor iş parçacığı** (DISM oturumu thread-affine davranır; paralel DISM işlemi yok). Kuyruklanmış işler sırayla çalışır. `AppState`'te iki tane: motor (`engine()`: DISM / WIMGAPI işleri, birer birer) ve okuyucu (`reader()`: uzun bir DISM işinin arkasında beklememesi gereken hızlı dosya okumaları); ikisi `AppState`'in son üyeleri, önce onlar durur (AUDIT APP-17). Ayrıca kendi runner'ı olan denetleyiciler var (Programlar, sürücü dışa aktarma, Uygulamalar, Windows indir).
- Yıkıcı kuyruktakilerin **ve çalışan işin** belirtecini iptal eder, çalışanı bekler, sonra `join` (D-129; imaja yazan işler kapanışta `confirmClose` ile zaten korunur). Duruş sırasında gelen iş hemen iptal edilir; `done`'dan çıkan istisna iş parçacığını düşürmez; motor iş parçacığından çağrılan `drain()` hemen döner.
- Çocuk süreçler (`core/system/Process`, `runProcess`) kill-on-close bir job'da çalışır; bağlı imajda dism.exe, diskpart ve bootsect bunun dışında (`ProcessLife::Outlives`); iptal belirteci yalnız yarıda kesilmesi zararsız araçlara verilir (D-130).
- UI thread'i asla bloklanmaz. İş sonucu ve ilerleme UI'a `ui::Window::post` ile iletilir (kilitli kuyruk + pencereye `kPostMessage`; öğeler sırayla, iç içe mesaj döngüsünde de sırası bozulmadan çalışır). Sistem dosya diyaloğu açıkken `ui::PostHold` gönderilenleri bekletir.

## 3. ui — çizim framework'ü

| Modül | Sorumluluk |
|---|---|
| `platform/Window` | HWND, `WM_NCCALCSIZE` ile custom titlebar, `WM_NCHITTEST` (Snap Layouts için HTMAXBUTTON), Per-Monitor-V2 DPI, min boyut |
| `render/RenderDevice`, `SwapChainTarget`, `OffscreenTarget`, `Graphics` | D3D11 (donanım, yoksa WARP) + HWND flip swapchain (`B8G8R8A8`, opak), D2D DeviceContext; offscreen hedef → PNG. `Graphics` = device + fontlar + text + ikonlar; **device lost**'ta bütün olarak yeniden kurulur. DComp yok (D-011; D-135'te denendi, sahibinin isteğiyle kaldırıldı) |
| `render/Canvas`, `render/SvgPath` | Çizim API'si (yalnızca token renkleri): `fillRect`, `fillRoundRect`, `strokeRoundRect`, `hairlineH/V` (tam 1 fiziksel piksel), `line`, `drawText`, `drawIcon`, clip. SVG path verisi → `ID2D1PathGeometry` |
| `text/FontLibrary`, `text/TextStyles` | Gömülü font baytlarından özel koleksiyon (tipografik aile modeli: Plex 400/500/600 tek aile). Her `TypeStyle` için bir `IDWriteTextFormat`: token satır yüksekliği + font metriklerinden baseline, `…` ile kesme, tek satır. `TextLayout` LRU cache → 1.4 (yok; 2026-11-10'dan beri yalnız kısa metinlerin — ≤128 karakter — genişliği stil başına saklanır, 4.096 girişte ve yerel ayar değişince boşalır: AUDIT UI-09) |
| `theme/Palette` | `tokens` → renk (dark/light/hc), WCAG kontrast, `bestContrast`. Yoğunluk ve kullanıcı vurgu rengi → P16 |
| `icons/IconCache` | Üretilmiş tablo (`Icons.g.h`: handoff ikonları + `brand-mark`) → tembel `ID2D1PathGeometry` önbelleği, round cap/join |
| `widget/Widget` | Temel sınıf: çocuklar (sahiplik), mutlak `bounds` (pencere DIP), `measure`/`layout`, `paint`/`paintOverlay`, `hitTest`, `windowZone` (başlık sürükleme/caption), `cursor`, olaylar (hover/press/click/double-click/key/focus), `tick` (animasyon), tooltip, UIA rol+ad, `forceVisualState` (galeri/render) |
| `widget/Host` | Bir pencerenin ağacı: işaretçi yönlendirme (hover, basılıyken yakalama, dışarıda bırakınca iptal), Tab/Shift+Tab + `tabStop=false` ile roving focus, odak halkası yalnızca klavyede, 400 ms tooltip (gölge + kenara çarpınca kayma; öğe başına tooltip: widget üzerindeyken `setTooltip` → `Host::tooltipChanged`, yeri `Widget::tooltipAnchor`), animasyon karesi isteme. Silinen/gizlenen widget'ları `forget` ile güvenle bırakır |
| `widget/Stack` | Yatay/dikey yerleşim: `Sizing::fixed/autoSize/fill`, `CrossAlign`, gap, `Insets` |
| `anim/Tween` | cubic-bezier easing (motion token'ları), Windows "animasyonları göster" kapalıysa anında; `--render` için `forceInstantMotion` |
| `anim/ItemMotion` | Tek widget içindeki öğelerin hareketi: `ItemHover` (sekme / bölüm / tablo satırı hover dolgusu Button'unki gibi `motion.fast` ile söner-yanar), `SelectionMove` (sekme alt çizgisi, RadioGroup'un yükseltilmiş bölümü yeni seçime `motion.base` ile kayar). Anında harekette son durum: durgun kare eskisiyle aynı |
| `widgets/*` | Şu an: Button, Label, Kbd, Splitter, EmptyState. Checkbox, Toggle, TextBox, ComboBox, Menu, TreeView (sanal), DataGrid (sanal), InfoBar, Toast, Dialog, Progress, Spinner, LogView, ScrollBar… ihtiyaç duyan ilk sayfayla birlikte yazılır ve galeriye eklenir |
| `platform/DropTarget`, `platform/FileDialog` | OLE sürükle-bırak (pencere geneli; `OleInitialize` gerekir), sistem dosya/klasör seçicileri |
| Sağ tık menüsü | `Window` `WM_RBUTTONUP` → `Host::onContextMenu(p)` → imleç altındaki widget'tan köke `Widget::onContextMenu`; Menü tuşu / Shift+F10 odaktaki widget için aynı yol. Menü = `MenuPopup` (`selected = -1`, çapa = tıklanan nokta). İlk kullanan: `RecentList`. |
| Host modal katmanı | `Host::pushModal/popModal`: scrim, girdi ve odak kapanı, kapanınca odak geri döner; modal açıkken başlık sürüklenir ama caption butonları pasif |
| `a11y` | UI Automation provider'ları (widget başına rol/ad/durum) |

Render akışı (2026-11-10'da koddan yeniden okundu): input → state değişir → widget `invalidate()` → `Host::requestFrame` → `InvalidateRect(hwnd, nullptr)` → `WM_PAINT`'te **bütün pencere** baştan çizilir (`App::paint`: `clear` + `Host::paint`) → `Present(1, 0)`, kirli dikdörtgen yok (AUDIT-2026-10 D4 / D5 açık). Tek kısaltma: kırpan bir ebeveynin (`clipsChildren`) alanının tümüyle dışında kalan alt ağaç çizilmez (AUDIT UI-02). Animasyon varken: tick'ler çizimin içinde koşar ve onların istediği kare o çizimin kendisidir (`m_ticking`, AUDIT UI-01); sonraki kareyi döngünün sonu ister — `tickIntervalMs` > 0 ise kare zamanlayıcısıyla (iş şeritleri 100–250 ms), değilse hemen (vsync). Animasyon yokken uyur (boşta %0 CPU).

**Görsel doğrulama:** `WinLove.exe --render=x.png --theme=<dark|light|hc> --scale=<1|1.5|2>` pencere göstermeden offscreen render eder. `tools/compare_design.py` bunu `WinLove-UI-Handoff/04_screens/*.svg` ile yan yana + fark görüntüsü yapar; `tools/capture_window.py` gerçek pencereyi yakalar; `tools/render_diff.py <eski exe> <yeni exe>` bütün sayfaları iki ikiliyle render edip pikselleri (RGB) karşılaştırır — çizimi değiştirmemesi gereken işler için (`docs/PERF-BASELINE.md`). AI her UI adımını bunlarla doğrular (`docs/TESTING.md`).

## 4. app

- `state/AppState`: motor ve okuyucu iş parçacıkları (`TaskRunner`, §2.5), açık kaynak (`SourceInfo`), bağlı imaj, kuyruk (`ChangeSet`, `queue.json`), imajdan okunan listeler, son kullanılanlar (`state/RecentSources`, `%LOCALAPPDATA%\WinLove\recent.json`). `subscribe()` ile değişiklikleri (`Change::…`) duyurur; dinleyiciler `shared_ptr<const>` tutulur, bildirim sırasında aboneliği kalkan çağrılmaz (AUDIT APP-16).
- Asenkron akış kalıbı (`Shell::openSource`): UI thread'inde `engine().run(work, done)`; `done` motor thread'inde yalnızca `postToUi`'ye devreder (UI nesnesine dokunmaz); UI tarafında `weak_ptr` canlılık kontrolü, sonra `AppState` güncellenir.
- `Format`: arayüz diline göre sayı/tarih ("6,72 GB", "bugün 14:02", "3 gün önce", "12 Eyl").
- Sayfalar düz `ui::Widget`'tır (ortak bir `Page` arayüzü yok): gövdeyi `Shell::showPage` kurar, başlık / açıklama / ikon `pages/PageInfo`'dan gelir, nav rozetleri `Shell::updateQueue`'dan.
- Komut paleti (P18): `shell/PaletteIndex` (ne bulunur, nasıl sıralanır — önbelleksiz, her aramada durumdan) + `shell/CommandPalette` (modal widget). Komutlar `PaletteCommand` enum'unda; çalıştıran `Shell::runPaletteCommand` (kısayollar da aynı yoldan). Bir sayfada satıra atlamak için sayfaya `reveal(id)` eklenir.
- `App` (`app/App.cpp`): Graphics + Window + SwapChainTarget'ı kurar; pencere modu veya `--render` modu. Komut satırı `app/App.h` başında.
- `Shell` (`app/shell`; D-107, iskelet D-109'dan beri referans aracınki, 2026-11-10 eklemeleri D-133): kök widget. TitleBar (`size::titleBar`: ≡ uygulama menüsü, çalışma alanları Görüntü · Ön Ayar · Araçlar, ortada başlık — 2026-11-11'den beri her zaman "WinLove", markanın yazı tipiyle (IBM Plex Sans SemiBold, "Love" vurgu tonunda; D-147) —, tema + CaptionButton'lar; komut paleti Ctrl+K). Altında PageView: şerit bandı (sayfanın eylemleri, `ButtonKind::Ribbon`; **eylemi olmayan sayfada bant çizilmez**, kartlar yine aynı yükseklikte başlar), ipucu satırı, sayfa kartı (sekme başlıkları + kaydırılabilir gövde `PageViewport`/`ScrollableBody`) ve varsa yan kart. Solda NavPanel (Başlat · Entegre et · Kaldır · Yapılandır · Bitir; sayfa başına kuyruk sayısı rozeti; hover ve seçim `ItemMotion` ile; Ctrl+B dar). En altta StatusBar: **tıklanabilir bölümler** (bağlı imaj → Görüntü; kuyruktaki değişiklikler ve tahmini kazanç → Uygula; söyleyecek bir şeyi olmayan bölüm çizilmez), çalışan iş, sağda imajın sürümü ya da bu PC'nin DISM'i ve boş alan. D-107'nin `ImageHeader`, `TabStrip` ve `ChangesPanel`'i kodda duruyor ama gizli. 2026-11-11 eklemeleri: imaj açıkken Görüntü kartı Kaynak seç · Sürümler · Özet sekmeleri (`workspaceTabs`, `attachSubNav`; Özet = `PageId::Overview`, D-138); `DetailSource` olan çalışma sayfalarına ortak ayrıntı kartını Shell kendisi ekler (`Shell::attachDetailCard`, pencere yeterince genişse, D-139); pencereli çalışmada `UpdateController` açılıştan 4 sn sonra, gerekiyorsa (ayar açık, son yanıt ≥ 24 sa önce) güncelleme denetimi yapar; kendi `TaskRunner`'ında, render ve testlerde kurulmaz (D-140). Uygulama kısayolları `Shell::handleShortcut` (odaktaki widget tüketmezse).
- `pages/PageInfo`: sayfa kataloğu (sıra, grup, metin anahtarları, ikon, yol haritası adımı). Menü, breadcrumb, Ctrl+1…9, `--page=` ve komut paleti buradan beslenir. Yeni sayfa = buraya satır + `Shell::showPage`'de gövdesi.
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
