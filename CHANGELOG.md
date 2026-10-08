# Değişiklikler · Changelog

[Türkçe](#türkçe) · [English](#english)

---

## Türkçe

### 1.1.0 Beta — 2026-10-08

WinLove alfadan çıkıp **beta** oldu. 1.0.2 Alpha'dan bu yana gelenler:

#### Yeni

- **Kendi kurulum ekranımız (Karşılama ekranı).** Windows'un hesap ve OOBE sayfalarının yerine, Windows logosundan
  hemen sonra WinLove'un Windows 11 görünümlü kurulum sihirbazı açılır. Kurulumu yapan kişiye şunları sorar:
  - kablosuz ağ,
  - hesap (parolalı ya da parolasız),
  - bilgisayarın adı ve saat dilimi,
  - tema, vurgu rengi, görev çubuğu hizası, saydamlık,
  - yedi Gezgin / görev çubuğu tercihi,
  - gizlilik düzeyi.

  Her seçim soldaki çizimde canlı görünür. Bitince bilgisayar bir kez yeniden başlar ve doğrudan masaüstü açılır.
  Windows'un kendi sayfaları, geçici hesabı ve bölge / klavye soruları gelmez. Katılımsız Kurulum › Hesap › **Karşılama ekranı**; **Önizle** ile bu bilgisayarda pencerede denenebilir.
- **Programlar** (Uygulamalar altında). winget deposunun tamamı (~15.400 paket) winget kurulu olmadan aranır. 12 kategori
  ve 6 hazır paket var (Temel, Oyuncu, Geliştirici, Ofis ve okul, İçerik üretici, Gizlilik). Seçilen programlar ilk
  oturumda WinLove'un penceresinde kurulur: ağı bekler, başarısız olanı yeniden dener, kapatılırsa bir sonraki
  oturumda devam eder.
- **Görev çubuğu sabitlemeleri.** Başlat menüsü sayfasında yeni **Görev çubuğu** sekmesi var: Windows'un varsayılanı,
  boş ya da kendi listen. Boş ya da kendi listen seçilince Edge, Mağaza ve Outlook yer tutucuları gelmez.
- **Kurulum ortamı da güncellenir.** Güncelleme kataloğu Safe OS ve Setup dinamik güncellemelerini de bulur. WinRE,
  `boot.wim` ve kurulum dosyaları da güncel olur (ISO / USB sayfasında).
- **WinSxS'i en aza indirme** (Bileşenler › Temizlik, geri dönüşsüz). Tek sürümlük `install.wim` 6,96 GB'tan 4,90 GB'a
  iner. Sanal makinede kurulum, masaüstü ve programlar sorunsuz.
- **Uyumluluk korumaları.**
  - Kalan uygulamaların ihtiyaç duyduğu çalışma zamanları kaldırılamaz.
  - 13 koruma (Windows Update, Mağaza, yazdırma, Wi-Fi / Bluetooth, ses, WebView2 uygulamaları, kurtarma…), açtığın
    özelliklerin ihtiyaç duyduğu bileşenleri ve servisleri korur.
- **Win10 + Win11 aynı ISO'da (AIO).**
  - Sürümler yukarı / aşağı taşınabilir; aynı adlı sürümlere sürüm etiketi eklenir.
  - Windows 11 kurulum ortamı Windows 10'u kuramadığı için WinLove bunu uyarır ve tek tıkla Windows 10 kurulum
    ortamına geçer.
- **Modlu Windows'ta çalışma.** Bilgisayarındaki `wimgapi` ya da DISM bozuk veya eksikse ADK'nın ya da kurulum
  medyasının kopyası kullanılır. Açılışta bir sağlık denetimi yapılır, hata mesajları hangi kopyanın kullanıldığını
  söyler.

#### Düzeltmeler

- Telemetri bileşeni kurulan Windows'u ilk açılışta siyah ekranda kilitliyordu. TroubleShooting paketi artık hiçbir
  tarifle kaldırılmıyor.
- Eski presetler artık güncel bileşen tariflerini kullanıyor. Örneğin görev çubuğunda Outlook yer tutucusu kalıyordu.
- Karşılama ekranında parolasız hesap kilit ekranında açılmıyordu: boş parola yanıt dosyasına boşluk olarak
  yazılıyordu.
- Yanıt dosyasında dil yoksa Windows bölge ve klavye soruyordu. Artık sistemin kendi dili veriliyor.
- Yanıt dosyasıyla açılan hesapların parolası 42 günde doluyordu. Artık süresiz.
- Kurulum görevleri dizüstü bilgisayar pildeyken çalışmıyordu; otomatik oturum parolası kayıt defterinde kalabilirdi.
- Aynı klasöre yeniden bağlanan imajda Simgeler sayfası eski simgeleri gösteriyordu.
- Uygula'nın süre tahmini gerçeğe yaklaştı (dil dosyaları, bileşenler). Mağaza'dan eklenen uygulama doğru sayılıyor.
- Preset karşılaştırması ve yüksek risk onayı neyi içerdiklerini düzgün söylüyor.
- Windows 10 sürümleri doğru derlemeyle (22H2 = 19045) görünüyor.
- MCT ESD'den sürüm eklerken kurulum ve PE imajları artık listelenmiyor.
- Programlar penceresindeki kaydırma çubuğu koyu temaya uyuyor.

#### Görünüm ve akıcılık

- Kurulum ekranındaki animasyonlar artık takılmıyor: pencere beklerken de çiziyor ve tıklamayı hemen alıyor.
- Kurulum ekranındaki denetimler:
  - düğmelerde yumuşak üzerine gelme ve basma,
  - kayan anahtarlar, yaylanarak çıkan onay işaretleri,
  - odaklanınca açılan alt çizgi.
- Sayfalar kademeli geçişle geliyor; tema ya da vurgu değişince renkler yumuşakça değişiyor.
- "Her şey hazırlanıyor" ekranı:
  - Windows 11'in ilerleme halkası,
  - her adımın altında ne yapıldığı (hesap, bilgisayar adı · saat dilimi, gizlilik, görünüm),
  - okunacak hızda ilerleyen adımlar,
  - sonunda "Her şey hazır".

#### Bilinen

- Beta: değişiklikleri yine önce bir sanal makinede dene, asıl imajının yedeğini tut.
- İnternete bağlıyken Windows, kurulum ekranımızdan sonra kendi "Güncelleştirmeler denetleniyor" adımını gösterir
  (birkaç dakika, bir yeniden başlatma; soru sormaz). Atlamanın güvenilir bir yolu henüz yok.
- Karşılama ekranında kurumsal (802.1X) kablosuz ağlara Windows açıldıktan sonra bağlanılır.
- Karşılama ekranı şimdilik Türkçe ve İngilizce: yanıt dosyasının kurulum dili `tr` ise Türkçe, başka bir dilse
  İngilizce; yanıt dosyasında dil yoksa uygulamanın dili. Bir sonraki sürümde ekran kurulan Windows'un dilini kendisi
  seçecek (15 dil).

Önceki sürümler: [Sürümler](https://github.com/shadesofdeath/WinLove/releases).

---

## English

### 1.1.0 Beta — 2026-10-08

WinLove leaves alpha and becomes a **beta**. What changed since 1.0.2 Alpha:

#### New

- **Our own setup screens (Welcome screen).** In place of Windows' account and OOBE pages, WinLove's setup wizard
  opens right after the Windows logo, in Windows 11's own look. It asks whoever installs for:
  - a wireless network,
  - the account (with or without a password),
  - the computer's name and time zone,
  - theme, accent colour, taskbar alignment, transparency,
  - seven Explorer / taskbar preferences,
  - the privacy level.

  Each choice shows live in the picture on the left. Then the PC restarts once and goes straight to the desktop.
  No Windows pages, no temporary account, no region / keyboard questions.
  Unattended Setup › Account › **Welcome screen**; **Preview** shows it in a window on this PC.
- **Programs** (under Apps). winget's whole repository (~15,400 packages) is searched without winget installed. There
  are 12 categories and 6 bundles (Essentials, Gamer, Developer, Office and school, Creator, Privacy). The picked
  programs install at the first sign-in in WinLove's own window: it waits for a network, retries what fails and goes
  on at the next sign-in if it was closed.
- **Taskbar pins.** A new **Taskbar** tab on the Start menu page: Windows' default, empty, or your own list. No Edge,
  Store or Outlook placeholders with the empty layout or your own list.
- **Setup media updated too.** The update catalog also finds the Safe OS and Setup dynamic updates. WinRE, `boot.wim`
  and the setup files are brought up to date (on the ISO / USB page).
- **WinSxS at its smallest** (Components › Cleanup, cannot be undone). One edition's `install.wim` goes from 6.96 GB to
  4.90 GB. In a VM, setup, the desktop and programs all work.
- **Compatibility guards.**
  - Runtimes the kept apps need cannot be removed.
  - 13 guards (Windows Update, Store, printing, Wi-Fi / Bluetooth, audio, WebView2 apps, recovery…) keep the
    components and services the features you turn on need.
- **Windows 10 + 11 in one ISO (AIO).**
  - Editions move up / down; editions with the same name get their release added.
  - The Windows 11 setup media cannot install Windows 10, so WinLove warns about it and switches to Windows 10 setup
    media in one click.
- **Works on modded Windows.** When this PC's `wimgapi` or DISM is broken or missing, the copy from the ADK or the setup
  media is used. A health check runs at startup, and errors name the copy in use.

#### Fixes

- The Telemetry component hung the installed Windows on a black screen at its first boot. The TroubleShooting package
  can no longer be removed by any recipe.
- Old presets now use the current component recipes. For example, an Outlook placeholder stayed on the taskbar.
- On the Welcome screen, an account without a password could not sign in at the lock screen: the empty password was
  written into the answer file as spaces.
- With no languages in the answer file, Windows asked for region and keyboard. The system's own language is now given.
- Accounts from the answer file had their password expire after 42 days. They no longer expire.
- Setup tasks did not run on laptops on battery; the automatic sign-in password could stay in the registry.
- After mounting an image again into the same folder, the Icons page showed the old icons.
- Apply's time estimate is now close to real (language files, components). An app added from the Store is counted
  correctly.
- Preset compare and the high-risk confirmation say properly what they hold.
- Windows 10 editions show their real build (22H2 = 19045).
- Adding editions from an MCT ESD no longer lists its setup and PE images.
- The Programs window's scroll bar fits the dark theme.

#### Look and smoothness

- The setup screen's animations no longer stutter: the window keeps drawing and takes clicks at once while it waits.
- Setup screen controls:
  - soft hover and press on buttons,
  - sliding switches, ticks that pop in,
  - an underline that opens on focus.
- Pages come in by parts; theme and accent changes cross-fade.
- The "Getting everything ready" screen:
  - Windows 11's progress ring,
  - what each step does under it (account, computer name · time zone, privacy, look),
  - steps at a readable pace,
  - "All set" at the end.

#### Known

- Beta: still try changes in a virtual machine first and keep a backup of your image.
- With an internet connection, Windows shows its own "Checking for updates" step after our setup screens (a few
  minutes and one restart; it asks nothing). There is no reliable way to skip it yet.
- On the Welcome screen, enterprise (802.1X) wireless networks are joined after Windows starts.
- The Welcome screen speaks Turkish and English for now: Turkish when the answer file's setup language is `tr`,
  English for any other; with no language in the answer file, the app's. In the next version it will follow the
  installed Windows' own language (15 languages).

Earlier versions: [Releases](https://github.com/shadesofdeath/WinLove/releases).
