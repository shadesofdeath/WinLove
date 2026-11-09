# Değişiklikler · Changelog

[Türkçe](#türkçe) · [English](#english)

---

## Türkçe

### 1.3.0 Beta — 2026-11-09

1.2.3 Beta'dan bu yana gelenler:

#### Yeni

- **Yeni arayüz:** yeni gezinme düzeni (≡ · Görüntü / Ön Ayar / Araçlar / Katılımsız · sol menü). Renkli
  ikonlar, saf siyah tema, pencere kenarında vurgu rengi, yuvarlak onay kutuları. Uygulama ilk açılışta tam ekran
  başlar.
- **Yeni bileşen modeli:**
  - kategori ağacı ve şablonlar (Hafif / Genel / Gizlilik / Önerilmez);
  - bileşen deposundan ölçülen gerçek boyutlar, 32 bit alt satırları, bağımlılık ağacı;
  - kaldırma modu: Varsayılan+ / Derin / Maksimum;
  - imajın kendi sürücüleri, dilleri, klavyeleri, yazı tipleri ve sistem uygulamaları da listede.
- **Uyumluluk korumaları (31):** Windows Update, Mağaza, yazdırma, Wi-Fi / Bluetooth, ses, kurulum, etkinleştirme
  gibi Windows özellikleri ve Office, Discord, Battle.net, EA, NVIDIA, Visual Studio gibi uygulamalar için.
  "Windows'un temeli" koruması hep açık.
- **Etki raporu:** Uygula, kuyruğun bozacağı özellikleri ve onları bozan adımları listeler. "Korumayı aç" derseniz
  o adımlar kuyruktan çıkar.
- **Güncellemelere karşı koru (isteğe bağlı, Uygula'da):** kurulu Windows'ta bir görev, güncellemenin geri açtığı
  hizmeti, sıfırladığı ayarı, yeniden açtığı görevi ve geri kurduğu uygulamayı düzeltir. Sizin kendi
  değiştirdiklerinize dokunmaz. Günlük: `C:\ProgramData\WinLove\Guard\guard.log`.
- **Katılımsız** üst menüde, kendi sayfasında. WinLove kurulum ekranının aşamaları canlı önizlenir; tek anahtarla
  açılıp kapanır.
- **Ayarlar:**
  - DISM motoru: Otomatik / Bu bilgisayar / Windows ADK;
  - ESD sıkıştırması için iş parçacığı sayısı;
  - ön ayar, oluşturulan ISO'nun kök dizinine de yazılır.
- Güncelleme bulma Windows Update'in kendi kanalıyla. Ayrıca güç planı menüsü, sürücü sınıf adları ve ikonları,
  Özellikler ağacı.

#### Düzeltmeler

- **Bileşen kaldırınca güncelleme ISO'ya girmiyordu.** Toplu güncelleştirme bileşen kaldırmalarından sonra
  kuruluyordu. Windows'un servis yığını kaldırılan bileşenlerin kayıtlarını bozuk sayıp güncellemeyi reddediyordu
  (0x80073712). Uygula da o adımı atlayıp imajı güncellemesiz kaydediyordu. Artık önce
  güncellemeler bütün imaja kuruluyor, kaldırmalar ondan sonra yapılıyor.
- **İndirme bitmeden Uygula:** güncelleme inerken Uygula başlatılabiliyordu. İndirme Uygula sırasında biterse
  güncelleme sessizce düşüyor, bildirim yine "kuyruğa eklendi" diyordu. Artık:
  - Uygula indirmenin bitmesini bekler;
  - bekleyen indirmeler kaybolmaz, imaj yeniden bağlanınca kuyruğa girer.
  Dil ve Mağaza indirmeleri için de aynısı geçerli.
- **Ağır ön ayarlarla kurulan Windows açılmıyordu, OOBE döngüye giriyordu ya da explorer çöküyordu.** Şunlar artık
  hiçbir kaldırmayla gitmiyor: önyükleme sürücüleri, oturum ve kurulum dosyaları, kalan modüllerin yüklediği
  DLL'ler, çekirdek yazı tipleri, EFS'nin çekirdeği ve Windows'un çalışması için gereken diğer parçalar.
- **Uygula imajı kaydetmiyordu:**
  - iki kalıcı yetenek yüzünden (0x800F0825);
  - küçültülmüş imajda CBS adımları 0x800F0830 döngüsüne giriyordu.
  İkisi de artık atlanıyor ve imaj kaydediliyor.
- Sol menü kayıyor, bildirimler yazının tamamını gösteriyor, "İsteğe bağlı özellikler" sekmesi tıklanıyor, Hakkında
  sayfası düzeldi.

### 1.2.3 Beta — 2026-10-11

1.2.2 Beta'dan bu yana gelenler:

#### Düzeltmeler

- **Windows indir: en güncel Windows 11 (25H2, 26200.9448) güncellemelerle ISO vermiyordu — x64'te de, ARM64'te
  de.** Bu derlemenin toplu güncelleştirmesi (KB5129241) bir hotpatch paketi; Microsoft'un güncelleme aracı onu
  çevrimdışı imaja almıyor ("Hotpatching is not supported", 0x800F8012). WinLove artık paketi kendisi açıyor (her
  dosya SHA-256 ile doğrulanır) ve servis yığınını, checkpoint'i, toplu güncelleştirmeyi doğrudan imaja ekliyor.
  Hotpatch'in kendisi çevrimdışı imaja girmez: ISO onun dayandığı toplu güncelleştirmeyle (26200.9445) çıkar, sayfa
  bunu yazar; kalan küçük fark kurulumdan sonra Windows Update'ten gelir.
- **Güncellemeler, WinRE ve boot.wim güncellemesi:** aynı paket oralarda da aynı yolla ekleniyor (checkpoint, MSU'nun
  yanındaki klasörden alınır).
- **ARM64 ISO:** ARM64 kurulum medyası Microsoft'unki gibi yalnız UEFI ile açılan bir ISO olur. ISO Oluştur, BIOS
  önyükleme dosyası olmayan bir klasörden de ISO yapar (yalnız UEFI).
- **Uygula: "1 adım başarısız, imaj kaydedilmedi" (0x800F0806).** Bir özellik açma / kapama ilk açılışa iş
  bıraktığında Windows bileşen deposu temizliğini yapmaz; imaj bundan etkilenmez. WinLove bunu hata sayıp bütün
  değişiklikleri kaydetmeden bağlı bırakıyordu. Artık imaj kaydediliyor, sonuç "1 işlem atlandı" diyor.
- **Windows indir: yarım kalan bağlama.** Uygulama dönüştürme sırasında kapanır ya da çökerse kalan bağlı imaj, bir
  sonraki açılışta ve yeni dönüştürmenin başında atılıyor (yeniden kullanılmıyor).
- **Windows indir: indirme kesintileri.** Microsoft'un sunucusu bazı dosyalarda bağlantıyı kesiyordu; dosya baştan
  iniyor, üç denemede iş düşüyordu. Artık kaldığı yerden devam ediyor.
- **Windows indir: ilerleme çubuğu** bir sürüm yeniden yapıldığında (Windows 10 ESU) geri gitmiyor.
- **Windows indir: gereksiz uyarı.** Microsoft'un o sette paketini yayınlamadığı bir uygulama (ARM64'te PC Manager)
  ISO'yu "uyarılarla" göstermiyor; Microsoft'un kendi medyasında da yok.

### 1.2.2 Beta — 2026-10-10

1.2.1 Beta'dan bu yana gelenler:

#### Yeni

- **Uygula: kaydetmeden önce dur.** Uygula özetinde "Kaydetmeden önce ☐ Dur: bağlı klasörü elle düzenleyeceğim".
  Değişiklikler uygulandıktan sonra iş, imajı kaydetmeden durur; imaj bağlı kalır. Klasörde dosya ekleyip silersin
  ("Klasörü aç"), sonra "Kaydet ve devam et". "Diğer sürümlere de uygula" ile her sürüm kendi kaydından önce durur.
  "Durdur" kaydetmeden bırakır.

#### Düzeltmeler

- **Uzun indirme ya da dönüştürme sırasında uygulama kapanıyordu** ("RenderFailure: creating swap chain …
  0x80070005"). Ekran kartı sıfırlanınca (sürücü güncellemesi, uykudan dönüş, GPU takılması) çizim yüzeyi yeniden
  kurulamıyordu. Artık kuruluyor ve süren iş devam ediyor.
- **Windows indir: ISO oluşmuyordu.**
  - Windows 10 + güncellemeler: servis yığını (SSU) hiç kurulmuyordu, toplu güncelleştirme reddediliyordu
    (0x800F0823). Artık önce o kuruluyor. Windows 10'un ESU dönemi toplu güncelleştirmeleri çevrimdışı imaja
    eklenemez (Microsoft kuralı); ISO o zaman onsuz oluşur ve sayfa bunu söyler.
  - Önceki başarısız bir denemenin (başka bir derlemenin) dosyaları bu derlemeninki sanılıyordu; artık sanılmıyor.
  - Hata 4 saniyelik bildirimle kaybolmuyor: nedeni Loglar'a yönlendirmeyle birlikte sayfada kalıyor. Biten bir
    ISO'nun uyarıları da orada kalıyor.
  - Çalışma sürücüsünde gereken yer yoksa ("Yer yetmiyor") ya da yönetici izni yoksa iş başlamıyor (dönüştürme
    yönetici ister; önceden bütün indirmeden sonra düşüyordu).

### 1.2.1 Beta — 2026-10-10

1.2.0 Beta'dan bu yana gelenler:

#### Düzeltmeler

- **Simgeler bütün boyutlarıyla.** Sistem simgesi değiştirilirken seçilen simge, yerini aldığı Windows simgesinin
  boyutlarına tamamlanır (256, 64, 48, 40, 32, 24, 20, 16 ya da o simgenin kendi seti, ör. 96 px). Tek boyutlu bir
  .ico'nun eksik boyutları en iyi görüntüsünden üretilir; Masaüstü / Gezgin simgeleri de aynı şekilde. Kaynak 256 px'ten
  küçükse uyarı çıkar. Küçültme artık kenarlarda koyu saçak bırakmıyor.
- **Windows indir: Mağaza uygulamalarının "Seç…" düğmesi.** Windows 10 derlemelerinde hiç açılmıyordu, çünkü Windows
  10'un ayrı bir uygulama seti yok (uygulamalar imajın içinde geliyor). Artık kutunun yanında "imajın içinde" yazıyor.
  Liste yüklenirken "…" görünüyor; liste alınamazsa "Seç…" yeniden deniyor.
- **Programlar: "Çevrimdışı kur" düğmesi.** Henüz program seçilmemişken açılınca ayar kayboluyordu; her tıklama
  yine "açıldı" diyordu. Artık ayar korunuyor ve düğme durumunu açıkça yazıyor: "Çevrimdışı: kapalı" ya da
  "Çevrimdışı: açık".

### 1.2.0 Beta — 2026-10-10

1.1.0 Beta'dan bu yana gelenler:

#### Yeni

- **Windows indir.** İstediğin Windows 10 / 11 derlemesini doğrudan Microsoft'un güncelleme sunucularından indirip ISO'ya
  dönüştürür. Derleme, dil ve sürüm seçilir; her dosya SHA-256 ile doğrulanır. Seçenekler: güncellemeleri ekle,
  Mağaza uygulamalarını tek tek seç, .NET 3.5, ResetBase, küçük ISO (install.esd). İndirme her sayfadan izlenir.
- **Karşılama ekranı 15 dilde.** Sihirbaz kurulan Windows'un dilinde açılır, yoksa İngilizce: Türkçe, İngilizce,
  Almanca, Fransızca, İspanyolca, İtalyanca, Portekizce (Brezilya), Rusça, Ukraynaca, Lehçe, Felemenkçe, Çince
  (Basitleştirilmiş ve Geleneksel), Japonca, Korece.
- **Karşılama ekranında duvar kâğıdı ve uygulama paketleri.** Windows'un kendi duvar kâğıtlarından seçim (canlı
  önizlemeyle) ve kurulumu yapanın işaretlediği uygulama paketleri (ilk oturumda winget ile).
- **Çevrimdışı program kurulumu.** Programlar sayfasında **Çevrimdışı kur**: seçilen programların kurulumları Uygula
  sırasında indirilip imaja gömülür, hedef bilgisayar internetsiz kurar. MSI, Inno ve NSIS kurulumları ağ kartı
  olmayan bir sanal makinede doğrulandı.
- **Bu bilgisayardan al.** Programlar: bu bilgisayarda kurulu ve winget'te olan programlar tek tıkla listeye.
  Ayarlar / Tweaks: her ayar bu bilgisayardaki hâliyle (geri alınabilir).
- **Secure Boot 2023 medyası.** ISO Oluştur'da "Windows UEFI CA 2023" önyükleme yöneticisi; bu bilgisayarın Secure Boot
  veritabanının durumu yanında görünür.
- **Desteklenmeyen bilgisayarda yerinde yükseltme.** Gereksinim atlatması açıkken medyaya bir betik eklenir; çalışan
  Windows'tan Kurulum'u TPM, Secure Boot, RAM ve işlemci denetimlerine takılmadan başlatır, dosyalar ve uygulamalar kalır.
- **İmaj sağlık denetimi ve onarımı.** Sürümler'de bağlı sürümün menüsünde **Sağlığı denetle / tara**; bozulma
  bulunursa **Onar**.
- **Kendi zamanlanmış görevin.** Görevler'de **Görev oluştur…**: ad, komut ve tetikleyici (oturum açılışı,
  başlangıç, her gün, her hafta, her saat); kurulumdan sonra SYSTEM olarak çalışır.
