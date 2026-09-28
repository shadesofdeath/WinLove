# WinLove — Claude Design için UI Handoff Prompt'u

> Aşağıdaki "PROMPT" bölümünün tamamını Claude Design'a yapıştır.
> Çok büyük bir paket olduğu için en altta "Devam prompt'ları" var; ilk çıktı eksik kalırsa onlarla parça parça tamamlat.

---

## PROMPT

Sen kıdemli bir ürün tasarımcısı ve design-system mimarısın. **WinLove** adlı bir masaüstü uygulamasının eksiksiz UI tasarım handoff paketini hazırlayacaksın. Çıktıyı **tek bir ZIP** olarak ver. Bu paket doğrudan bir C++ geliştiricisine gidecek ve arayüz **Direct2D + DirectWrite + DirectComposition** ile sıfırdan, tamamen custom-draw olarak çizilecek (Win32 common control, WinUI, WPF, Qt, Electron YOK). Bu yüzden her şey piksel-kesin, ölçülü ve vektörel olmalı.

### 1. Ürün tanımı

WinLove, NTLite seviyesinde güçlü bir **Windows imaj özelleştirme aracıdır**. Hedef kitle: sistem yöneticileri, power-user'lar, modderlar.

Yapabildikleri:
- Kaynak açma: ISO, WIM, ESD, SWM, VHD/VHDX; ayrıca çalışan sistemi (live/online) düzenleme
- İmaj içindeki sürümleri (index/edition) listeleme, mount/unmount, ESD↔WIM dönüştürme, export, index silme/birleştirme
- **Bileşen kaldırma** (AppX, Windows bileşenleri, Defender, Edge, telemetri vb.) — bağımlılık ve uyumluluk uyarılarıyla, tri-state ağaç yapısında
- Windows Özellikleri (Features on Demand, Optional Features) açma/kapama
- Güncelleme entegrasyonu (MSU/CAB, SSU/LCU sıralaması, .NET)
- Sürücü entegrasyonu (INF klasörü tarama, sınıfa göre gruplama)
- Kayıt defteri tweak'leri (hazır kategoriler + özel .reg import)
- Servis başlangıç türlerini düzenleme
- Ayarlar/Tweaks (gizlilik, performans, görünüm, gezgin, başlat menüsü)
- Katılımsız kurulum (autounattend.xml: OOBE, yerel hesap, disk bölümleme, dil/bölge, ürün anahtarı, TPM/SecureBoot atlama)
- Kurulum sonrası: uygulama kurulumları, komutlar, dosya kopyalama
- Uygulama kuyruğu (tüm değişiklikler önce kuyruğa girer, "Uygula" ile işlenir)
- ISO oluşturma (bootable, UEFI/BIOS), USB'ye yazma
- Preset (ön ayar) kaydet/yükle/paylaş, karşılaştırma
- Canlı log konsolu

Desteklenen hedefler: Windows 10 / 11, x64 ve ARM64. Uygulama dili: **Türkçe ve İngilizce** (tüm metinler iki dilde).

### 2. Tasarım vizyonu

**Özgün, minimalist, kompakt ve anlaşılır.** Bu dört kelime her kararın ölçütüdür.

- **Fluent / WinUI / Windows 11 görünümü KESİNLİKLE YOK.** Mica, Acrylic, Fluent reveal, Fluent'in büyük yuvarlak köşeli kartları ve iri kontrolleri kullanılmayacak. Hiçbir hazır tasarım dilini (Fluent, Material, macOS, Bootstrap) taklit etme. NTLite dahil hiçbir rakibe benzememeli; WinLove kendi tasarım diline sahip olmalı. Önce bu dili 5–6 maddelik bir "tasarım ilkeleri" listesiyle tanımla, sonra her şeyi ona göre üret.
- **Minimalizm:** Süs yok, gereksiz kutu/kart/çerçeve yok. Hiyerarşi boyutla değil tipografi ağırlığı, renk tonu, hizalama ve boşlukla kurulur. Ekranda her an yalnızca o anda gerekli olan görünür; gelişmiş seçenekler katlanır veya inspector'a gider. Her ekranda tek bir birincil aksiyon olur.
- **Kompakt, küçük öğeler — aşırı büyük hiçbir şey olmayacak:**
  - Varsayılan kontrol yüksekliği **24px** (buton, input, dropdown), küçük varyant 20px; en büyük kontrol 28px.
  - Liste/ağaç/tablo satır yüksekliği **24px** (rahat modda 28px).
  - Gövde metni **12px**, ikincil metin 11px, sayfa başlığı en fazla **16px** semibold. Dev başlık, hero alanı ve büyük illüstrasyon yok.
  - İkonlar arayüzde **16px** (24px set yalnızca boş durum ve karşılama ekranı için).
  - Köşe yarıçapı küçük: **2–4px**. Pill ve büyük radius yok.
  - Gölge çok az ve sadece yüzen katmanlarda (menü, dialog, toast). Ayrım için 1px ince çizgiler ve ton farkları kullan.
  - Boşluk ölçeği 2/4/6/8/12/16/24px; bölümler arası 24px'i geçmesin.
