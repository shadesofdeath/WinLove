# WinLove — DirectComposition + Windows Animation Manager + DPI: tasarım belgesi

> **RAFA KALDIRILDI (2026-11-11):** kullanıcı işi iptal etti ("animasyon istemiyorum"); Aşama 0 ve 1 uygulanıp ölçüldü,
> sonra kullanıcının isteğiyle koddan tamamen kaldırıldı (Aşama 1b'nin DPI maddelerinden yalnız pencere simgesinin DPI'ya
> göre yüklenmesi, `--size`'ın istemci alanı olması ve çift tık alanının DPI'ya göre olması kaldı). Belge plan olarak duruyor.
>
> Hazırlayan: render mimarisi incelemesi (salt okuma; kaynak değiştirilmedi, derlenmedi, çalıştırılmadı), 2026-11-10.
> İstek (kullanıcı, 2026-11-10): "DirectComposition, Windows Animation Manager, DPI Awareness API da kullan akıcı güzel
> animasyonları olsun arayüzün."
> İşaretler: **[kod]** = bu depoda okundu (dosya:satır). **[API]** = API bilgisinden; bu makinede (Windows 11 Pro 26200,
> SDK 10.0.26100) **doğrulanmalı**. **[ölç]** = ancak Aşama 0'ın ölçümüyle bilinir.

---

## 0. Özet

- Bugün tek HWND flip swap chain'e **her karede bütün pencere** çiziliyor; kirli bölge yok (ARCHITECTURE.md §3 aksini
  yazıyor, yanlış). Bütün hareketler arayüz iş parçacığında çiziliyor; o iş parçacığı takılınca durup sonra atlıyor.
  Kare süresi bu projede **hiç ölçülmedi**.
- Önerilen: kök içerik bir **compositor swap chain**'i olarak küçük bir DComp görsel ağacına girer; yalnız **yüzen
  yüzeyler** (modal + scrim, menü / açılır liste, bildirim, araç ipucu) ve **geçiş anlık görüntüleri** (sayfa değişimi,
  tema) kendi yüzeylerini alır. Görünme / kaybolma compositor'da oynar (UI thread meşgulken bile, ekranın hızında);
  widget içi hareket (hover, basma, seçim kayması, tik, anahtar) bugünkü gibi çizilir.
- Windows Animation Manager (WAM) dar kapsamda: compositor eğrilerinin kaynağı (`GetCurve`) ve kesilen geçişlerde hız
  sürekliliği; kendi zamanlayıcısı kullanılmaz. Yoksa kendi bezier→polinom eğrimiz.
- DPI: uygulama **zaten Per-Monitor-V2**. "DPI Awareness API" burada cila demek: `WM_GETDPISCALEDSIZE`, DPI
  değişiminde tek yeniden boyutlama, DPI'ya göre pencere simgesi, çift tık aralığı, yakınlaştırmanın DComp'a
  sızmaması.
- Beş aşama, her biri gönderilebilir ve bir anahtarla eski yola dönülebilir: **0 ölçüm → 1 compositor kök (aynı
  pikseller) → 2 katmanlar + ilk compositor hareketleri → 3 WAM → 4 hareket dilinin kalanı** (+ bağımsız DPI cilası).
  Toplam tahmin: ~9,5–13 ajan-günü + kullanıcı onay turları.
- Tasarım gerilimi: D-109 referans aracı birebir istiyor ve referans araçta hareket yok. Bu yüzden hareket dili
  **durgun kareyi hiç değiştirmez** (render_diff 0 fark), yalnız geçişlere kısa, küçük hareket ekler.

---

## 1. Bugünkü durum (kanıtlarla)

### 1.1 Ne hareket ediyor, nasıl

Mekanizma: `Tween` (`src/ui/anim/Tween.cpp:93-126`, duvar saati tabanlı, CSS cubic-bezier, `nowMs()` = QPC) ve
`ItemMotion` (`ItemHover`, `SelectionMove`: `src/ui/anim/ItemMotion.cpp`). Widget `animate()` ile `Host::m_animating`'e
girer; `Host::paint` her karede `tick(now)` çağırır (`src/ui/widget/Host.cpp:177-197`). Hepsi **çizilerek** (D2D) oynar.

| Kullanan | Ne | Süre / eğri | Kanıt |
|---|---|---|---|
| `Button` | hover dolgusu, basma | fast 80 / `kPressReleaseMs` | `ui/widgets/Button.cpp:70-83` |
| `Toggle` | topuz kayar | base 140 | `ui/widgets/Toggle.cpp:23,75` |
| `CheckField`, `TableView` | tik kendini çizer | base 140 (`kRevealMs`) | `Checkbox.cpp:94-110`, `TableView.cpp:132-146` |
| `TableView`, `TabBar`, `RadioGroup`, `PillGroup`, `CardTabs`, `StatusBar`, `RecentList` | öğe hover'ı söner-yanar (`ItemHover`) | fast 80 | `TabBar.cpp:209,376`, `PillGroup.cpp:156`, `PageView.cpp:133`, `StatusBar.cpp:210`, `RecentList.cpp:227` |
| `TabBar`, `RadioGroup`, `PillGroup`, `NavPanel` | seçim göstergesi kayar (`SelectionMove`); gezintide rozet rengi de karışır | base 140 | `ItemMotion.cpp:46-67`, `NavPanel.cpp:290-315` |
| `ScrollBar` | genişler | base 140 | `ScrollBar.cpp:59` |
| `DropZone`, `DropRow`, `CaptionButton` | hover | fast 80 | `DropZone.cpp:35`, `DropRow.cpp:52`, `TitleBar.cpp:155` |
| `Toast` | 8 DIP yükselerek belirir (`paintOpacity` + kayma) | enter 180, decelerate | `Toast.cpp:55-58,101-105`, `Toast.h:35` |
| `PageView` | yeni sayfa (şerit + ipucu + kart) solarak gelir (`paintOpacity`) | enter 180, decelerate | `PageView.cpp:210,333-337`, `PageView.h:121-123` |
| Yavaş tikler (`tickIntervalMs`) | iş şeridi spinner'ı + ETA | 100 ms (`OperationStrip`, `FetchStrip`, `LanguagesPage`, `UpdatesPage`), 250 ms (`DownloadPage`) | `OperationStrip.h:23`, `FetchStrip.h:28`, `LanguagesPage.cpp:59`, `UpdatesPage.cpp:42`, `DownloadPage.h:50` |

Spinner 8 adımlı: her 100 ms'de 45° (`FetchStrip.cpp:46-48`) → saniyede 10 kare, **her biri bütün pencere**.