- **Güç planı.** Kurulum Sonrası'nda bir .pow dosyası içe aktarılıp etkin plan yapılır.
- **"Lite" ISO'lar.** İçinde solid (ESD sıkıştırmalı) install.wim olan ISO'lar artık tanınıyor; bağlamak yerine tek
  tıkla WIM'e dönüştürülür.

#### Düzeltmeler

- Bağlama klasöründe yalnızca imaj artıkları temizlenir; başka dosyalara dokunulmaz.
- Değişiklik kuyruğu diske kaydedilir ve imajıyla geri gelir; imajı çözmek kuyruğu bırakacağını önceden söyler.
- Presetteki uygulama kaldırmaları imajın kendi uygulama sürümünü bulur.
- ISO çalışma kopyası sormadan üzerine yazılmaz ya da başka bir ISO'yla karışmaz.
- Görüntü aygıtı kaybolunca ya da ayarlar sıfırlanınca arayüz, süren bir işin altında yeniden kurulmaz.
- Uygula'da bir adım başarısız olursa imaj kaydedilmeden bağlı tutulur ve neyin olduğu raporlanır.
- Aynı anda tek WinLove çalışır; imaj işi sürerken bilgisayar uykuya geçmez ya da sessizce kapanmaz.
- Karşılama ekranı yanıt dosyasındaki diğer hesapları korur; geride parola kalmaz.
- Arama kutuları Türkçe adları (ı / İ, ş, ğ…) her yerde doğru bulur.