- **Anlaşılırlık:** Her kontrolün ne yaptığı etiketinden anlaşılmalı; yalnızca ikondan oluşan butonlar tooltip'li olsun. Risk ve bağımlılık gibi kritik bilgiler renkle birlikte ikon ve metinle de verilsin. Tutarlı hizalama ve tek bir grid kullan.
- **Renk:** Nötr, sakin bir zemin (hafif sıcak veya soğuk gri; saf siyah/beyaz değil) ve **tek bir vurgu rengi**, o da ölçülü kullanılsın (seçim, birincil aksiyon, odak). Gradyanı yalnızca logoda kullan, arayüzde kullanma. Vurgu rengini özgün bir kimlik oluşturacak şekilde sen öner ve 2–3 alternatifini göster. Kullanıcı vurgu rengini değiştirebilmeli.
- **Temalar:** Varsayılan koyu tema; ayrıca tam destekli açık tema ve High Contrast varyantı.
- **Marka motifi "Love":** Yalnızca logoda ve açılış ekranında çok ince, geometrik bir iz olarak kullanılsın; arayüzün içinde olmasın. Asla çocuksu olmasın.
- **Yoğunluk:** Varsayılan "Compact"; isteğe bağlı "Comfortable" modu satır ve kontrolleri yalnızca 4px büyütür.
- Her şey klavyeyle kullanılabilmeli; komut paleti (Ctrl+K) olmalı.

### 3. Render kısıtları (ÇOK ÖNEMLİ — geliştirici bunları D2D ile çizecek)

- Tüm ölçüler **96 DPI (100%) baz piksel** cinsinden verilsin. 125/150/175/200/250/300% ölçeklemede keskin kalacak şekilde 2px/4px grid'e oturt. 1px çizgiler her ölçeklemede keskin kalmalı.
- Arka plan opak, düz renk olmalı (DWM backdrop, Mica, Acrylic kullanılmayacak).
- Sadece D2D ile kolay çizilebilen efektler: düz renk, rounded rect, path, opacity, ince drop shadow (blur radius + offset + renk olarak ver). CSS'e özgü sihirli efektler (backdrop-filter kombinasyonları, mix-blend-mode, conic-gradient) kullanma; kullanırsan D2D karşılığını yaz.
- Gölgeler: her elevation seviyesi için `offsetX, offsetY, blurRadius, color(ARGB)` olarak iki katmanlı tanım.
- Font: Özgün kimliği destekleyen, küçük boyutlarda (11–12px) çok okunaklı bir sans-serif seç (ör. Inter, IBM Plex Sans, Geist; OFL lisanslı olmalı ve font dosyası pakete eklenmeli). Fallback **Segoe UI**. Monospace: JetBrains Mono veya Cascadia Mono. Seçimini gerekçesiyle açıkla. Tipografi tokenlarında DirectWrite karşılıklarını ver: family, weight (DWRITE_FONT_WEIGHT), size (DIP), line-height, letter-spacing.
- İkonlar `ID2D1SvgDocument` ile veya path geometry olarak çizilecek: bu yüzden SVG'lerde **filter, mask, clipPath, text, image, style/class, CSS, use/defs YOK**. Sadece `path`, `circle`, `rect`, `line`, `polyline`, `polygon`, `g`. Renk `currentColor`.
- Animasyonlar CSS değil sayısal spec: süre (ms), easing (cubic-bezier 4 değer veya spring: stiffness/damping/mass), animasyonlu özellik (opacity, translate, scale, color).

### 4. Uygulama iskeleti (layout)

Minimum pencere 1100×700, referans 1440×900 ve 1920×1080.