**Hiç hareket etmeyenler:** modallar (Dialog, MenuPopup / açılır listeler, komut paleti) ve scrim anında açılıp
kapanır (`Host::pushModal/popModal`, `Host.cpp:66-100`; `popModal` widget'ı hemen yok eder). Araç ipucu anında
(`Host.cpp:479-485`). Bildirimin kapanışı anında (`Toast::hide` = `setVisible(false)`, `Toast.cpp:93-95`). Gezinti
daralması anında (`Shell::setNavCollapsed` `animated` parametresini yok sayıyor, `Shell.cpp:4442-4447`). Tema değişimi,
kaydırma, ilerleme çubuğu (oran atlar), ağaç aç / kapa anında.

Token'lar: `motion.fast 80 / base 140 / slow 220 / enter 180 / exit 120`, `standard (0.2,0,0,1)`, `decelerate
(0,0,0,1)`, `accelerate (0.4,0,1,1)`; kullanılmayan bir `spring {380, 34, 1}` (`ui/generated/Tokens.g.h:454-465`).
`zorder` token'larında bildirim (300) diyaloğun (210) üstünde; kodda bildirim bir Shell çocuğu, yani modalların
**altında** çiziliyor (`Shell.cpp:3218-3222`, `Host.cpp:199-205`). Bu belge kodun davranışını korur.

### 1.2 Bir kare nasıl üretiliyor

1. `Widget::invalidate()` → `Host::requestFrame()` → `Window::invalidate()` → `InvalidateRect(hwnd, nullptr, FALSE)`
   (`Widget.cpp:130-134`, `Host.cpp:102-109`, `Window.cpp:145-152`). **Dikdörtgen yok: her istek bütün pencere.**
2. `WM_PAINT` → `BeginPaint/EndPaint` (bölge doğrulanır) → `App::paint()` (`Window.cpp:330-338`, `App.cpp:1921-1958`).
3. `canvas.clear(BgBase)` + `Host::paint`: tik döngüsü, **bütün ağaç**, her modal için scrim + modal, odak halkası,
   araç ipucu (`Host.cpp:177-230`).
4. `EndDraw` + `Present(1, 0)` (`SwapChainTarget.cpp:94-98`). Zincir: `FLIP_SEQUENTIAL`, 2 tampon, `B8G8R8A8`,
   `ALPHA_MODE_IGNORE`, `SCALING_NONE` (`SwapChainTarget.cpp:11,20-31`). Bekleme nesnesi yok,
   `SetMaximumFrameLatency` yok; `DXGI_STATUS_OCCLUDED` yok sayılıyor (AUDIT D5).
5. Animasyon sürerken: aralık 0 ise yeni `requestFrame` (tempo = `Present(1)`'in dikey eşitleme beklemesi), değilse
   tek atımlık `kFrameTimer` (`Host.cpp:231-242,465-471`). Hiçbir şey sürmüyorsa kare yok: **boşta %0 CPU** (ölçüldü,
   PERF-BASELINE).
6. Boyutlama: `WM_SIZE` → `resized` → `ResizeBuffers` + `layout` + **eşzamanlı** `paint()` (`App.cpp:1709-1727`);
   büyütmede olası çift `Present` (AUDIT APP-21).

Ek maliyet noktaları **[kod]**:
- `paintOpacity()` < 1 olan widget `Canvas::pushOpacity(own)`'u **sınırsız** açar → katman kırpma kadar, yani
  pencere boyutunda (`Widget.cpp:181-184`, `Canvas.cpp:444-448`). PageView ve Toast'un her solma karesi bunu öder.
- `Canvas::dropShadow` her çizimde bir komut listesi + katman başına bir D2D gölge (Gaussian) efekti kurar
  (`Canvas.cpp:305-330`; AUDIT D4). Bildirim, diyalog, menü ve araç ipucu her karede gölgelerini yeniden bulanıklaştırır.
- Zaman, `Host::paint`'in başında örneklenir (`Host.cpp:181`), karenin ekrana çıkacağı an değil; kuyrukta 3'e kadar
  kare olabilir **[API: DXGI varsayılan gecikme 3]** → hareket zamanı titrer, girdi gecikmesi +1–2 kare.

### 1.3 UI thread takılınca

Her hareket duvar saatine bağlı ve yalnız `WM_PAINT`'te ilerler. Takılma süresince **hiçbir şey hareket etmez**;
takılma bitince ilk karede hareket geçen süre kadar ileri **atlar**. Bilinen takılma kaynakları: sayfa kurulumu
(`Shell::showPage` gövdeyi eşzamanlı kurar, `Shell.cpp:2377-3228`), uzun form yerleşimi (AUDIT UI-02), çizimde dosya
simgesi yükleme (SUB-03), arayüz iş parçacığında `queue.json` yazımı (APP-05 / CTL-11, STATUS "Kalan"). `WM_PAINT`
düşük öncelikli olduğundan motorun `post` iletileri de kareyi geciktirir (`Window.cpp:463-493`).

Somut sonucu: **sayfa solmasının saati, sayfa gövdesi kurulmadan başlıyor.** `PageView` kurucusu `m_enter.animateTo`'yu
çağırıyor (`PageView.cpp:210`), gövde ondan sonra `setBody<…>` ile kuruluyor (`Shell.cpp:2432-3215`), ilk kare
`showPage` döndükten sonra geliyor. Gövde kurulumu + ilk kare 180 ms'yi geçerse sayfa solmadan, tam opak belirir;
geçmezse solmanın başı kesilir. **[ölç]** hangi sayfalarda olduğu.

### 1.4 DPI

- Manifest: `<dpiAwareness>PerMonitorV2</dpiAwareness>` (`resources/WinLove.manifest:19`). Kod içinde
  `SetProcessDpiAwarenessContext` yok, gerek de yok.
- `WM_DPICHANGED` işleniyor: önerilen dikdörtgen uygulanıyor, `resized` çağrılıyor (`Window.cpp:353-364`).
  `GetDpiForWindow`, `GetSystemMetricsForDpi` kullanılıyor (`Window.cpp:79,211-214,268,367`).
- Ölçek = DPI/96 × yakınlaştırma; yakınlaştırma = yoğunluk × Windows metin boyutu (`App.cpp:90-99`,
  `Window.cpp:114-129`); D2D bağlamının DPI'ına girer (`SwapChainTarget.cpp:68-75`).
- Eksikler (hepsi küçük): `WM_GETDPISCALEDSIZE` yok (monitör değişince istemci alanının DIP boyutu çerçeve yuvarlaması
  kadar kayar); `WM_DPICHANGED`'de `SetWindowPos` zaten `WM_SIZE` → `resized` üretiyor, ardından ikinci bir `resized`
  (`Window.cpp:357-362`: aynı karede iki yeniden boyutlama + iki çizim); pencere simgesi yalnız sınıfa `LoadIconW` ile
  (`Resources.cpp:80-82`, sistem DPI'ında 32 px, küçük simge / `WM_SETICON` yok); çift tık aralığı
  `GetSystemMetrics(SM_CXDOUBLECLK)` = sistem DPI'ında piksel, DIP gibi kullanılıyor (`Host.cpp:373-374`); ilk
  pencere boyutu çerçeve dahil hesaplanıyor (istenen DIP'ten 2 × çerçeve kadar dar istemci, `Window.cpp:96-98`).
  `WM_GETMINMAXINFO`'daki çerçeve hesabı kendi `WM_NCCALCSIZE`'ımıza uyuyor (`Window.cpp:365-381`), dokunmaya gerek yok.

### 1.5 Aygıt kaybı

`deviceLost()` kodları (`App.cpp:47-57`) → `recreateGraphics()`: hedef bırakılır, `Canvas::forgetFileIcons()`,
`RenderDevice::recreateDevice()` (fabrikalar kalır), yeni HWND swap chain (`App.cpp:1938-1977`). Varsayım: **bir HWND'de
aynı anda tek flip zinciri**; eskisi tamamen bırakılmadan (`SetTarget(nullptr)` + `ClearState` + `Flush`,
`SwapChainTarget.cpp:47-63`) yenisi `E_ACCESSDENIED` alır (ENGINE.md saha notu 2026-10-10). Canvas kurtarmadan önce kapsam
dışına çıkar (`App.cpp:1925-1933`). `--test-device-lost` gerçek bir `Present`'ten sonra kaybı enjekte eder
(`App.cpp:1822,1935-1937`). WARP yedeği `RenderDevice.cpp:22-24,43-45`.

### 1.6 Pencere ve çerçeve

`WM_NCCALCSIZE` ile başlık istemci alanında (`Window.cpp:296-312`), `WM_NCHITTEST` Snap Layouts için, DWM yuvarlak köşe
+ kenar rengi + koyu çerçeve (`Window.cpp:82-86,201-205`), `WM_ERASEBKGND` `bg.base` ile boyar (`Window.cpp:323-329`).
`WS_EX_NOREDIRECTIONBITMAP` yok (`Window.cpp:73`). İlk kare `ShowWindow` + `UpdateWindow`'dan gelir (`Window.cpp:131-134`).
UI thread STA (`OleInitialize`, `main.cpp:58-59`) — WAM için uygun. D2D fabrikası `SINGLE_THREADED`
(`RenderDevice.cpp:57`), metin gri tonlu AA (`RenderDevice.cpp:78`). `wl_ui` dcomp'a bağlanmıyor (`ui/CMakeLists.txt:43-45`).

### 1.7 Hareketi azalt

`SPI_GETCLIENTAREAANIMATION` (`Tween.cpp:41-45`), uygulama ayarı "Hareketi azalt" = her zaman azalt
(`setReducedMotionForced`, `App.cpp:502,1871`), `WM_SETTINGCHANGE`'te yenilenir (`App.cpp:1777`), `--render`
`forceInstantMotion` (`App.cpp:601`). Azaltılmışta `Tween::animateTo` hedefe atlar (`Tween.cpp:97-99`); spinner durur
(`FetchStrip.cpp:48`). `ItemMotion` anında son durumu çizer (D-133: durgun kare eskisiyle aynı).

### 1.8 Ölçülmeyenler ve belge tutarsızlıkları

- Kare süresi / kare aralığı hiç ölçülmedi (PERF-BASELINE.md:23; D-133 Sonuç; STATUS "Doğrulanmayan").
  D-133'ün hareketleri canlı pencerede hiç izlenmedi.
- ARCHITECTURE.md:87 "yalnızca kirli bölge çizilir → `Present1` dirty rect ile" — **kodda yok**.
- CLAUDE.md "Arayüz: Direct2D + DirectWrite + DirectComposition" ve D-001 — DComp **hiç kullanılmıyor** (D-011).
- ROADMAP 1.2 / 1.4 satırları ve STATUS "Faz 4: DComp/dirty-rect (D-011)" bekleyen iş olarak duruyor.

---

## 2. Üç teknoloji burada gerçekte ne kazandırır

| | Kazandırdığı | Bedeli / riski | Hüküm |
|---|---|---|---|
| **DirectComposition** | Görünürlük, saydamlık, dönüşüm ve kırpma animasyonları DWM'de, ekranın hızında, UI thread meşgulken bile; yüzen yüzeylerin animasyonu arka pencereyi her karede yeniden çizmeden; kapanış animasyonu widget yok edildikten sonra son piksellerle; diyalog açıkken hover yalnız diyalog yüzeyini çizer; boyutlamada açılan alan sayfa renginde; bekleme nesnesiyle daha düşük gecikme | İkinci bir sunum yolu; aygıt kaybında pencere başına tek hedef; DWM'ye bağımlılık (WinPE / DWM yok → yedek yol); `PrintWindow` davranışı; Present ile Commit'in ayrı karelere düşebilmesi; bellek | **Asıl kazanç burada.** Dar tutulursa (≈6 görsel türü) risk yönetilebilir |
| **Windows Animation Manager** | Kesilen geçişte hız sürekliliği (yeni geçiş eskisinin anlık hızından başlar), storyboard / anahtar kare, öncelikli çakışma çözümü, `GetCurve` ile eğrinin olduğu gibi DComp'a aktarılması | COM ayrıntısı; token'ların CSS bezier'i ile WAM geçiş modelinin uyumu **[API]**; `GetCurve` zaman tabanının hizalanması **[API]**; 80–220 ms'lik hareketlerde gözle görülür fark küçük | **Dar kapsamda**: compositor eğrileri + kesilen yüzey hareketleri. Widget tween'lerini WAM'a taşımak ölçümle karar |
| **DPI Awareness API** | Zaten PMv2; kalan: monitörler arasında DIP boyutunun korunması, tek yeniden boyutlama, net görev çubuğu simgesi, doğru çift tık aralığı | Yok denecek kadar az | **Cila**, ayrı küçük aşama |

Dürüst not: akıcılığı bugün en çok bozan şey büyük olasılıkla hareket mekanizması değil, UI thread'in takılmaları ve
tam pencere çizimidir **[ölç]**. DComp UI thread'i hızlandırmaz; **geçişleri** ondan bağımsız kılar. Aşama 0 bunu
ayırmak için var.

---

## 3. DirectComposition mimarisi (bu uygulama için)

### 3.1 Görsel ağacı

```
IDCompositionTarget (HWND, topmost = TRUE)            ← pencere başına bir tane
└─ Root (IDCompositionVisual3)
   ├─ Background     1×1 yüzey, bg.base, en yakın komşu ölçekli → boyutlamada açılan alan sayfa rengi
   ├─ Content        içerik = compositor swap chain (bugünkü bütün çizim; satır içi her şey)
   ├─ Transitions    geçici: sayfa / tema / liste anlık görüntüsü (IDCompositionSurface) — saydamlık 1→0
   ├─ Floating       bildirim (yüzey), araç ipucu (yüzey)              ← kodda modalların altında
   └─ Modals         modal başına: Scrim (1×1 yüzey, ölçekli) + Panel (yüzey: diyalog / menü / açılır liste / palet)
                     + kapanmakta olanlar (widget'ı yok, son pikselleri oynuyor)
   (Aşama 4) Embedded  iş şeridi spinner'ı gibi küçük gömülü görseller (Content'in üstünde, kendi kırpmasıyla)
```

Sıra bugünkü çizim sırasıyla aynı (`Host.cpp:199-230`): kök, bildirim (Shell çocuğu, en önde), modallar sırayla, araç
ipucu en üstte. Odak halkası, odaklı widget hangi katmandaysa o katmanın yüzeyine çizilir (bugün en son çiziliyor,
`Host.cpp:207-227`; katmanlı düzende kök yüzeye çizilirse modalın altında kalırdı).

**Görsel olmayanlar (bilerek):** butonlar, satırlar, sekmeler, gezinti hapı, anahtar, tik, kaydırma çubuğu, ilerleme
dolgusu. Gerekçe: bunlar başka içerikle iç içe çiziliyor (gezinti hapının üstündeki etiket ve rozet rengi hapla birlikte
karışıyor, `NavPanel.cpp:290-306`); görsel yapmak gezinti kartını üç yüzeye (kart / hap / içerik, saydam) bölmek demek.
Küçük alanlı, kısa hareketler; kazanç ölçümle gösterilmedikçe değmez.

### 3.2 Kök: compositor swap chain mi, yüzey mi?

| | Compositor swap chain (`CreateSwapChainForComposition`) | `IDCompositionSurface` |
|---|---|---|
| Bugünkü koda yakınlık | `SwapChainTarget`'ın ikizi; `ResizeBuffers`, `Present`, aygıt kaybı aynı | Yeni çizim yolu (`BeginDraw` atlas ofseti) |
| Tempo | `Present(1)` + bekleme nesnesi: hazır | Kendi temposunu kurmak gerek (Win10'da `WaitForCommitCompletion` / `DwmFlush`, Win11'de `DCompositionWaitForCompositorClock`) **[API]** |
| Ölçüm | `GetFrameStatistics` (PresentCount, PresentRefreshCount, SyncQPCTime) | Yalnız DComp / DWM istatistikleri |
| Görsel değişiklikle eşzamanlılık | **Yok**: Present ile Commit ayrı karelere düşebilir | Commit ile atomik |
| İleride kısmi güncelleme | Flip-model dirty rect, içerik koruma kuralları belirsiz **[API]** | `BeginDraw(updateRect)`: dışı korunur **[API]** |

Karar: **kök = compositor swap chain** (Aşama 1, en az risk, ölçüm hazır). Katmanlar ve geçiş görüntüleri = yüzey
(Commit ile atomik, küçük, nadiren güncellenir). Kökün yüzeye taşınması ancak ölçüm tam pencere çiziminin bütçeyi
aştığını gösterirse (koşullu Aşama 5).

Zincir: `Scaling = DXGI_SCALING_STRETCH` (compositor zincirinde zorunlu **[API]**), `FLIP_SEQUENTIAL`, 2 tampon,
`ALPHA_MODE_IGNORE` (opak, bugünkü gibi), `DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT` + `SetMaximumFrameLatency(1)`.
`App::paint` `BeginDraw`'dan önce bekleme nesnesini bekler (zaman aşımlı) ve hareket zamanını **beklemeden sonra**
örnekler: zaman karenin ekrana çıkacağı ana yaklaşır, `Present` artık bloklamaz (AUDIT D5'in yarısı).

Cihaz: `DCompositionCreateDevice3(d2dDevice, IID_IDCompositionDesktopDevice)` — D2D aygıtı verilirse
`IDCompositionSurface::BeginDraw` doğrudan `ID2D1DeviceContext` döndürür **[API]**; `IDCompositionDevice3` QI ile
(Visual3: `SetOpacity`, 4×4 dönüşüm). `dcomp.dll` **gecikmeli yüklenir** (`/DELAYLOAD` veya `GetProcAddress`): yoksa
(DWM'siz ortam) süreç yine açılır, eski yola düşer.

### 3.3 Katmanlar (yüzen yüzeyler)

`ui`'de yeni kavram, `Host` içinde yönetilir (katman kuralı korunur: `ui` sayfa bilmez):
- **Hangi widget katman:** her modal (otomatik, `pushModal`), `Widget::compositionLayer()` true dönen widget (Toast),
  Host'un kendi araç ipucu.
- **Yüzey boyutu:** modal pencereyi kaplar (girdi kapanı) ama boyadığı yer küçük → yeni sanal
  `Widget::surfaceExtent()` (varsayılan `bounds`); Dialog = `m_box` + gölge payı (dialog elevation: 40 bulanıklık +
  16 kayma ≈ 56 DIP), MenuPopup = `m_panel` + 32, Toast = bounds + 32.
- **Kirli takibi:** `Widget::invalidate()` en yakın katman köküne kadar ebeveynleri gezer → `Host::requestFrame(layer)`.
  Katman kirliyse yalnız onun yüzeyi çizilir; kök kirli değilse kök hiç çizilmez ve **Present yapılmaz**. Diyalog
  açıkken buton hover'ı = yalnız diyalog yüzeyi (bugün: bütün pencere + scrim + iki Gaussian gölge).
- **Piksel ızgarası:** yüzey kökeni tam sayı piksele yuvarlanır, alt ağaç `translate(−köken/ölçek)` ile çizilir; tam
  sayı piksel kaydırma `hairline` yaslamasını değiştirmez → aynı pikseller. `BeginDraw`'ın döndürdüğü atlas ofseti
  dönüşüme eklenir; her `BeginDraw`'da `SetDpi(ölçek×96)` ve gri tonlu AA yeniden kurulur (DComp'un verdiği bağlam
  bizim `createContext` ayarlarımızı taşımaz **[API]**). Biçim `B8G8R8A8`, `PREMULTIPLIED`.
- **Kapanış:** `popModal` / `Toast::hide` widget'ı bugünkü gibi hemen yok eder (çağıranlar buna güveniyor:
  "the fields are gone from here on", `Shell.cpp:1350`). Görsel ise son pikselleriyle "emekli" listesine geçer, çıkış
  animasyonu kurulur, bitiş zamanında tek atımlık zamanlayıcı onu siler. **Arada UI thread hiç kare çizmez.**
- **Scrim:** 1×1 yüzey `Scrim` rengiyle, `NEAREST_NEIGHBOR` ölçekli; saydamlığı animasyonlu. Satır içi yolda scrim
  bugünkü gibi `fillRect`.
- **Satır içi yol** (render, HWND anahtarı, DComp kurulamadığında): `Host::paint` bugünkü kodu aynen çalıştırır; katman
  kavramı yok sayılır. `--render` tanım gereği değişmez.

### 3.4 Geçiş anlık görüntüsü (sayfa, sekme, tema, yüklenen liste)

Tek genel API: `Host::crossfade(RectF region, std::span<const RectF> holes, Motion)`:
1. Bölge, **şu anki** ağaçla bir yüzeye çizilir (bir ek çizim, birkaç ms **[ölç]**); `holes` saydam bırakılır
   (`PushAxisAlignedClip` + `Clear(0)`).
2. Görsel, köke tam opak eklenir ve Commit edilir. Görüntü eskiyle aynı olduğu için ne zaman görünürse görünsün fark
   edilmez.
3. Çağıran durumu değiştirir (eski sayfayı yok eder, yenisini kurar). Kök yeni kareyi sunar.
4. **Bir sonraki karede** (yeni kök karesinin `Present`'i döndükten sonra) saydamlık animasyonu kurulur ve Commit
   edilir. Present ile Commit eşzamanlı olmadığından (3.2) animasyon bilerek bir kare sonra başlar; eski görüntü o ana
   kadar tam opak örttüğü için atlama görünmez.
5. Bitişte görsel ve yüzey silinir.

Sayfa değişimi: `Shell::showPage`, `removeChild(m_pageView)`'dan önce `host()->crossfade(orta bölge, {gezinti kartı})`
çağırır. Delik gerekli: `PageView` orta bölgenin tamamını boyuyor ve gezinti kartı onun üstünde
(`Shell.cpp:4680,4694-4695`); gezinti hapı aynı anda kökte kaydığı için görüntü onu örtmemeli. Bugünkü `PageView`
solması (`paintOpacity`) compositor yolunda kapatılır; satır içi yolda kalır. Yan bonus: geçiş artık yeni sayfanın
**ilk sunulan karesinden** başlar (1.3'teki "kurulum solmayı yiyor" sorunu biter).

Opak içeriğin üstünde eski görüntünün 1→0 solması matematiksel olarak çapraz geçiştir (eski·a + yeni·(1−a)).

### 3.5 Compositor'da ne, çizimde ne

| Compositor (DComp animasyonu) | Çizim (Tween / ItemMotion, kök veya katman yüzeyi) |
|---|---|
| Modal / menü / bildirim / araç ipucu giriş-çıkış (saydamlık, kayma, diyalogda ölçek) | Hover, basma, seçim göstergesi, anahtar topuzu, tik |
| Scrim saydamlığı | Gezinti hapı + rozet renk karışımı |
| Sayfa / sekme / tema / liste çapraz geçişi | Ağaç oku dönüşü, ilerleme dolgusu |
| (Aşama 4) belirsiz spinner dönüşü | ETA metni, yüzde |

Kural: konumu ya da saydamlığı **bütün bir yüzeyin** değişiyorsa compositor; bir yüzeyin **içindeki** bir şey
değişiyorsa çizim.

### 3.6 Efektler ve maliyetleri **[API]**

| Özellik | Maliyet | Kullanım |
|---|---|---|
| Saydamlık (`IDCompositionVisual3::SetOpacity`, animasyonlu) | Çok düşük | Evet |
| 2D kayma (`SetOffsetX/Y`, animasyonlu) | Çok düşük; kesirli kaymada metin geçiş boyunca çift doğrusal örneklenir (hafif yumuşama), durunca tam sayı → keskin | Evet (≤ 8 DIP) |
| Ölçek (`IDCompositionScaleTransform`) | Düşük; geçiş boyunca metin yumuşar | Yalnız diyalog, 0,98→1 |
| Dikdörtgen / yuvarlak kırpma (`IDCompositionRectangleClip`) | Düşük | Gömülü görsellerin kaydırma kırpması (Aşama 4) |
| 3D dönüşüm, derinlik | Düşük ama gereksiz | Hayır |
| Filtre efektleri (`IDCompositionDevice3`: Gaussian bulanıklık, gölge, doygunluk…) | Görsel değiştikçe her compositor karesinde ara yüzeye çizim; alanla büyür; WARP / VM'de pahalı | **Hayır**: gölge D2D ile yüzeye bir kez pişirilir |
| Arka plan malzemeleri (Mica / Acrylic, `DWMWA_SYSTEMBACKDROP_TYPE`) | — | Kapsam dışı (D-109: opak `#202020`) |

### 3.7 Başlık çubuğu, DWM çerçevesi, yuvarlak köşeler

- `WM_NCCALCSIZE` / `WM_NCHITTEST` / Snap Layouts değişmez: DComp hedefi istemci alanını kaplar **[API: hedefin kökeni
  istemci alanının kökeni — doğrula]**.
- Windows 11 yuvarlak köşeleri ve `DWMWA_BORDER_COLOR` DComp içeriğine de uygulanır **[API, doğrula]**.
- Aşama 1'de **yeniden yönlendirme bit eşlemi kalır** (`WS_EX_NOREDIRECTIONBITMAP` yok): ilk Commit'ten önce GDI zemini
  (`bg.base`) görünür, HWND yolu ile aynı pencere stili kalır. Bedeli ~ istemci × 4 bayt (2560×1440'ta ~15 MB).
  `NOREDIRECTIONBITMAP` ancak `PrintWindow` ile denendikten sonra ayrı bir adım (pencere oluştururken seçilmeli, sonra
  değiştirilemez).
- Kök içeriğin ilk karesi `ShowWindow`'dan **önce** çizilip Commit edilir: DWM'nin kendi açılış animasyonu içerikle
  oynar.

### 3.8 Canlı boyutlandırma

Flip-model'in bilinen sorunu: pencere çerçevesi (DWM) ile içerik (uygulamanın sunumu) ayrı karelere düşer, içerik bir
kare geriden gelir; açılan şeritte eski / boş içerik görünür. Bugün `resized` eşzamanlı çizerek bunu azaltıyor
(`App.cpp:1726`). DComp ile:
- `Background` görseli pencereden bağımsız ve büyük: açılan alan **siyah / çöp değil `bg.base`** (en görünür iyileşme).
- Zincir 1:1 gösterilir (dönüşüm yok); `ResizeBuffers` + çizim + `Present` aynı.
- `APP-21`'in çift sunumu: `resized` içindeki çizimden sonra `ValidateRect` (iki yolda da).
- Kalan: sağa yaslı başlık düğmeleri sürüklerken bir kare geriden gelir (bugün de). Tam çözüm (çerçeve ile içeriğin
  aynı karede güncellenmesi) Windows'ta DComp ile bile garanti değil **[ölç: boyutlama senaryosu]**.

### 3.9 `PrintWindow`, araçlar, UI Automation

- `tools/capture_window.py`, `tools/gui.py shot`, `tools/capture_oobe.py` `PW_RENDERFULLCONTENT` (2) kullanıyor
  (`capture_window.py:24,48`, `gui.py:292`): bu bayrak DirectX / DComp içeriğini DWM'den alır **[API, Aşama 1'in kabul
  ölçütü]**. Compositor animasyonu sürerken yakalanan kare DWM'nin o anki hâlidir (Aşama 0'ın "takılma" denemesi bundan
  yararlanır).