#### Görünüm

- Yeni yazı tipi (Inter; Geist ya da Segoe UI Variable seçilebilir) ve Lucide simgeleri; yoğunluk ayarı.
- Gezinme çubuğunda katlanabilir gruplar ve ayırt edilir simgeler.
- Koyu tema siyaha yakın, katmanlar daha belirgin; Windows yüksek karşıtlık renkleri.
- Sayfalar yumuşak geçişle gelir; bildirimlerde durum şeridi ve **Geri al**; kuyrukta geri al / yinele.
- Tablolar başlığa tıklayınca sıralanır; Bileşenler'de boyut çubukları.
- Ayarlar / Tweaks'te tüm sekmelerde arama ve "yalnız değişenler".
- Kaynak sayfasında son kullanılanlar kart olarak.

#### Bilinen

- Beta: değişiklikleri yine önce bir sanal makinede dene, asıl imajının yedeğini tut.
- Çevrimdışı kurulum imajı büyütür ve Uygula sırasında internet ile winget ister. Bir programın ihtiyaç duyduğu çalışma
  zamanı (Visual C++ vb.) ayrı bir program olarak seçilmeli. App Installer (winget) kaldırılmış bir imajda, kurulumu
  indirilemeyen bir program varsa liste durur.
- 24H2 ve sonrası POPCNT / SSE4.2 olmayan işlemcilere kurulamaz; bunu hiçbir atlatma aşamaz.
- İnternete bağlıyken Windows, kurulum ekranımızdan sonra kendi "Güncelleştirmeler denetleniyor" adımını gösterebilir.
- Karşılama ekranında kurumsal (802.1X) kablosuz ağlara Windows açıldıktan sonra bağlanılır.
- Sağdan sola yazılan diller (Arapça, İbranice) karşılama ekranında henüz yok.

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