- **Custom title bar** (client alana genişletilmiş, **32px**): sol tarafta küçük logo işareti ve aktif imaj breadcrumb'ı (ör. `Win11_24H2.iso › Pro › Mounted`), ortada komut paleti tetikleyicisi, sağda WinLove'un kendi tasarım dilinde çizilmiş min/max/close butonları. Maximize butonunun üzerindeki Snap Layouts hover alanı işlevsel olarak korunmalı.
- **Sol navigasyon** (genişletilmiş **200px** / daraltılmış **44px**): iş akışı sırasıyla gruplanmış adımlar — Kaynak, İmajlar | Kaldır (Bileşenler), Özellikler, Güncellemeler, Sürücüler | Kayıt Defteri, Servisler, Ayarlar/Tweaks | Katılımsız Kurulum, Kurulum Sonrası | Uygula, ISO Oluştur | Presetler, Loglar. Her öğede bekleyen değişiklik sayısı rozeti.
- **Ana içerik alanı**: sayfa başlığı + açıklama + araç çubuğu (arama, filtre, görünüm değiştirme) + içerik.
- **Sağ inspector paneli** (280px, kapatılabilir): seçili öğenin detayları, bağımlılıklar, risk seviyesi, boyut kazancı.
- **Alt durum/görev çubuğu** (24px): mount durumu, imaj boyutu, kazanılan alan tahmini, kuyruktaki işlem sayısı, "Uygula" CTA butonu, arka plan görev ilerlemesi.

### 5. Teslim edilecek ZIP yapısı

```
WinLove-UI-Handoff/
├─ README.md                      # Paketin tamamının rehberi, nasıl okunacağı
├─ manifest.json                  # Paketteki TÜM dosyaların listesi + açıklaması
├─ 00_brand/
│  ├─ logo-full.svg, logo-mark.svg, logo-mono-light.svg, logo-mono-dark.svg
│  ├─ app-icon.svg + app-icon-{16,20,24,32,40,48,64,96,128,256}.svg (her boyut piksel-hizalı ayrı çizim)
│  ├─ splash.svg (açılış ekranı)
│  └─ brand-guidelines.md (kullanım, boşluk, yanlış kullanım örnekleri)
├─ 01_tokens/
│  ├─ tokens.json                 # renk (dark/light/hc), tipografi, spacing, radius, elevation, motion, opacity, z-order
│  ├─ tokens.h                    # C++ header: namespace wl::tokens, constexpr D2D1_COLOR_F / float değerleri
│  └─ tokens.md                   # Her token'ın anlamı ve kullanım yeri
├─ 02_icons/
│  ├─ 16/ ve 24/ klasörleri       # aynı ikonun iki grid boyutu, her biri ayrı optimize
│  ├─ icons-preview.html          # tüm ikonların ızgara önizlemesi, dark/light
│  ├─ icons-paths.json            # { "name": { "viewBox": "...", "paths": ["M..."] } } — D2D path geometry için
│  └─ icons.md                    # stil kuralları (stroke kalınlığı, köşe, optik denge)
├─ 03_components/
│  ├─ <component>.md              # her bileşen için ayrı spec
│  ├─ <component>.svg             # tüm state'lerin yan yana çizimi (redline ölçüleriyle)
│  └─ components-gallery.html     # tüm bileşenlerin canlı galerisi
├─ 04_screens/
│  ├─ <nn>-<screen>-dark.svg / -light.svg   (1440×900)
│  └─ screens.md                  # her ekranın amacı, bölgeleri, etkileşimleri
├─ 05_motion/
│  └─ motion.md                   # tüm geçişlerin sayısal tanımı
├─ 06_prototype/
│  └─ index.html                  # tek dosya, tıklanabilir HTML prototip (tüm ekranlar arası gezinme, tema değiştirme)
├─ 07_copy/
│  ├─ strings.tr.json
│  └─ strings.en.json             # aynı anahtarlar, tüm UI metinleri, hata/uyarı mesajları, tooltip'ler
└─ 08_implementation/
   ├─ d2d-rendering-guide.md      # bileşen → D2D çizim adımları, katman sırası, DirectComposition görsel ağacı önerisi
   ├─ layout-system.md            # grid, breakpoint, panel davranışları, splitter kuralları
   ├─ interaction.md              # hover/press/focus, klavye kısayolları, focus sırası, drag&drop
   └─ accessibility.md            # UI Automation rolleri, kontrast oranları, High Contrast eşlemesi, ekran okuyucu metinleri
```

### 6. İkon seti (en az şu ikonlar, kebab-case adlarla)