- `--render` offscreen ve DComp'suz: değişmez, `tools/render_diff.py` her aşamada **0 fark** vermelidir.
- UI Automation HWND ve widget sınırlarına dayanır; DComp onu etkilemez. Compositor dönüşümü sürerken (≤ 220 ms)
  görsel konum ile UIA / isabet testi konumu farklıdır: isabet testi her zaman son yerleşimde (bilerek).

### 3.10 WARP, uzak masaüstü, GPU'suz, DWM'siz

- Kod donanım aygıtı kuramazsa WARP'a düşüyor (`RenderDevice.cpp:22-24`). DComp herhangi bir `IDXGIDevice` / D2D aygıtı
  kabul eder ve GPU'suz makinede DWM de WARP'ta çalışır **[API]**. Uygulama WARP'ta, DWM donanımdayken (bu PC'de
  `--force-warp`) yüzeylerin bağdaştırıcılar arası paylaşımı **doğrulanamadı** → Aşama 1'de iki durum da denenir: bu PC'de
  zorla WARP, Hyper-V VM'de (Temel Görüntü Bağdaştırıcısı) gerçek GPU'suz.
- Uzak masaüstü: DComp DWM üzerinden uzaklanır **[API]**; donanım aygıtı yoksa WARP durumuna döner. Animasyonlar bant
  genişliği harcar; Windows'un "animasyonları göster" ayarı RDP deneyim ayarına göre zaten kapanabiliyor
  (`SPI_GETCLIENTAREAANIMATION`). Ek kural (ör. `SM_REMOTESESSION`'da azalt) ölçülmeden konmaz. **Bu oturumda RDP
  denenemez** (istemci SKU'da kendine RDP kolay değil).
- DWM / `dcomp.dll` yok (WinPE, Server Core): gecikmeli yükleme + kendiliğinden HWND yoluna düşüş. WinLove.exe'nin WinPE'de
  çalıştığına dair bir belge bulamadım; D-009 yalnız exe'lerin taşınabilir olmasını söylüyor.

### 3.11 Aygıt kaybı (DComp ile)

Yeniden kurulacaklar: D3D/D2D aygıtı (bugünkü gibi), **DComp aygıtı** (D2D aygıtına bağlı), **hedef**, kök / arka plan /
içerik görselleri, compositor zinciri, bütün katman yüzeyleri (kirli işaretlenir, sonraki karede yeniden çizilir).
Kurallar:
- **Önce eski hedef tamamen bırakılır**: pencere başına bir DComp hedefi olabilir, eskisi yaşarken
  `CreateTargetForHwnd` `DCOMPOSITION_ERROR_WINDOW_ALREADY_COMPOSED` (0x88980800) döner **[API]** — flip zincirindeki
  `E_ACCESSDENIED` dersinin (ENGINE.md 2026-10-10) DComp karşılığı. Sıra: görseller / yüzeyler → hedef → DComp aygıtı →
  zincir → bağlam; sonra eski D3D bağlamında `ClearState` + `Flush` (bugünkü gibi).
- Compositor zinciri HWND'ye bağlı değil: `E_ACCESSDENIED` sınıfı sorun yalnız hedefte kalır.
- Süren compositor animasyonları ve emekli görseller düşürülür; katmanlar son durumla yeniden çizilir (atlamalı ama
  doğru).
- Algılama: `EndDraw` / `Present` kodları (bugünkü `deviceLost()`), ek olarak `Commit` hatası veya
  `IDCompositionDevice::CheckDeviceState(&valid)` = false → aynı yol **[API]**. DWM yeniden başlarsa DComp aygıtı geçersiz
  olur **[API]**: yalnız VM'de denenir (`dwm.exe`'yi kullanıcının oturumunda öldürmek yok).
- `--test-device-lost` iki yolda da; ek olarak "modal açıkken" ve "bildirim solarken" varyantları (Aşama 2).

### 3.12 Çok monitör / DPI değişimi

DComp piksel uzayında çalışır; ölçek (DPI × yakınlaştırma) D2D DPI'ında kalır. `WM_DPICHANGED`'de: zincir yeniden
boyutlanır, katman yüzeyleri yeni ölçekte yeniden çizilir, görsel ofsetleri yeniden hesaplanır, **süren compositor
animasyonları son değerlerine atlatılır** (eski ölçekte hesaplanmış piksel kaymaları yanlış olur). Yakınlaştırma asla
DComp ölçek dönüşümüyle yapılmaz (bulanık olur).

### 3.13 Ne gerileyebilir / riskler

1. Present ile Commit ayrı karelere düşebilir → geçiş başlangıcında bir karelik atlama (3.4'teki sıralama ile önlenir;
   ölçülmeli).
2. Satır içi ile katmanlı arasında ±1 seviye fark (gölgeler, scrim üstünde harmanlama): `--render` etkilenmez; canlı
   pencere karşılaştırması toleranslı yapılır.
3. Kaçırılan bir `invalidate` → bayat katman (bugün her istek bütün pencereyi çizdiği için bu hata sınıfı yok).
4. Aygıt kaybı / DWM yeniden başlaması yolunun genişlemesi.
5. WARP / VM / RDP'de DComp'un davranışı ve hızı.
6. Bellek: katman yüzeyleri + geçiş görüntüsü (tema geçişinde geçici tam pencere ≈ 15 MB, 2560×1440).
7. `Host`, `Shell`, `App` büyük ve başka ajanlarca düzenleniyor: birleştirme riski (Aşama 2'yi bu dosyalar boştayken yap).
8. Tasarım: referans araçta hareket yok (D-109); kullanıcı D-133'ten sonra "değişmemiş gibi" dedi ve daha cesur bir yön
   bekliyor. Hareket bu yön kararından bağımsızdır ama onu ikame etmez.

---

## 4. Windows Animation Manager

### 4.1 Ne sunar, bizde ne eksik **[API]**

- `IUIAnimationManager2` + `IUIAnimationTransitionLibrary2`: değişken (`IUIAnimationVariable2`), storyboard (anahtar
  kareler, birden çok değişken), zamanlama (`Schedule`), `Update(saniye)`, durum (`GetStatus`: IDLE / BUSY).
- Geçişler: accelerate-decelerate, cubic, smooth stop, parabolic, sinusoidal, linear, discrete, instantaneous,
  ve (beklenen) `CreateCubicBezierLinearTransition(süre, son, x1, y1, x2, y2)` — **adı ve hız davranışı SDK başlığında
  doğrulanmalı**.
- Kesilen geçiş: bir değişken hareket hâlindeyken yeni storyboard zamanlanırsa öncelik karşılaştırıcıları
  (`SetTrimPriorityComparison`, `SetCancelPriorityComparison` …) eskisini kırpar/iptal eder; başlangıç hızını hesaba
  katan geçişler (cubic, smooth stop, parabolic) **anlık hızdan** devam eder. Karşılaştırıcı kurulmazsa yeni storyboard
  ertelenebilir **[API, doğrula]**.
- `IUIAnimationVariable2::GetCurve(IDCompositionAnimation*)`: değişkenin eğrisini DComp animasyon nesnesine
  polinom parçalarıyla yazar → eğri compositor'da birebir oynar.
- `IUIAnimationTimer` (isteğe bağlı): `SetTimerUpdateHandler(manager, UI_ANIMATION_IDLE_BEHAVIOR_DISABLE)` ile boşta
  kendini kapatır.

Bizim `Tween`'de eksik olan: kesilince hız sürekliliği (`animateTo` mevcut değerden **sıfır zamanla** yeni bezier
başlatır, `Tween.cpp:101-106`), çok değişkenli storyboard, DComp'a eğri aktarımı. 80 ms hover'da hız sıçraması
algılanmaz; 140–220 ms kayma / yüzeylerde, hızlı art arda tıklamada (bildirim değişir, menü açılırken kapanır) fark eder.

### 4.2 Benimseme: her widget'ı yeniden yazmadan

1. **Önce compositor eğrileri (Aşama 3'ün çekirdeği).** `ui::anim::SurfaceMotion` (Aşama 2'de kendi eğrimizle gelir)
   eğriyi WAM'dan alır: değişken `from`'da, geçiş = token bezier'i (CubicBezierLinear varsa) yoksa en yakın
   accelerate-decelerate, `Schedule(t0)`, `GetCurve(anim)`, `visual->SetOpacity(anim)`, Commit. Aynı yüzeyin özelliği
   tekrar hareket ederse aynı değişken yeniden zamanlanır → kırpma + hız sürekliliği.
2. **Zaman tabanı.** `t0` = `IDCompositionDevice::GetFrameStatistics().nextEstimatedFrameTime / timeFrequency`; manager
   aynı saatle `Update` edilir. `GetCurve`'ün t=0'ı ile DComp'un "özellik Commit edildiği an" t=0'ının nasıl hizalandığı
   **bu makinede küçük bir deneyle doğrulanmalı** (2 s'lik doğrusal eğri, `PrintWindow` örnekleri).
3. **Widget tween'leri (isteğe bağlı, ölçümle).** `Tween` / `ItemHover` / `SelectionMove` API'si aynı kalır; arkası WAM
   değişkenine geçirilebilir (süreç başına bir manager, tween başına tembel bir değişken, kare başına tek `Update`,
   `value()` = `GetValue`). Varsayılan **kendi `Tween`'imiz kalır**; WAM arkası yalnız sık kesilenlerde
   (`SelectionMove`, `ItemHover`) ve ancak tezgâh hız sıçramasının azaldığını gösterirse. Gerekçe: eğri modeli
   değişir (token bezier ≠ WAM geçişleri), `tests/ui`'deki tween beklentileri değişir, yüzlerce buton = yüzlerce COM
   nesnesi.

### 4.3 İş parçacığı, ömür, hatalar, boşta maliyet

- Manager UI thread'de, `OleInitialize`'dan sonra `CoCreateInstance(CLSID_UIAnimationManager2)` ile; yalnız o thread
  kullanır. `App` kapanırken `Shutdown()`, sonra bırakılır (`OleUninitialize`'dan önce).
- **WAM zamanlayıcısı kullanılmaz**: uygulamanın kare saati (bekleme nesnesi / `WM_PAINT`) zaten var; zamanlayıcının
  olaylarının hangi thread'den geldiği ve DWM ile ilişkisi ek belirsizlik **[API]**. Compositor eğrileri `GetCurve`'den
  sonra UI thread'e hiç ihtiyaç duymaz; manager yalnız zaten çizilen karelerde `Update` edilir. Manager IDLE ise hiçbir
  şey çalışmaz → **boşta %0 CPU korunur** (perf_measure.py ile her aşamada).
- Her `HRESULT` hatası bir kez loglanır ve o istek için kendi eğrimize düşülür; bir kare asla WAM yüzünden başarısız
  olmaz. `CoCreateInstance` başarısızsa (veya `WINLOVE_WAM=0`) süreç boyu kendi eğrimiz.
- Hareketi azalt / anında: WAM'a hiç gidilmez; son değer doğrudan yazılır (`CreateInstantaneousTransition` bile yok).

---

## 5. DPI: somut iş listesi

Uygulama zaten Per-Monitor-V2 (`WinLove.manifest:19`), `WM_DPICHANGED`'i doğru dikdörtgenle işliyor, `*ForDpi`
kullanıyor. "DPI Awareness API'yi kullan" burada şunlar demek:

| # | İş | Yer | Neden |
|---|---|---|---|
| 1 | `WM_GETDPISCALEDSIZE`: istemci alanının DIP boyutunu koruyan pencere boyutunu döndür (istemci px = DIP × yeni ölçek + yeni DPI'daki yan/alt çerçeve; başlık yok, kendi `WM_NCCALCSIZE`'ımız) | `Window.cpp` | Monitör değişince yerleşim birebir aynı kalır; yoğunluk/yakınlaştırmayla birlikte doğrusal ölçeklemenin yuvarlama kayması biter |
| 2 | `WM_DPICHANGED`'de ikinci `resized` yalnız boyut değişmediyse (o zaman `WM_SIZE` gelmez) | `Window.cpp:357-362` | Aynı karede iki `ResizeBuffers` + iki çizim |
| 3 | `WM_SETICON` (küçük / büyük) `LoadIconWithScaleDown` ile, `GetSystemMetricsForDpi(SM_CXSMICON/SM_CXICON, dpi)` boyutunda; açılışta ve DPI değişiminde | `Window.cpp`, `Resources.cpp:80-82` | %150–175'te net görev çubuğu / Alt+Tab simgesi |
| 4 | Çift tık aralığı `GetSystemMetricsForDpi(SM_CXDOUBLECLK, dpi) / ölçek` | `Host.cpp:373-374` (Host DPI bilmiyor: değer `HostServices`'ten) | Sistem DPI'ı ≠ pencere DPI'ı olduğunda yanlış eşik |
| 5 | İlk boyut istemci alanı olarak (yan/alt çerçeve eklenerek) | `Window.cpp:96-98` | `--size=1440x900` istemci alanı tam o olur (yakalama karşılaştırmaları) |
| 6 | DPI değişiminde compositor animasyonlarını son değere atlat, katmanları yeniden çiz | Aşama 2 ile | 3.12 |
| 7 | Yakınlaştırma yalnız D2D DPI'ında; DComp'a ölçek olarak asla | ilke | Bulanıklık |

Gerekmeyenler: `SetThreadDpiAwarenessContext` (tek UI thread, süreç PMv2; sistem diyalogları kendileri PMv2),
`EnableNonClientDpiScaling` (yalnız PMv1 için), `AdjustWindowRectExForDpi` (başlığı olan çerçeve varsayar; bizim
`WM_NCCALCSIZE`'ımızla `WM_GETMINMAXINFO` hesabı zaten doğru).

Doğrulama: boyut hesabı saf bir fonksiyon → birim testi. Gerçek DPI değişimi: bu makinede farklı ölçekli ikinci bir
monitör var mı **bilinmiyor**; varsa `gui.py` ile pencere `SetWindowPos` ile oraya taşınıp yakalanır, yoksa yakınlaştırma
(yoğunluk ayarı) ile aynı yol kısmen sınanır.

---

## 6. Hareket dili

İlkeler (kısa ve az):
1. **Durgun kare değişmez.** Her hareketin son durumu bugünkü çizimdir (render_diff 0 fark).
2. Yalnız **saydamlık** ve **küçük kayma** (≤ 8 DIP); ölçek yalnız diyalogda (0,98). Dönüş, sıçrama, yay, sıralı giriş yok.
3. Giriş `decelerate`/`standard`, çıkış `accelerate` ve daha kısa. Hiçbir şey `slow 220`'den uzun değil.
4. **Girdi hiçbir hareketi beklemez:** giren yüzey hemen son yerinde isabet alır, çıkan yüzeyin widget'ı zaten yoktur.
5. Hareketi azalt / Windows "animasyonları göster" kapalı: hepsi anında (tek istisna: spinner 1 Hz adım).

| Olay | Ne hareket eder | Ne kadar | Süre / eğri | Nerede | Hareketi azalt |
|---|---|---|---|---|---|
| Sayfa değişimi (gezinti, Ctrl+1…9, sekme başlığındaki alt sayfalar) | Eski orta bölgenin görüntüsü (şerit + ipucu + kart + yan kart; gezinti kartı delik) söner; yenisi altında hazır | saydamlık 1→0 | exit 120, standard | Compositor (crossfade) | Anında |
| Sayfa içi sekme (TabBar, ör. diyaloglarda) | Alt çizgi kayar (bugün); içerik bölgesi çapraz geçiş | alt çizgi: konum; içerik 1→0 | alt çizgi base 140 standard; içerik exit 120 standard | Alt çizgi çizim; içerik compositor | Anında |
| Gezinti seçimi | Hap yeni öğeye kayar, rozet renkleri yer değiştirir (bugün); hover söner-yanar | konum, renk | base 140 / fast 80, standard | Çizim (kök) | Anında |
| Bildirim girişi | Yüzey | saydamlık 0→1, y +8→0 DIP | enter 180, decelerate | Compositor | Anında |
| Bildirim çıkışı (süre doldu, eylem, yenisi geldi) | Emekli yüzey | saydamlık 1→0, y 0→+4 | exit 120, accelerate | Compositor | Anında |
| Bildirim değişimi / yığın | Bugün tek bildirim: eski çıkar, yeni aynı yerde girer (çakışan ~120 ms). Yığın olursa kalanlar yeni yerlerine kayar | y kayması | base 140, standard (WAM: kesilince hız sürekli) | Compositor | Anında |
| Diyalog + scrim (komut paleti dahil) | Scrim; panel | scrim 0→1; panel saydamlık 0→1, ölçek 0,98→1 (merkezden) | enter 180: scrim standard, panel decelerate | Compositor | Anında |
| Diyalog kapanışı | Emekli panel; scrim | 1→0 (ölçek yok) | exit 120, accelerate | Compositor | Anında |
| Menü / açılır liste / ≡ menüsü açılış | Panel | saydamlık 0→1, y −4→0 (yukarı açılırsa +4→0) | base 140, decelerate | Compositor | Anında |
| Menü kapanışı (seçim, dışarı tık, Esc) | Emekli panel | 1→0 | fast 80, accelerate | Compositor | Anında |
| Araç ipucu | Yüzey (400 ms gecikmeden sonra) | saydamlık 0→1 / 1→0 | fast 80, standard | Compositor | Anında |
| Ağaç düğümü aç / kapa | Yalnız ok döner; satırlar anında (36 bin satıra kadar sanal tablo; yükseklik animasyonu her karede yeniden yerleşim demek) | 0→90° | fast 80, standard | Çizim | Anında |
| Liste yüklenince (boş durum / spinner → satırlar) | Tablo bölgesi çapraz geçiş; satır satır giriş yok | 1→0 | exit 120, standard | Compositor (crossfade) | Anında |
| İlerleme çubuğu (oranlı) | Dolgu yeni orana yumuşak gider | genişlik | base 140, standard | Çizim | Atlar |
| İş şeridi spinner'ı (belirsiz) | Simge döner | 360°/1000 ms, sürekli | doğrusal, `AddRepeat` | Compositor (gömülü görsel, Aşama 4); şerit yalnız ETA için 1 Hz çizilir | 1 Hz adım (iş sürdüğü görülsün) |
| İşaret / anahtar | Tik kendini çizer, topuz kayar (bugün) | — | base 140, standard | Çizim | Anında |
| Sürükle-üzerinde | Bırakma alanı vurgusu (bugün) | renk | fast 80 | Çizim | Anında |
| Gezinti daralması (Ctrl+B) | Orta bölge + gezinti çapraz geçiş (genişlik animasyonu yok) | 1→0 | base 140, standard | Compositor (crossfade) | Anında |
| Tema / vurgu değişimi | Bütün istemci alanının görüntüsü söner | 1→0 | slow 220, standard | Compositor (crossfade, geçici tam pencere yüzeyi) | Anında |
| Pencere gösterimi | DWM'nin kendi açılış / büyütme animasyonu; ilk kare `ShowWindow`'dan önce Commit | — | DWM | DWM | DWM'nin kendi kuralı |
| Kaydırma (tekerlek) | Hareket yok (bugünkü gibi anında) | — | — | — | — |

---

## 7. Aşamalı plan

Ortak: her aşama Debug + Release + birim testleri + `./build.ps1 -Dist` ile biter; build sırasında lab çalışmaz
(bellek notu); ölçüm koşuları `build\lab\perf\bin\<etiket>` kopyalarından (perf_measure.py gibi), `--profile` ile
(kullanıcının son kullanılanlarına dokunmadan); görüntü yalnız `PrintWindow` / `--render`.

Anahtar (her aşamada geçerli, açılışta bir kez okunur, değişiklik yeniden başlatma ister):
`WINLOVE_COMPOSITION=0|1` ortam değişkeni > `--composition=off|on` > `settings.json` gizli `"composition"` anahtarı >
varsayılan. DComp kurulamazsa kendiliğinden HWND yolu (loga bir satır). Ek alt anahtarlar: `WINLOVE_WAM=0`,
`WINLOVE_WARP=1` (zorla WARP).

### Aşama 0 — Ölçüm düzeneği (0,5–1 gün)

Amaç: "akıcı"yı sayıya çevirmek; önce sayıları HWND yolunda almak.
- **Yeni:** `src/ui/render/FrameLog.{h,cpp}` — sabit boyutlu halka tampon (karede ayırma yok), kapanışta CSV. Satır:
  QPC başlangıç, bekleme ms, tik ms, çizim ms (BeginDraw→EndDraw), Present ms, yol (hwnd/dcomp), kök çizildi mi, çizilen
  katman sayısı, Commit ms, animasyondaki widget sayısı, `GetFrameStatistics` (PresentCount, PresentRefreshCount,
  SyncQPCTime). Senaryo başında/sonunda `DwmGetCompositionTimingInfo(nullptr)` (genel DWM sayaçları: düşen / kaçan kare,
  yenileme hızı). DComp yolunda ayrıca `IDCompositionDevice::GetFrameStatistics`; Windows 11'de varsa
  `DCompositionGetStatistics` **[API]**.
- **Değişen:** `App.cpp` (`paint` ölçümü), `App.h` bayrakları: `--frame-log=<csv>` (pencereli), `--force-warp`,
  `--demo-job` (pencereli sahte iş: spinner + ilerleme, imaj yok), `--debug-stall-ms=N` (bir geçişin hemen ardından UI
  thread'i N ms uyutur), `--debug-toast` kısayolu (yalnız Debug: bildirimi tetiklemenin yan etkisiz yolu yok),
  `SwapChainTarget::frameStatistics()`.
- **Yeni araç:** `tools/motion_bench.py` (gui.py'nin girdi yöntemiyle: `SendMessageTimeoutW`, kullanıcının faresi
  kullanılmaz; zaman damgası `time.perf_counter` = QPC, uygulamanın `nowMs` saatiyle aynı). Senaryolar:
  boşta 5 sn (kare = 0) · gezinti üzerinde hover taraması (30 hareket, 16 ms arayla) · 10 sayfa değişimi (Ctrl+1…9,
  600 ms arayla) · komut paleti aç/kapa ×5 · ≡ menüsü aç/kapa ×5 · bildirim ×5 · Galeri sayfasında 30 tekerlek adımı ·
  canlı boyutlandırma (gui.py `resize` yolu, 60 adım × 20 px) · `--demo-job` 5 sn · tema ×4 (Ctrl+Shift+T) · takılma:
  `--debug-stall-ms=200` ile sayfa değişimi, sırasında 6 `PrintWindow` örneği ve bir yoklama pikselinin değeri.
- **Çıktı (senaryo başına):** kare sayısı; kare aralığının medyan / p95 / p99 / en büyük (ms) ve yenileme süresine oranı;
  "takılma" sayısı (animasyon sürerken aralık > 1,5 × yenileme süresi); kaçan dikey eşitleme (PresentRefreshCount
  sıçraması); kare başına UI thread süresi (medyan / p95); DWM düşen kare farkı; süreç CPU ms (`GetProcessTimes`);
  Private Bytes. Ayrıca her sayfa için **tek tam kare çizim süresi** (Aşama 5 kararı buna bağlı).
- **Kayıt:** HWND yolu × {GPU, WARP} × {%100 yakınlaştırma, yoğunlukla ~%175}; ekranın yenileme hızı not edilir.
  `docs/PERF-BASELINE.md`'ye "Hareket" bölümü, yöntem `docs/TESTING.md`'ye.
- **Kabul:** boşta 0 kare; iki koşu arasında medyan fark < %10 (gürültü bilinsin). Gözle değerlendirme yok.
- **Risk:** DWM sayaçları genel (başka pencerelerin hareketi karışır) → koşu sırasında başka şey hareket etmesin;
  PresentMon (ETW, yönetici) yalnız çapraz denetim için isteğe bağlı, ürüne girmez.

### Aşama 1 — Compositor kök, aynı pikseller (1,5–2 gün)

- **Yeni:** `src/ui/render/Compositor.{h,cpp}`: gecikmeli yüklenen `dcomp.dll`, `IDCompositionDesktopDevice`
  (+`IDCompositionDevice3`), hedef, Root / Background / Content görselleri, `commit()`, `checkDeviceState()`,
  `frameStatistics()`, `setBackgroundColor()`. `SwapChainTarget::createForComposition(...)` (3.2'deki tanım) +
  `waitForFrame(timeoutMs)`; HWND `create` olduğu gibi kalır.
- **Değişen:** `App.cpp`: yol seçimi + kendiliğinden düşüş; ilk kare + Commit `show`'dan önce; `paint`: bekle → zaman
  örnekle → çiz → Present (Commit yalnız görsel değiştiğinde); simge durumunda (`IsIconic`) kare yok, tik durur
  (AUDIT D5); `recreateGraphics`: 3.11 sırası; `resized` sonrası `ValidateRect` (APP-21). `applyTheme`: arka plan
  görselinin rengi. `ui/CMakeLists.txt`: `dcomp` + `delayimp` + `/DELAYLOAD:dcomp.dll`. `Window`: değişiklik yok
  (yeniden yönlendirme bit eşlemi kalır).
- **Doğrulama:** `render_diff.py eski yeni` → 0 fark; `capture_window.py` iki yolda → **aynı PNG** (fark varsa
  `PrintWindow`'un DComp davranışı incelenir: tüm doğrulama araçları buna dayanıyor); `gui.py` turu iki yolda;
  `--test-device-lost` iki yolda (log: "new device, same shell" + ardından kare); `--force-warp` iki yolda;
  `motion_bench` → hiçbir senaryoda kötüleşme yok, girdi→kare gecikmesi düşük gecikmeli bekleme ile azalmalı;
  boşta %0 CPU (perf_measure.py); bellek farkı kaydedilir. VM'de (GPU'suz) duman testi — lab sırası gelince.
- **Risk:** hedefin koordinat kökeni / yuvarlak köşeler; WARP'ta bağdaştırıcılar arası; `PrintWindow`.

### Aşama 1b — DPI cilası (0,5 gün, bağımsız; Aşama 1'den önce veya sonra)

- 5. bölümdeki 1–5 (6 Aşama 2 ile). Dosyalar: `Window.{h,cpp}`, `Resources.cpp`, `Host.{h,cpp}` (çift tık eşiği
  `HostServices`'ten), `App.cpp`.
- **Doğrulama:** boyut fonksiyonunun birim testi; `--size=1440x900` yakalamasında istemci tam 1440×900 DIP; varsa ikinci
  monitöre taşıma (gui.py), yoksa yoğunluk değişimiyle `setZoom` yolu.

### Aşama 2 — Katmanlar ve ilk compositor hareketleri (3–4 gün)

- **Yeni:** `src/ui/render/LayerSurface.{h,cpp}` (yüzey, gerektiğinde yeniden boyut, `BeginDraw` → bağlam + atlas
  ofseti + DPI + gri AA); `src/ui/anim/SurfaceMotion.{h,cpp}` (hazır hareketler 6. bölümdeki tablodan; token bezier →
  `IDCompositionAnimation`: 8–12 parçalı kübik Hermite uydurma `AddCubic` + `End`, saf uydurma fonksiyonu DComp'suz
  test edilir); `src/ui/widget/Layers.{h,cpp}` (Host'un katman tablosu, emekli listesi, crossfade görüntüleri).
- **Değişen:** `Host` (katman yönlendirmesi, kök/katman ayrı çizim, katman başına odak halkası, `popModal` → emekli,
  araç ipucu katmanı, `crossfade()`, emekli temizliği için tek atımlık `kLayerTimer`); `Widget` (`compositionLayer()`,
  `surfaceExtent()`, `invalidate()`'in katman kökü araması); `Dialog`, `MenuPopup` / `Dropdown`, `CommandPalette`
  (`surfaceExtent`); `Toast` (compositor yolunda `paintOpacity` ve kayma kapalı); `PageView` (compositor yolunda solma
  kapalı); `Shell::showPage` (`crossfade` çağrısı); `App::paint` (kök kirliyse çiz + Present; katmanlar; Commit).
- **Doğrulama:** birim testleri — eğri uydurma hatası < %0,5; modal içindeki `invalidate` kökü kirletmez; emekli
  görsel bitişte silinir; hareketi azaltta hiç animasyon nesnesi kurulmaz; satır içi yol bugünkü çizim sırasını verir.
  `render_diff` 0 fark. Canlı pencere: durmuş kareler HWND yoluyla ±1 seviye içinde (render_diff mantığına tolerans
  eklenmiş küçük bir karşılaştırma). `motion_bench`: diyalog / palet / menü / bildirim / sayfa senaryolarında
  compositor hareketi sırasında UI thread karesi ≈ 0 (yalnız başlangıç ve temizlik), DWM düşen kare 0; **takılma
  senaryosunda yoklama pikseli takılma boyunca ilerliyor** (HWND yolunda donuyor) — gözle değil, piksel değeriyle.
  `--test-device-lost` modal açıkken ve bildirim solarken. `--force-warp`.
- **Risk:** 3.13'teki 1–3; odak halkası / araç ipucu sırası; kapanan modalın emekli görüntüsünün altındaki yeni modal.

### Aşama 3 — Windows Animation Manager (1,5–2 gün; ilk 0,5 gün deneme)

- **Deneme (önce):** SDK başlığında adlar (`CLSID_UIAnimationManager2`, `CreateCubicBezierLinearTransition`,
  `GetCurve`, öncelik karşılaştırıcıları); `GetCurve` zaman tabanı (4.2-2); WAM eğrisi ile `evalBezier` örneklerinin
  farkı. Geçmezse: kayıt düşülür, WAM yalnız kesilen geçişlerde (accelerate-decelerate ile) ya da hiç kullanılmaz —
  dürüstçe D-135'e not.
- **Yeni:** `src/ui/anim/AnimationManager.{h,cpp}` (UI thread tekil, uygulama sürümlü `Update`, kırpma karşılaştırıcısı,
  `curve(from, to, ms, easing) → IDCompositionAnimation`, `finalValue`, `Shutdown`). `SurfaceMotion` WAM'ı kullanır,
  yoksa kendi uydurması. İsteğe bağlı: `SelectionMove` / `ItemHover` için WAM arkası (derleme anahtarı, varsayılan
  kapalı).
- **Değişen:** `App.cpp` (oluşturma / `Shutdown`), `ui/CMakeLists.txt` (gerekirse `uuid`).
- **Doğrulama:** birim testi (WAM eğrisi ≈ token bezier, tolerans); boşta manager IDLE, %0 CPU; `WINLOVE_WAM=0` ve
  CoCreate hatası benzetimi → kendi eğri; tezgâhta "kesilme" senaryosu (bildirim girerken yenisi, menü açılırken
  kapanış): kaydedilen eğri değerlerinde hız sıçraması yok.
- **Risk:** 4.1'deki belirsizlikler; ek COM karmaşıklığı. Kazanç küçükse Tween arkası hiç açılmaz.

### Aşama 4 — Hareket dilinin geri kalanı (2–3 gün)

- Gömülü görsel (`Host`, kökün üstünde, kaydırma kırpmasıyla; konumu her kök çiziminde güncellenir): iş şeridi
  spinner'ı compositor'da sürekli döner; `OperationStrip`, `FetchStrip`, `LanguagesPage`, `UpdatesPage`, `DownloadPage`
  `tickIntervalMs` 100/250 → 1000 (yalnız ETA). İş sürerken saniyede 10 tam pencere çizimi → 1.
- Oranlı ilerleme dolgusu için küçük yardımcı (`Canvas::progressBar` çağıranlar).
- Tema / vurgu / dil yeniden kurulumu: `App::applyTheme` ve `rebuildUi` öncesi tam pencere `crossfade`.
- Liste yüklenince `crossfade` (tablo bölgesi: sayfa kodundan çağrı), gezinti daralması `crossfade`
  (`Shell::setNavCollapsed`), ağaç oku dönüşü (`ComponentsPage` / tablo satırı çizimi), araç ipucu solması.
- **Doğrulama:** `--demo-job` senaryosunda kare sayısı ve CPU düşer, spinner DWM'de; tema senaryosunda geçici bellek
  ölçülür ve bırakıldığı görülür; render_diff 0 fark.

### Aşama 5 — Koşullu: kök için kısmi güncelleme

Yalnız Aşama 0 tam kare çiziminin hedef yenileme hızında bütçeyi aştığını gösterirse (ör. 144 Hz'de > 6 ms): kökü
`IDCompositionSurface`'e taşıyıp `BeginDraw(updateRect)` + `Widget::invalidate(rect)` + `paintTree` ayıklaması. Bu belge
bunu planlamıyor; ölçüm karar verir.

### Tahmin

| Aşama | Gün (ajan) |
|---|---|
| 0 Ölçüm | 0,5–1 |
| 1 Compositor kök | 1,5–2 |
| 1b DPI | 0,5 |
| 2 Katmanlar + ilk hareketler | 3–4 |
| 3 WAM | 1,5–2 |
| 4 Kalan hareket dili | 2–3 |
| **Toplam** | **~9,5–13** (+ kullanıcı onayı; Aşama 2'de büyük dosyalardaki birleştirme) |

---

## 8. Yapmayacaklarım (ve neden)

- **Her widget'ı görsel yapmak**: yüzlerce görsel, kirli takibi karmaşası, metin/dolgu iç içe çizimi; kazanç yok.
- **Compositor kaydırma / sanal yüzey / yumuşak kaydırma**: `IDCompositionVirtualSurface` + döşeme yönetimi; tablolar ve
  formlar yeniden çizime göre tasarlanmış (UI-02); referans araç yumuşak kaydırmıyor.
- **DComp filtre efektleri** (bulanıklık, gölge, doygunluk) ve **Mica / Acrylic**: her compositor karesinde GPU,
  WARP / VM'de pahalı, D-109'un opak görünümüyle çelişir.
- **Windows.UI.Composition (Visual Layer) / WinUI etkileşimi**: yaylar, örtük animasyonlar güçlü ama WinRT çalışma
  zamanı ve yeni bağımlılık (D-006); DComp yeterli.
- **Yay (spring) / sıçrama / sıralı (stagger) / 3D / paralaks**: kompakt profesyonel araç; `motion.spring` token'ı
  kullanılmadan kalır.
- **Düzen animasyonu** (gezinti genişliği, ağaç satır yüksekliği): her karede yeniden yerleşim; yerine çapraz geçiş.
- **WAM zamanlayıcısı** (`IUIAnimationTimer`): kendi kare saatimiz var; ek thread / belirsizlik.
- **Bütün `Tween`'leri WAM'a taşımak**: eğri modeli ve testler değişir, kazanç ölçülmeden.
- **Kendi pencere açılış / kapanış animasyonu**: DWM zaten yapıyor.
- **Kök için dirty rect** (bu planda): ölçüm gerektirmedikçe (Aşama 5).

---

## 9. Karar kaydı ve değişecek belge satırları

### 9.1 `docs/DECISIONS.md`'ye yapıştırılacak kayıt

(Yeni kayıtların yeri: dosyada en yeni kaydın üstü — şu an D-037'nin altı, D-133'ün üstü. Aşama 1 inene kadar başlığa
"TASLAK (uygulanmadı):" eklenebilir, D-130 / D-131 gibi.)

```markdown
## D-135 — Arayüz hareketi: DirectComposition katmanları, Windows Animation Manager eğrileri, DPI cilası (2026-11-10; D-011'i geçersiz kılar)
Bağlam: Kullanıcı (2026-11-10): "DirectComposition, Windows Animation Manager, DPI Awareness API da kullan akıcı güzel
animasyonları olsun arayüzün." D-011'den beri tek HWND flip swap chain'e her karede bütün pencere çiziliyor
(`Present(1, 0)`, kirli bölge yok; ARCHITECTURE §3'teki "Present1 dirty rect" hiç yazılmamıştı). Her hareket (`Tween`,
`ItemMotion`) arayüz iş parçacığında çiziliyor: o iş parçacığı takılınca (sayfa kurulumu, uzun form yerleşimi, çizimde
simge yükleme) duruyor, sonra atlıyor. Sayfa solmasının saati gövde kurulmadan başladığı için ağır sayfada solma
görünmüyor. Diyalog, menü, araç ipucu ve bildirimin kapanış hareketi yok (widget hemen yok ediliyor); gölgeler her
karede yeniden bulanıklaşıyor; iş sürerken spinner saniyede 10 kez bütün pencereyi çizdiriyor. DPI tarafı zaten
Per-Monitor-V2 (manifest, `WM_DPICHANGED`). Kare süresi bu projede hiç ölçülmedi (PERF-BASELINE, D-133).
Karar:
- **Önce ölçüm:** kare günlüğü (`--frame-log`) + betikli etkileşim (`tools/motion_bench.py`, gui.py'nin girdi yöntemi);
  sayılar (kare aralığı medyan / p95 / en büyük, takılma, kaçan dikey eşitleme, UI thread süresi, CPU, bellek) önce ve
  sonra PERF-BASELINE'a. Gözle karar yok.
- **Kök içerik compositor swap chain'i** (`CreateSwapChainForComposition`, bekleme nesnesi, `SetMaximumFrameLatency(1)`)
  küçük bir DirectComposition ağacında; altında `bg.base` renkli arka plan görseli (boyutlamada açılan alan sayfa
  rengi). `dcomp.dll` gecikmeli yüklenir; kurulamazsa ya da `WINLOVE_COMPOSITION=0` / `--composition=off` ise eski HWND
  yolu. `--render` değişmez (tek bit eşlem).
- **Yalnız yüzen yüzeyler kendi görselini alır:** modal (scrim + panel), menü / açılır liste, bildirim, araç ipucu; bir
  de geçiş görüntüleri (sayfa, sekme içeriği, tema, yüklenen liste). Hover, basma, seçim kayması, tik, anahtar,
  gezinti hapı çizilerek kalır.
- **Hareket dili:** yalnız saydamlık ve ≤ 8 DIP kayma (diyalogda 0,98 ölçek); mevcut motion token'ları; giriş
  decelerate/standard, çıkış accelerate ve kısa; en uzun `slow 220`. Kapanış, widget yok edildikten sonra son
  pikselleriyle oynar: girdi hiçbir hareketi beklemez. Durgun kare değişmez (D-109; `render_diff` 0 fark).
- **Windows Animation Manager** compositor eğrilerinin kaynağı (`IUIAnimationVariable2::GetCurve`) ve kesilen
  geçişlerde hız sürekliliği için; kendi zamanlayıcısı kullanılmaz, boşta hiçbir şey çalışmaz. Kurulamazsa
  (`WINLOVE_WAM=0`) token bezier'inden kendi parçalı eğrimiz. `Tween` API'si aynı kalır; widget tween'lerinin WAM'a
  geçmesi ölçümle ayrı karar.
- **DPI cilası:** `WM_GETDPISCALEDSIZE` (monitör değişince DIP boyutu aynı), DPI değişiminde tek yeniden boyutlama, DPI'ya
  göre pencere simgesi, çift tık eşiği `GetSystemMetricsForDpi`. Yakınlaştırma (yoğunluk × Windows metin boyutu) D2D
  DPI'ında kalır, DComp ölçeğine çevrilmez; DPI değişince süren compositor hareketleri son değerine atlar.
- **Hareketi azalt** (uygulama ayarı ya da Windows "animasyonları göster" kapalı): hiçbir compositor animasyonu kurulmaz;
  tek istisna iş spinner'ı 1 Hz adımla.
- **Yapılmayanlar:** widget başına görsel, compositor kaydırma, DComp efektleri, Mica / Acrylic, Windows.UI.Composition,
  yay / sıralı / 3D hareket, düzen animasyonu, WAM zamanlayıcısı, (ölçüm istemedikçe) kök için kısmi güncelleme.
Sonuç: D-001'in "DirectComposition"ı ilk kez gerçek olur. Aşamalar: 0 ölçüm → 1 compositor kök (aynı pikseller) → 2
katmanlar + sayfa / diyalog / menü / bildirim hareketleri → 3 WAM → 4 kalan (spinner compositor'da, ilerleme, tema,
liste) + bağımsız DPI cilası; her aşama anahtarla geri alınabilir ve render_diff 0 fark, `capture_window.py`, kare
tezgâhı, `--test-device-lost`, zorla WARP ile doğrulanır. Yeni riskler: Present ile Commit'in ayrı karelere düşmesi
(geçiş bir kare sonra başlar), aygıt kaybında pencere başına tek DComp hedefi (`DCOMPOSITION_ERROR_WINDOW_ALREADY_COMPOSED`;
eski hedef önce bırakılır), WARP / VM / uzak masaüstü, `PrintWindow`'un DComp içeriği, katman yüzeyleri ve tema geçişinin
geçici tam pencere görüntüsü kadar bellek. Hareket, bekleyen görsel yön kararının (kartlar / yüzeyler, tipografi /
yoğunluk) yerini tutmaz.
```

### 9.2 Değişecek satırlar

| Dosya | Bugün | Olacak |
|---|---|---|
| `docs/ARCHITECTURE.md:71` | "D3D11 (donanım, yoksa WARP) + HWND flip swapchain (`B8G8R8A8`, opak), D2D DeviceContext; … DComp henüz yok (D-011)" | "D3D11 (donanım, yoksa WARP) + DirectComposition: kök = compositor swap chain (`B8G8R8A8`, opak, bekleme nesnesi), yüzen yüzeyler ve geçiş görüntüleri `IDCompositionSurface`; anahtarla HWND flip swapchain (D-135). … `Compositor`, `LayerSurface`" |
| `docs/ARCHITECTURE.md:79-80` | `anim/Tween` / `anim/ItemMotion` satırları | + `anim/SurfaceMotion` (yüzey hareketleri, token bezier → DComp eğrisi), `anim/AnimationManager` (WAM, D-135) |
| `docs/ARCHITECTURE.md:84` | "Host modal katmanı: scrim, girdi ve odak kapanı…" | + "compositor yolunda her modal kendi yüzeyinde; kapanış son piksellerle oynar; `Host::crossfade`" |
| `docs/ARCHITECTURE.md:87` | "… yalnızca kirli bölge çizilir → `Present1` dirty rect ile. Animasyon varken kare saati çalışır…" (yanlış) | "input → durum → `invalidate()` → en yakın katman (yoksa kök) kirli → sonraki karede kök (gerekirse) bütünüyle çizilir + Present, kirli katman yüzeyleri çizilir, Commit. Yüzey giriş/çıkışları compositor'da, UI thread kare çizmeden. Animasyon yokken kare yok (boşta %0 CPU)." |
| `docs/ARCHITECTURE.md:98` | "`App`: Graphics + Window + SwapChainTarget'ı kurar" | "+ Compositor (yol seçimi, kendiliğinden düşüş)" |
| `docs/ARCHITECTURE.md:15` | "Render: D2D/DComp device" | Değişmez (artık doğru) |
| `CLAUDE.md` "Proje nedir" | "Arayüz: **Direct2D + DirectWrite + DirectComposition** ile tamamen custom-draw." | Aynı + " (hareket: DComp katmanları + Windows Animation Manager eğrileri, D-135)". Bugüne kadar DComp kısmı doğru değildi (D-011) |
| `CLAUDE.md` "Komutlar" | — | `python tools/motion_bench.py <etiket>` satırı; `WINLOVE_COMPOSITION=0` anahtarı |
| `docs/CONVENTIONS.md:37` | "renk geçişleri `Tween` + `animate()`" | + "bir yüzeyin bütünü görünüp kayboluyorsa Host katmanı + `SurfaceMotion` (compositor); yüzeyin içindeki değişim `Tween`. Durgun kare hareketsiz hâlle aynı." |
| `docs/TESTING.md` "Görsel doğrulama" | — | kare tezgâhı, anahtar, `--force-warp`, katmanlı / satır içi karşılaştırma toleransı |
| `docs/ROADMAP.md:19,21` | "DComp ve dirty-rect → 1.4 (D-011)", "DComp/dirty-rect hâlâ D-011" | "→ D-135" |
| `docs/STATUS.md` (geçmiş satırı) | "Faz 4: DComp/dirty-rect (D-011)" | "D-135" |
| `docs/PERF-BASELINE.md:23` | "Kare süresi … dışarıdan ölçülmedi." | Aşama 0'ın tablosu |
| `docs/ENGINE.md` "Saha notları" | — | Öğrenilen DComp / WAM tuhaflıkları (hedef tekliği, `GetCurve` zaman tabanı, WARP, `PrintWindow`) |

---

## 10. Bu makinede doğrulanacaklar / okuyarak belirleyemediklerim

**[API] — ilk karşılaşmada doğrula:**
1. Compositor zincirinin `DXGI_SCALING_STRETCH` zorunluluğu ve `ALPHA_MODE_IGNORE` desteği.
2. `CreateTargetForHwnd` kökeni: istemci alanı mı (bizim `WM_NCCALCSIZE`'ımızla); yuvarlak köşelerin DComp içeriğini kırpması.
3. Pencere başına tek hedef ve hatanın kodu; DWM yeniden başlayınca `CheckDeviceState` / `Commit` davranışı (yalnız VM'de).
4. `IDCompositionSurface::BeginDraw` D2D bağlamının varsayılan DPI / AA ayarları; atlas ofseti.
5. `PW_RENDERFULLCONTENT`'in DComp içeriğini (ve ileride `WS_EX_NOREDIRECTIONBITMAP` penceresini) yakalaması; animasyon
   sürerken anlık hâli vermesi.
6. WARP aygıtıyla compositor zinciri ve yüzeyler: (a) uygulama WARP, DWM donanım; (b) GPU'suz VM.
7. Flip-model'de Present ile Commit'in sıralaması (geçiş başlangıcında atlama var mı) — tezgâhta yoklama pikseliyle.
8. `IDCompositionAnimation::AddCubic` katsayılarının zamanı segment başına göre mi aldığı.
9. WAM: `CreateCubicBezierLinearTransition`'ın varlığı ve hız davranışı; `GetCurve` zaman tabanı; öncelik
   karşılaştırıcısı kurulmadan çakışmanın varsayılan sonucu; CLSID'lerin bağlanması (`uuid` gerekir mi).
10. `DCompositionGetStatistics` / `DCompositionWaitForCompositorClock`'un 26200'de varlığı (yalnız ölçüm için, şart değil).

**Okuyarak belirleyemediklerim:**
- Bugünkü tam kare çizim süresi ve kare aralıkları (hiç ölçülmemiş) — Aşama 2'nin "UI thread kare ≈ 0" kazancının ve
  Aşama 5'in gerekip gerekmediğinin büyüklüğü buna bağlı.
- Hangi sayfaların kurulumu sayfa solmasını tamamen yiyor (1.3) — süre ölçümü gerekir.
- Bu makinede farklı ölçekli ikinci monitör olup olmadığı; ekranın yenileme hızı.
- RDP davranışı (bu oturumda denenemez).
- WinLove.exe'nin WinPE / DWM'siz ortamda çalışmasının desteklenen bir senaryo olup olmadığı (belge bulamadım).
- `tokens.zorder`'daki bildirim > diyalog sırası kasıtlı mı (kod tersini yapıyor; bu plan kodu korur).
- Kullanıcının bekleyen görsel yön kararı (kartlar / yüzeyler, tipografi / yoğunluk) yüzey sayısını ya da gölgeleri
  değiştirirse katman sınırları (`surfaceExtent`) yeniden ayarlanır; hareket dilinin kendisi değişmez.