### 1.3.0 Beta — 2026-11-09

What changed since 1.2.3 Beta:

#### New

- **New interface:** new navigation (≡ · Image / Presets / Tools / Unattended · side menu). Coloured icons,
  a pure black theme, the accent colour on the window border, round check boxes. The app starts maximized.
- **New component model:**
  - category tree and templates (Light / General / Privacy / Not recommended);
  - real sizes measured from the component store, 32-bit sub-rows, a dependency tree;
  - removal mode: Default+ / Deep / Maximum;
  - the image's own drivers, languages, keyboards, fonts and system apps are listed too.
- **Compatibility guards (31):** for Windows features such as Windows Update, Store, printing, Wi-Fi / Bluetooth,
  audio, setup and activation, and for apps such as Office, Discord, Battle.net, EA, NVIDIA and Visual Studio. The
  "Windows core" guard is always on.
- **Impact report:** Apply lists the features the queue would break and the steps that break them. "Turn the
  guard on" takes those steps out of the queue.
- **Guard against updates (optional, on Apply):** on the installed Windows a task puts back a service an update
  turned back on, a setting it reset, a task it enabled again and an app it brought back. What you changed yourself
  is left alone. Log: `C:\ProgramData\WinLove\Guard\guard.log`.
- **Unattended** has its own page in the top menu. The stages of WinLove's setup screens are previewed live; one
  switch turns them on or off.