Ana set **16px** grid (arayüzde kullanılan), 1px veya 1.25px stroke, piksel-hizalı; büyük görünümler için ayrı çizilmiş 24px seti. İkonlar sade ve geometrik olsun, kendine özgü tek bir karakteri olsun (Fluent, Material veya Lucide kopyası olmasın). Çıktıda stroke'ların **outline path'e dönüştürülmüş** versiyonunu da ver. Her ikonun `regular` ve `filled` (seçili durum) varyantı olsun.

Navigasyon/iş akışı: source, disc-iso, image-wim, layers-editions, mount, unmount, components-remove, puzzle-features, update-download, driver-chip, registry, services-gear, tweaks-sliders, unattended-robot, post-setup-rocket, apply-play, iso-build, usb-drive, preset-bookmark, log-terminal, settings, about-info.

Aksiyonlar: add, remove, delete, edit, duplicate, save, open-folder, import, export, refresh, search, filter, sort, more-horizontal, more-vertical, close, check, chevron-up/down/left/right, arrow-*, expand, collapse, pin, unpin, undo, redo, copy, paste, link, external-link, play, pause, stop, restart, download, upload, compare, lock, unlock, eye, eye-off, drag-handle, command-palette, sidebar-toggle, panel-right-toggle, density-compact, density-comfortable.

Durum: success-circle, warning-triangle, error-octagon, info-circle, question-circle, shield-check, shield-warning, spinner (animasyon için 8 dilimli), queue-clock, dependency-link, risk-low/medium/high, size-saved.

Windows/içerik: windows-logo-generic (Microsoft logosu KULLANMA, jenerik pencere), appx-package, defender-shield, edge-browser-generic, telemetry-signal, network, bluetooth, printer, language-globe, keyboard, user-account, disk-partition, tpm-chip, secure-boot, dotnet-generic, cab-file, msu-file, inf-file, reg-file, xml-file, script-file, folder, file.

Başlık çubuğu: caption-minimize, caption-maximize, caption-restore, caption-close (WinLove'un kendi stilinde).

### 7. Bileşenler (her biri için: anatomi, ölçüler, padding, radius, tipografi, renk tokenları, TÜM state'ler)

State'ler: rest, hover, pressed, focused (focus ring spec'i), disabled, selected, indeterminate (varsa), error, loading.

Button (primary/accent, secondary, subtle, danger, icon-only, split-button), ToggleSwitch, Checkbox (tri-state), RadioButton, Slider, TextBox, SearchBox (kısayol ipucu ile), NumberBox, ComboBox/Dropdown, ContextMenu (alt menü, kısayol metni, ayırıcı, ikon), MenuFlyout, Tooltip (zengin tooltip dahil), NavigationRail öğesi (rozetli), Tabs/SegmentedControl, Breadcrumb, **TreeView (tri-state checkbox, lazy-load, arama vurgusu, risk rozeti, boyut sütunu — bileşen kaldırma ekranının kalbi)**, DataGrid (sıralanabilir sütun, sanal kaydırma, satır seçimi, sütun yeniden boyutlandırma), ListItem/Card, InfoBar (success/warning/error/info), Toast bildirimi, Dialog/ContentDialog, Onay dialogu (tehlikeli işlem), ProgressBar (determinate/indeterminate), ProgressRing, adım adım görev ilerleme listesi, Badge/Counter, Chip/Tag (kapatılabilir), Splitter, ScrollBar (ince, hover'da genişleyen), StatusBar, TitleBar, DropZone (dosya sürükle-bırak), EmptyState, Skeleton loader, LogConsole (renkli seviye, filtre, otomatik kaydırma), KeyValue inspector satırı, Diff görünümü (preset karşılaştırma), Command Palette, Kısayol tuşu (kbd) gösterimi.

### 8. Ekranlar (her biri dark + light, 1440×900)

01 Karşılama / Kaynak seç (son kullanılanlar, sürükle-bırak alanı, live sistem kartı)
02 İmaj listesi (edition kartları/tablosu: ad, mimari, build, boyut, dil; mount/export/sil aksiyonları)
03 Mount işlemi sırasında ilerleme (tam ekran olmayan, zarif)
04 Bileşenler — kaldırma ağacı + inspector (bağımlılık uyarısı gösterilen durum dahil)
05 Özellikler
06 Güncellemeler (sürükle-bırak kuyruğu, sıralama, durum)
07 Sürücüler
08 Kayıt Defteri tweak'leri (kategori kartları + özel .reg)
09 Servisler (tablo, başlangıç türü dropdown'ı)
10 Ayarlar/Tweaks
11 Katılımsız Kurulum (çok adımlı form + canlı XML önizleme paneli)
12 Kurulum Sonrası
13 Uygula — değişiklik özeti, tahmini kazanç, uyarılar, "Uygula" onayı
14 Uygula — çalışırken (adım listesi, canlı log, iptal)
15 Tamamlandı (özet, ISO oluştur'a geçiş)
16 ISO Oluştur / USB'ye yaz
17 Presetler (liste + karşılaştırma diff'i)
18 Loglar
19 Uygulama ayarları (tema, vurgu rengi, yoğunluk, dil, çalışma klasörü, DISM yolu)
20 Hakkında
21 Command Palette açık hali (herhangi bir ekranın üstünde)
+ Boş durum, yükleme (skeleton), hata ve yönetici yetkisi gerekiyor durumları.

### 9. Hareket (motion)

Sayfa geçişi, panel aç/kapa, nav rail genişleme, hover/press geri bildirimi, checkbox/toggle, tree expand, toast giriş/çıkış, dialog açılış (arka plan karartma dahil), progress indeterminate döngüsü, spinner, "Uygula" butonunun kuyrukta değişiklik varken dikkat çeken ama rahatsız etmeyen durumu. Her biri: tetikleyici, özellikler, süre, easing, gecikme. "Reduce motion" açıkken fallback'leri.

### 10. Kalite kuralları

- Tüm SVG'ler geçerli, optimize (SVGO benzeri), gereksiz metadata yok, `viewBox` tanımlı, sabit width/height yok.
- Tüm renk çiftleri WCAG AA (metin 4.5:1, büyük metin/ikon 3:1) — tokens.md'de oranları tabloyla göster.
- Tokenlar tek kaynak: ekranlarda ve bileşenlerde kullanılan her renk/ölçü bir token'a karşılık gelsin; spec'lerde değer yerine token adını yaz.
- Adlandırma tutarlı: kebab-case dosya, `wl.color.bg.base` tarzı token yolları, C++ tarafında `wl::tokens::color::bg::base`.
- Microsoft'a ait logo/marka kullanma; jenerik temsiller çiz.
- README'de "geliştirici bu paketi hangi sırayla okumalı" bölümü olsun.
- Eksik bıraktığın bir şey varsa README'nin sonunda "TODO / Sonraki adımlar" altında açıkça listele; asla yarım dosyayı tamamlanmış gibi gösterme.

- **Boyut denetimi:** Paketi vermeden önce tüm ekranları tara. Bölüm 2'deki ölçüleri aşan hiçbir öğe (28px'ten yüksek kontrol, 16px'ten büyük başlık, 4px'ten büyük radius) kalmamalı. Varsa düzelt.
- **Özgünlük denetimi:** Ortaya çıkan tasarım Fluent, Windows 11 ayarları, NTLite veya bilinen bir UI kit'e benziyorsa yeniden tasarla.

Önce kısa bir tasarım yönü önerisi sun: tasarım ilkeleri, 2–3 renk paleti alternatifi, tipografi ölçeği ve Bileşenler ekranının (04) kompakt bir taslağı. Ardından paketin tamamını üret ve ZIP olarak ver.

---

## Devam prompt'ları (paket tek seferde bitmezse)

1. `Sadece 02_icons klasörünü eksiksiz üret: listedeki tüm ikonlar, 16 ve 24 boyut, regular+filled, icons-paths.json ve icons-preview.html. ZIP olarak ver.`
2. `Sadece 03_components klasörünü üret: her bileşen için .md spec + tüm state'leri gösteren redline'lı .svg + components-gallery.html.`
3. `Sadece 04_screens klasörünü üret: 01–10 arası ekranlar, dark ve light.` → ardından `11–21 ve özel durumlar.`
4. `06_prototype/index.html'i üret: tüm ekranlar arası gezinme, dark/light geçişi, Ctrl+K komut paleti, tree view etkileşimi çalışsın.`
5. `01_tokens/tokens.h ve 08_implementation klasörünü, önceki tokens.json ile birebir tutarlı olacak şekilde üret.`
6. `Paketin tamamını denetle: manifest.json ile gerçek dosyaları karşılaştır, eksik/tutarsız token kullanımını, SVG kural ihlallerini (filter/mask/text/style) listele ve düzelt.`