- **Settings:**
  - DISM engine: Automatic / This PC / Windows ADK;
  - the number of threads for ESD compression;
  - the preset is also written to the root of the ISO it makes.
- Updates are found through Windows Update's own channel. Also: power plan menu, driver class names and icons,
  Features tree.

#### Fixes

- **With components removed, the update did not get into the ISO.** The cumulative update was installed after
  the component removals. Windows' servicing stack took the removed components' entries for corrupt and refused
  the update (0x80073712), and Apply skipped that step and saved the image without it. Now the
  updates go into the whole image first and the removals follow.
- **Apply before the download finished:** Apply could start while an update was still downloading. If the
  download finished during Apply, the update was silently dropped while the notice still said "queued". Now:
  - Apply waits for the download to finish;
  - held downloads are no longer lost: they are queued when the image is mounted again.
  The same goes for language and Store downloads.
- **With heavy presets the installed Windows did not boot, OOBE looped or explorer crashed.** None of these goes
  with any removal any more: boot drivers, sign-in and setup files, DLLs that remaining modules load, core fonts,
  EFS's core and the other parts Windows needs to run.
- **Apply did not save the image:**
  - because of two permanent capabilities (0x800F0825);
  - on a shrunk image, CBS steps looped on 0x800F0830.
  Both are now skipped and the image is saved.
- The side menu scrolls, notices show their whole text, the "Optional features" tab is clickable, and the About
  page is fixed.

### 1.2.3 Beta — 2026-10-11

What changed since 1.2.2 Beta:

#### Fixes

- **Download Windows: the latest Windows 11 (25H2, 26200.9448) gave no ISO with updates — on x64 and ARM64
  alike.** This build's cumulative update (KB5129241) is a hotpatch package; Microsoft's update agent does not take
  it into an offline image ("Hotpatching is not supported", 0x800F8012). WinLove now opens the package itself (every
  file checked by SHA-256) and adds the servicing stack, the checkpoint and the cumulative update straight into the
  image. The hotpatch itself cannot go into an offline image: the ISO has the cumulative update it builds on
  (26200.9445), the page says so, and the small rest comes from Windows Update after setup.
- **Updates, WinRE and boot.wim updates:** the same package goes in the same way there (the checkpoint comes from the
  MSU's folder).
- **ARM64 ISO:** ARM64 setup media become an ISO that boots by UEFI only, like Microsoft's. Create ISO makes an ISO
  from a folder without the BIOS boot file too (UEFI only).
- **Apply: "1 step failed, the image is not saved" (0x800F0806).** When enabling or disabling a feature leaves work
  for the first boot, Windows does not clean up the component store; the image is not affected. WinLove counted it
  as an error and left all the changes unsaved and mounted. Now the image is saved and the result says "1 operation
  skipped".
- **Download Windows: a half-done mount.** When the app closes or crashes during a conversion, the image left
  mounted is discarded at the next start and when a new conversion begins (never reused).
- **Download Windows: dropped downloads.** Microsoft's server cut the connection on some files; the file started
  over and the job failed after three tries. It now continues where it stopped.
- **Download Windows: the progress bar** no longer goes back when an edition is made again (Windows 10 ESU).
- **Download Windows: a needless warning.** An app Microsoft publishes no package of for the set (PC Manager on
  ARM64) no longer shows the ISO "with warnings"; Microsoft's own media do not have it either.

### 1.2.2 Beta — 2026-10-10

What changed since 1.2.1 Beta:

#### New

- **Apply: pause before saving.** On the Apply summary: "Before saving ☐ Pause: I will edit the mounted folder by
  hand". After the changes go in, the run stops before saving; the image stays mounted. Add or delete files in the
  folder ("Open folder"), then press "Save and continue". With "Also apply to other editions" every edition stops
  before its own save. "Stop" leaves it unsaved.

#### Fixes

- **The app closed during a long download or conversion** ("RenderFailure: creating swap chain … 0x80070005"). When
  the graphics card was reset (driver update, waking from sleep, a GPU hang) the drawing surface could not be made
  again. Now it is, and the running job goes on.
- **Download Windows: the ISO was not made.**
  - Windows 10 with updates: the servicing stack (SSU) was never installed, so the cumulative update was refused
    (0x800F0823). It now goes in first. Windows 10's ESU-era cumulative updates cannot go into an offline image
    (Microsoft's rule); the ISO is then made without it and the page says so.
  - Files left by an earlier, failed run (of another build) were taken for this one's; no more.
  - The reason no longer disappears with a 4-second notice: it stays on the page with a pointer to the Logs. The
    warnings of a finished ISO stay there too.
  - It does not start when the working drive lacks the room it needs ("Not enough space"), nor without
    administrator rights (the conversion needs them; before, it failed after the whole download).

### 1.2.1 Beta — 2026-10-10

What changed since 1.2.0 Beta:

#### Fixes

- **Icons in every size.** A system icon you replace now gets the sizes of the Windows icon it replaces (256, 64, 48,
  40, 32, 24, 20, 16, or that icon's own set, e.g. 96 px). The missing sizes of a single-size .ico are made from its
  best image; Desktop / Explorer icons too. A source smaller than 256 px shows a warning. Shrinking no longer leaves
  dark fringes at the edges.
- **Download Windows: the Store apps' "Pick…" button.** It never turned on for Windows 10 builds: Windows 10 has no
  separate app set (its apps come inside the image). The box now says "in the image". While the list loads it shows
  "…", and when the list cannot be loaded "Pick…" tries again.
- **Programs: the "Install offline" button.** Switched on before any program was picked, the choice was lost and
  every click said "on" again. The choice now stays, and the button says its state: "Offline: off" or "Offline: on".

### 1.2.0 Beta — 2026-10-10

What changed since 1.1.0 Beta:

#### New

- **Download Windows.** Any Windows 10 / 11 build straight from Microsoft's update servers, turned into an ISO. Pick
  the build, language and editions; every file is checked against its SHA-256. Options: integrate updates, pick Store
  apps one by one, .NET 3.5, ResetBase, a small ISO (install.esd). The download can be followed from any page.
- **Welcome screen in 15 languages.** The wizard opens in the installed Windows' language, English otherwise: Turkish,
  English, German, French, Spanish, Italian, Portuguese (Brazil), Russian, Ukrainian, Polish, Dutch, Chinese
  (Simplified and Traditional), Japanese, Korean.
- **Wallpaper and app bundles on the Welcome screen.** A pick from Windows' own wallpapers (with a live preview) and the
  app bundles the person installing ticks (installed with winget at the first sign-in).
- **Offline program install.** **Install offline** on the Programs page: the selected programs' installers are
  downloaded during Apply and embedded in the image; the target PC installs them without internet. MSI, Inno and NSIS
  installers were verified in a virtual machine with no network card.
- **Take from this PC.** Programs: the programs installed on this PC that winget has, in one click. Settings / Tweaks:
  every setting the way this PC has it (undoable).
- **Secure Boot 2023 media.** A "Windows UEFI CA 2023" boot manager option on Build ISO, with this PC's Secure Boot
  database status beside it.
- **In-place upgrade on unsupported PCs.** With the requirement bypass on, the media gets a script that starts Setup from
  the running Windows past the TPM, Secure Boot, RAM and CPU checks; files and apps stay.
- **Image health check and repair.** **Check / Scan health** in the menu of the mounted edition on Editions; **Repair**
  when corruption is found.
- **Your own scheduled tasks.** **Create task…** on Tasks: name, command and trigger (at sign-in, at startup, daily,
  weekly, hourly); it runs as SYSTEM after setup.
- **Power plan.** A .pow file imported and made the active plan on Post-setup.
- **"Lite" ISOs.** ISOs whose install.wim is solid (ESD-compressed) are recognised now; instead of mounting, one click
  converts them to a WIM.

#### Fixes

- Only an image's leftovers are ever cleared from the mount folder; nothing else is touched.
- The change queue is kept on disk and comes back with its image; unmounting says beforehand that it drops the queue.
- App removals from a preset find this image's version of the app.
- An ISO's work copy is never overwritten or mixed with another ISO without asking.
- A lost display device or a settings reset no longer rebuilds the UI under a running job.
- When a step of Apply fails, the image stays mounted unsaved and the report says what happened.
- One WinLove at a time; the PC does not sleep or shut down silently during image work.
- The Welcome screen keeps the answer file's other accounts; no password is left behind.
- Search boxes find Turkish names (ı / İ, ş, ğ…) everywhere.

#### Look

- A new typeface (Inter; Geist or Segoe UI Variable to pick) and Lucide icons; a density setting.
- Foldable groups and distinct icons in the navigation rail.
- A near-black dark theme with layers further apart; Windows high-contrast colours.
- Pages fade in; toasts with a status stripe and **Undo**; undo / redo for the queue.
- Tables sort by a header click; size bars on Components.
- Search and "only what changes" across every tab of Settings / Tweaks.
- Recent sources as cards on the Source page.

#### Known

- Beta: still try changes in a virtual machine first and keep a backup of your image.
- Offline install makes the image bigger and needs internet and winget during Apply. A runtime a program needs (Visual
  C++ and the like) must be picked as a program of its own. On an image without App Installer (winget), the list stops
  when a program's installer could not be downloaded.
- 24H2 and later cannot be installed on CPUs without POPCNT / SSE4.2; no bypass gets past that.
- When online, Windows may show its own "Checking for updates" step after our setup screens.
- On the Welcome screen, enterprise (802.1X) Wi-Fi is joined after Windows starts.
- Right-to-left languages (Arabic, Hebrew) are not on the Welcome screen yet.

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
