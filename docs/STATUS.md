# Durum — "Neredeyiz?"

> Her oturumun sonunda güncellenir. En üstte güncel durum; geçmiş en altta kısa satırlar.

## Güncel
- **Faz:** 3 — sayfalar. P01–P04 ✅. P05 Uygula, P06 ISO, P08 Güncellemeler, P09 Sürücüler, P10 Servisler, P11 Kayıt Defteri,
  P12 Ayarlar / Tweaks, P13 Katılımsız Kurulum, P14 Kurulum Sonrası, P15 Presetler, P16 Uygulama Ayarları, P17 Hakkında, P18 Komut Paleti:
  🟨 geliştirme bitti, kullanıcı testi bekliyor. P07 Bileşenler: 🟨 v2 (AppX + sistem bileşenleri + depo
  temizliği, D-031), test bekliyor.
- **YAYINLANDI: v1.2.0-beta (2026-10-10)** — https://github.com/shadesofdeath/WinLove/releases/tag/v1.2.0-beta
  (ön sürüm, `WinLove.exe` 10.051.584 bayt, SHA-256 `D0A93F9ED2051083D8FDC61F2597C8D76C039E799972DDD68EC85A526470100D`).
  `main` push edildi (a849531). Bilinen sınır (D-104): winget'siz imajda karışık çevrimdışı liste durur — sıradaki aday.
- **Yayın sonrası (2026-10-10, D-105, henüz yayımlanmadı):** simgeler artık değiştirdikleri grubun boyutlarına
  tamamlanıyor (forumdaki "tek boyutlu simge" uyarısı). Tek boyutlu .ico'nun eksik boyutları en iyi görüntüden üretilir,
  yönlendirme modunun .ico'su da 8 boyuta tamamlanır, ölçekleme premultiplied; kaynak < 256 px ise uyarı toast'ı.
  Doğrulandı: birim testler, `wlcli icon-patch` gerçek imageres kopyasında, gerçek imajda (mount) `wlcli apply`
  (Windows 369 grubu 0 hatayla yüklüyor; imaj discard ile ayrıldı, bağlı imaj yok). Uyarı toast'ı dosya seçimi
  gerektirdiği için render'la görülmedi.
- **Hata düzeltmesi (2026-10-10, kullanıcı bildirdi):** Windows indir'de "Mağaza uygulamaları → Seç…" Windows 10'da
  hiç açılmıyordu: UUP dump'ta Windows 10 setinin `appxPresent`'i false, `lang=neutral&edition=app` →
  UNSUPPORTED_LANG, liste boş kalıyordu (uygulamalar zaten imajın içinde, ENGINE saha notu). Panel artık
  `FileSet::appxPresent` + liste sonucuyla durum tutuyor (yükleniyor "…" / hazır "x / y" / alınamadı → Seç yeniden
  dener / Windows 10 "imajın içinde" / yok "bu derlemede yok"). `--demo-download=win10` render'ı eklendi; Windows 10 ve
  11 render'la doğrulandı.
- **Yeni özellik turu (2026-10-09, kullanıcı seçimi): 4 özellik — (1) çok dilli sihirbaz, (2) çevrimdışı program
  kurulumu, (3) imaj sağlık denetimi + onarım, (4) özel zamanlanmış görev.** Not: "bu PC sürücüleri", OEM bilgileri ve
  disk bölümleme zaten vardı (önerilenler kodda kontrol edildi). **Tamamlanan: (3) imaj sağlık denetimi + onarım (D-101)** —
  `ImageHealth.{h,cpp}` + `wlcli health`, Sürümler bağlam menüsünde denetle/tara + "Onar" toast'ı. Gerçek 25H2 imajında
  (mount) check & scan "image is healthy" (exit 0); parse birim testli. **Tamamlanan: (4) özel zamanlanmış görev (D-102)**
  — ChangeSet op `CreateTask`, ScheduledTasks `taskcreate.cmd`/`.json`, Görevler'de "Görev oluştur…" dialog'u. Birim
  testler + gerçek Windows'ta `schtasks /Create` (3 örnek) oluştur/sorgula/sil başarılı; dialog render'landı. **Tamamlanan: (1) çok dilli
  karşılama sihirbazı (D-103)** — 15 dil (tr, en, de, fr, es, it, pt-BR, ru, uk, pl, nl, zh-CN, zh-TW, ja, ko);
  oobe.json `textsByLang` taşır, oobe.ps1 InstalledUICulture ile seçer (İngilizce fallback), yapısal adlar da `$t`'den
  çözülür. `gen_welcome_langs.py` → `welcome-langs.json` (IDR_WELCOME_LANGS). Çeviriler `resources/strings/welcome/`.
  Doğrulandı: birim testler + `capture_oobe.py --ui-lang` ile pt-BR ve ja (CJK) render. **Motor hazır: (2) çevrimdışı program
  kurulumu (D-104)** — `OfflinePrograms` (winget download + manifest parse + `{path}`'li kurulum komutu), `wlcli
  programs-download`, PostSetupPlan offline alanları, programsJson per-program offline, programs.ps1 çevrimdışı dalı.
  Doğrulandı: birim testler + gerçek `wlcli programs-download 7zip.7zip` (1.9 MB msi + msiexec komutu). Orkestrasyon + UI tamam: Programlar
  sayfasında "Çevrimdışı kur" anahtarı; indir+göm `applyPostSetup` içinde Apply anında (bayrakla). **Doğrulandı: gerçek
  imajda (mount) `wlcli apply` ile offline changeset → winget download Apply'da çalıştı, 7-Zip .msi apps'e gömüldü,
  programs.json offline+msiexec taşıyor (apply exit 0).** **VM'de doğrulandı (ağ kartı YOK):** offline1 (7-Zip +
  Notepad++, msi) ve offline2 (Git inno + Steam nullsoft) — dördü de internetsiz kuruldu, ALL PASSED; VM, programs.ps1'in
  ağ bekleme hatasını yakaladı (düzeltildi). **4 özelliğin 4'ü de uçtan uca yapıldı ve doğrulandı (sağlık, görev,
  çok dilli, çevrimdışı programlar).** Bir sonraki somut adım: kullanıcının yeni isteği. Build + tüm testler temiz. Her biri kullanıcı onayı bekler (Altın kural 1).
- **Bu oturumda tamamlananlar (2026-10-09, D-093…D-100):** Windows indir (UUP→ISO, Mağaza uygulamaları + seçici, Windows 10,
  .NET 3.5, ResetBase, install.esd); Secure Boot 2023 medyası (D-094); "Bu bilgisayardan al" — Programlar + Tweaks (D-095);
  güç planı (.pow) içe aktarma (D-096); desteklenmeyen PC'de yerinde yükseltme betiği (D-097); `wlcli welcome-json` +
  `tools/capture_oobe.py` VM'siz sihirbaz render (D-098); **karşılama sihirbazına duvar kağıdı + uygulama paketi sayfaları
  + modern scrollbar** (D-099); **solid install.wim tanıma** (X-Lite gibi lite ISO'lar artık "ESD/solid → WIM" ile mount
  edilebilir, D-100). VM `uup1`/`uup2`/`uup3` ALL PASSED; Windows 10 dönüşümü de başarılı; X-Lite ISO uçtan uca doğrulandı
  (dönüştürülen WIM mount oldu). `dist\WinLove.exe` yeni. **Açık doğrulama:** sihirbazın yeni sayfaları (duvar kağıdı +
  paketler) render ile doğrulandı ama tam OOBE VM kurulumunda henüz denenmedi (welcome harness'ı gerekir). **Bir sonraki
  somut adım:** sihirbaz yeni sayfalarının VM testi veya kullanıcının yeni isteği.
- **Windows özellikleri (2026-10-09, kullanıcı "4,6,8,9 dışında hepsini yap", D-093 / D-094 / D-095):**
  - **Windows indir (D-093):** yeni sayfa (İMAJ grubu) + Kaynak'ta kısayol. Liste UUP dump API'sinden (sürüm / dil /
    sürüm / dosya), dosyalar yalnız Microsoft CDN'inden (SHA-256, 4 bağlantı, 403'te link tazeleme, `.part` devam).
    Kendi dönüştürücümüz (wimlib yok): referanslı dışa aktarma (paket ESD + FOD CAB'leri yakalayarak), WinRE her sürüme,
    `boot.wim` (PE + Setup), güncellemeler içerikten sınıflandırılıp Microsoft sırasıyla (Edge, SSU, enablement/.NET,
    checkpoint+LCU, cleanup), Setup DU medyaya, Mağaza uygulamaları (app CompDB → hash eşleştirme → DISM provision).
    Sayfa seçenekleri kalıcı (settings.json): güncellemeler, Edge, uygulamalar (gruplu seçici), .NET 3.5, ResetBase,
    ESD; **Windows 10** (22H2) ve x86 ürün filtresi. `wlcli uup builds|langs|editions|files|download|convert|role|apps`.
  - **Secure Boot 2023 (D-094):** ISO Oluştur › Önyükleme kutusu; `boot.wim`'den (bağlamadan, `wimFileData`) 2023
    imzalı önyükleme dosyaları medyaya, `efisys_EX.bin` El Torito UEFI imajı. Yanında bu PC'nin db durumu.
    `wlcli iso --secureboot2023`, `wlcli secureboot-db`. Kanıt: ISO'nun `bootx64.efi`'si Windows UEFI CA 2023 zinciri.
  - **Bu bilgisayardan al (D-095):** Programlar (winget dizinine ürün kodu / normalize ad eşleştirme — bu PC'de 65'in
    34'ü) ve Ayarlar/Tweaks (her ayar bu PC'nin kayıt defterine göre — 61 ayar) dialogları, "Geri al"lı.
  - **Kanıt:** 362+ test; VM `uup1` (güncellemesiz) ve `uup2` (güncellemeli, 10.0.26300.9550) ALL PASSED;
    `uup3` (Mağaza uygulamalı, 56 uygulama provision) sürüyor. **Sırada:** 5 yerinde yükseltme, 7 güç planı, 10 karşılama
    sihirbazı + baştan tasarımı. `dist\WinLove.exe` yenilenecek.
- **Tasarım yenilemesi (2026-10-09, kullanıcı "hepsini uygula", D-091 / D-092):** gezinmede katlanabilir grup başlıkları,
  ayrık simgeler; durum çubuğunda iş akışı adımları + kuyruk özeti + indirmeler; Kaynak'ta kartlar; Tweaks'te arama,
  "Yalnız değişenler", sekme sayaçları, işaretli satırlar; tablo sıralama + Bileşenler'de boyut çubukları; İmajlar sağ
  panelinde eylemler üstte; sayfa geçişi / rozet vurgusu, yavaş animasyonlar zamanlayıcıda (D3); "Geri al"lı bildirim,
  Ctrl+Z / Ctrl+Y, uzun dialog metni kayar; boş durumlar; koyu tema siyaha yakın, Windows yüksek kontrastı; yoğunluk +
  Windows metin boyutu. **Yazı tipi Inter** (Geist, Segoe UI Variable seçilebilir), **simgeler Lucide**. VM `b1k`, `b2k`
  ALL PASSED (B1 / B2 kurulan sistemde doğrulandı). `dist\WinLove.exe` yeni. **Sırada (kullanıcı seçimi):** Windows
  özellikleri 1 ISO indirme, 2 Secure Boot 2023, 3 bu PC'den taşıma, 5 yerinde yükseltme, 7 güç planı, 10 + karşılama
  sihirbazının baştan, çok daha üst seviye yeniden tasarımı.
- **İncelemenin kritik 10 maddesi yapıldı (2026-10-09, kullanıcı seçimi "kritiklerin hepsi", D-090):** bağlama klasörü
  koruması (A1), kuyruk diskte + çözme uyarısı (A2/A3), preset uygulamaları aileyle (A4), ISO çalışma kopyası kaydı (A5),
  aygıt kaybı / Sıfırla kilitlenmesi (A6), başarısız adımda imaj bekletilir (A7), tek örnek + uyku/kapanma engeli
  (A12/A13), karşılamada diğer hesaplar + Administrator parolası (B1), otomatik oturum parolası temizliği (B2), yanıtlar
  karşılamasız saklanır (B4), Türkçe arama (D2). **Kanıt:** 354 test; `tools\lab_audit_apply.ps1` ALL PASSED (gerçek imaj:
  DISM eksik AppX'te `0x80073CF1` döndürüyor — düzeltildi); render `--demo-apply=held`; `--test-device-lost` (aynı kabuk);
  tek örnek denemesi (aynı profil ikinci açılış kapanır). VM `b1k` (karşılama + "helper" + Administrator parolası) ve
  `b2k` (klasik otomatik oturum, DefaultPassword) sürüyor — ilk deneme ürün anahtarı ekranında kaldı (lab yanıt dosyasında
  sıfır anahtar, ENGINE saha notu). `dist\WinLove.exe` yeni. **Bir sonraki somut adım:** VM sonuçları; sonra kullanıcı
  listeden "Önemli" maddeleri (11–16) seçer.
- **Genel inceleme (2026-10-09, kullanıcı isteği):** beş paralel salt-okunur inceleme (motor, çevre modüller, uygulama
  katmanı, UI çatısı, NTLite farkı + mühendislik düzeni) → `docs/AUDIT-2026-10.md` (~100 bulgu, dosya:satır, Faz A–F +
  önerilen sıra). Kod değişmedi. **Bir sonraki somut adım:** kullanıcı sırayı seçer; öneri Faz A'nın küçük yıkıcı
  hataları (A1–A6, A12, A13, D2, B1, B2, B4), ardından E1 Secure Boot 2023 CA (PCA 2011 2026-10-19'da sona eriyor).
- **Çalışma şekli:** kullanıcı "her seferinde durma" dedi — sayfa bitince build + test + `-Dist` + yerel commit,
  sonra doğrudan bir sonraki sayfa. Kullanıcı `dist\WinLove.exe`'yi paralel test ediyor.
- **Faz 3'ün bütün sayfaları yazıldı** (P01–P04 onaylı, P05–P18 kullanıcı testi bekliyor). Kural 1 gereği Faz 4'e
  geçmeden önce bu sayfaların kullanıcı onayı gerekir.
- **Kaynak / İmajlar araçları (2026-10-01 gece, kullanıcı seçimi "1,2,3,4,5,6,7,9", D-058):** çoklu dosya bırakma, son kullanılanlarda
  SHA-256 doğrulama dialogu, Kaynak'ta "Klasörden imaj oluştur…" (yönetici); İmajlar'da arama + mimari filtresi, "Araçlar" menüsü
  (sıkıştırmayı değiştir, SWM'e böl / SWM → WIM, başka imajdan sürüm ekle), satır menüsünde "Çoğalt…". WLM kaldırıldı (kullanıcı
  kararı). **Kanıt:** 258 test / 6125 doğrulama; render (`--demo-tool=recompress|split|duplicate|capture|hash|append`,
  `--click-at` ile Araçlar menüsü); `tools\lab_imagetools.ps1` (yönetici, kendim) **ALL PASSED**. Düzeltilen: SWM parçaları tek
  tek referans verilmeli (joker reddediliyor). **Görülmeyen:** bu akışların uygulamanın içinden tıklanarak yapılması, çoklu
  bırakma (render'da sürükleme yok).
- **Bileşenler genişletildi (2026-10-02, D-059):** imaj tarandı (860 gizli paket, COMPONENTS sahipliği, WinSxS boyutları);
  yalnız gerçekten isteğe bağlı 29 paket düzeyinde bileşen eklendi, 6 yeni grup (Gizlilik, Güvenlik, Multimedya, Yazı
  Tipleri, Kurumsal, Diğer). Gerekli olanlar (ağ / depolama sürücüleri, MTP, BitLocker, gpedit) bilerek yok — kullanıcı
  isteği, test bunu denetliyor. Var mı / boyut çalışma anında (`ComponentStoreIndex`, ~4 sn). **Kanıt:** 263 test;
  `tools\lab_cbs_removal.ps1` (yönetici, kendim) ALL PASSED — 30 girdi kaldırıldı, ScanHealth temiz, ISO −844 MB; çalışma
  anı boyutları gerçek imajda Python analiziyle aynı; render `--demo-components`. **Düzeltilen:** offreg değer adı hatası,
  WinSxS listesinin yarıda kesilmesi, paket-yalnız tarifte çökme (`paths.front()`). **Görülmeyen:** kaldırılmış imaja
  sonradan toplu güncelleme, kurulan sistemde etki (VM).
- **Derin kaldırma + kod incelemesi (2026-10-02, D-060):** eski donanım sürücüleri (modem, teyp, disket, FireWire, PCMCIA, POS)
  imajdan ve bileşen deposundan tutarlı biçimde çıkıyor (ScanHealth temiz). Ölçüldü: sonradan eklenen toplu güncelleme
  kurulamıyor → Uygula'da yeni DeepRemove aşaması güncellemelerden sonra; sayfa kırmızı uyarı gösteriyor. `lab_deep_removal`
  (-LcuFirst, KB5129195) ALL PASSED. Ardından dört katmanlı kod incelemesi (çekirdek, UI, sayfalar, shell/CLI) ve düzeltmeleri:
  ~40 gerçek hata (ör. junction üzerinden host'a yazma, ISO yeniden paketlemede önce silme, indirme adresinde kullanıcı-bilgisi
  hilesi, gizlenen widget'ın odak / hover durumu, basılı Enter tekrarı, açık listeyle değişen Dropdown, Uygula'da yarım preset),
  ortak yardımcılar (`base/Text`, `base/File`, `base/Encoding`, `core/system/Files|Com|Handle|BackupFiles`, Shell modal /
  okuyucu yardımcıları, `PageBits`), ~85 kullanılmayan metin anahtarı ve ölü kod silindi. 271 test.
- **Katılımsız kurulum genişletildi (2026-10-02, schneegans üreticisinden):** ek yerel kullanıcılar, yerleşik Administrator +
  parolası, parolalar süresiz / hesap kilitleme kapalı, rastgele bilgisayar adı, kayıtlı sahip / kuruluş, hedef disk numarası,
  kurtarma (WinRE) bölümü, Wi-Fi ve OEM ekranlarını atlama, cihaz şifrelemesini engelleme, 3 sistem (specialize) + 3 ilk oturum
  komutu; yeni adım "Sistem ve komutlar". Öğeler Windows SIM sırasıyla doğru pass / bileşende (birim testi XML'i ayrıştırıp
  yerlerini ve geri okumayı denetler). Kullanıcı test istemedi: gerçek kurulumda görülmedi.
- **Windows Spotlight (2026-10-02, kullanıcı isteği, test edilmeden eklendi):** Ayarlar › Kilit ekranı'nda "Windows Spotlight"
  (CloudContent politikaları + kilit ekranı dönen resim değerleri, ilk oturumda yeniden) ve Ayarlar › Masaüstü'nde Spotlight
  simgesi; aynıları Kayıt Defteri kataloğunda. Spotlight'ın ayrı bir paketi yok (yalnız duvar kağıdı paketinde 1,6 MB): kaldırma
  = tamamen kapatma. Kullanıcı test istemedi: imajda / kurulan sistemde görülmedi.
- **Diller: Windows Update dil dosyaları + otomatik bulma + yeni sayfa (2026-10-04, kullanıcı isteği, D-061):** kullanıcının
  uupdump.net'ten indirdiği en-us dosyaları Uygula'da düşüyordu — dil paketi (.esd) tanınmıyordu, express meta veri cab'ları
  paket sanılıyordu, özellikler UUP adıyla 0x800F0912 veriyordu (CBS bağımlılıkları LoF adıyla arıyor). Motor iki adlandırmayı
  da tanıyor, .esd'yi klasöre açıp ekliyor, UUP adlı cab'ı LoF adıyla kopyalayıp ekliyor; Planner dil dosyalarını kurulum
  sırasına diziyor. Yeni "Dil ekle…": imajın build'i için uupdump.net listesinden 43 dil, parça seçimi (el yazısı / OCR /
  metin okuma / konuşma / imajdaki bileşenlerin dilleri), Microsoft sunucularından SHA-256 denetimli indirme, kuyruk.
  Sayfa dil odaklı (satır = dil), indirme şeridi, toplu güncelleme uyarısı; dil adları uygulama dilinde. `wlcli uup-languages`.
  **Kanıt:** 282 test / 7080 doğrulama; render `--demo-languages[=dialog|fetch]`; `tools\lab_languages.ps1` (yönetici,
  kendim, 13:00 ALL PASSED): Pro'da kullanıcının UUP klasörü 22/22 adım, Home'da otomatik indirme (32 dosya) 33/33 adım, ikisinde de en-US kurulu
  ve arayüz dili. **Görülmeyen:** uygulamanın içinden indirme (gerçek pencerede ağ + dialog), kurulan sistemde İngilizce
  arayüz (VM), dilden sonra LCU'nun yeniden kurulması. Kapsam dışı: LXP dilleri, boot.wim / kurulum ekranı dili, lang.ini.
- **Gece turu (2026-10-05, kullanıcı uyurken, istek listesi; D-062 … D-066):**
  - **lang.ini (D-062):** dil eklenen Uygula'da kurulum klasörünün `sources\lang.ini`'si `dism /Gen-LangINI` ile yenilenir → Setup'ın
    "Yüklenecek dil" listesine eklenen dil gelir (kurulum ekranı dili hâlâ Türkçe: boot.wim'e WinPE dil paketi yok).
  - **Ayarlar (D-062):** masaüstü simge boyutu, Denetim Masası görüntüleme ölçütü (kategori / büyük / küçük), sanal bellek (Windows
    yönetsin / C: / kapalı + "D: 4096 8192" özel), Microsoft Defender anahtarı (servisler, politikalar, tepsi, sağ tık). `lab_settings_d062` ALL PASSED.
  - **Duvar kağıdı (D-062):** özel duvar kağıdı seçilince Spotlight masaüstünden kapatılır (ilk oturum politikası), sayfa uyarır.
  - **Defender tamamen (D-063):** Bileşenler › Güvenlik; tariflere `appx` alanı (Windows Güvenliği uygulaması). `lab_defender` kaldırma geçti.
    **Önemli düzeltme:** "kaldırmadan sonra LCU kurulmuyor" (D-060'ta da) aslında DISM API'nin bu makinede .msu'yu kuramaması
    (0x800401E3, dokunulmamış imajda da); dism.exe kuruyor → motor .msu'da otomatik dism.exe'ye geçer.
  - **Bağlı imajlar (D-064):** Kaynak sayfasında bu bilgisayardaki bütün DISM mount'ları; çift tık ile benimse, Delete ile kaydetmeden ayır.
  - **Simgeler (D-065):** yeni sayfa; 14 simge yuvası, ikon paketi klasörü (ad / iconpack.json), tek tek .ico, kısayol oku kaldır,
    gerçek önizleme. DLL yamalanmaz (güncellemelere dayanıklı).
  - **Mağaza (D-066):** Uygulamalar › "Mağazadan ekle…": Microsoft Store araması → Windows Update'ten uygulama + çerçeveler (SHA-256)
    → kuyruk. rg-adguard bot korumalı olduğu için aynı veri doğrudan Microsoft'tan. Şifreli paketler elenir. `lab_store` ALL PASSED.
  **Görülmeyen (hepsi):** kurulan sistemde etkiler (VM), uygulamanın içinden ağ akışları (Store, dil indirme), başka araçla bağlanmış
  imajın uygulamada benimsenmesi.
- **GitHub tweak seti + Kayıt Defteri sadeleşti (2026-10-05 öğlen, D-067):** 9 popüler projeden (winutil, Win11Debloat, Winhance,
  Sophia, Optimizer, AtlasOS, ReviOS, xd-AntiSpy, schneegans) bizde olmayan 90 aday; kullanıcı hepsini seçti → Ayarlar'a 154 satır
  (283 ayar), sekmeler 7 → 10 (Yapay zekâ, Uygulamalar, Güvenlik yeni), yanlış bölümdekiler taşındı. Kayıt Defteri artık yalnız
  kullanıcının kendi girdileri: "Değer ekle / düzenle" diyaloğu (her tür, sil, kurulumdan sonra yeniden uygula) + .reg içe
  aktarma; `tweaks.json` / `TweakCatalog` kaldırıldı (35 tweak'in hepsi Ayarlar'da vardı). Motor: imajda olmayan servis atlanır.
  **Kanıt:** 289 test / 10.168 doğrulama; render (10 sekme 1280'de tr / en, Kayıt Defteri, diyalog); `tools\lab_settings_d067.ps1` (yönetici):
  ALL PASSED (2026-10-05 12:11, Pro 26200): 275 ayar → 578 işlem hatasız; 557 / 557 değer motorun okuyucusuyla imajda, her tür ve hive için reg.exe örnekleri (DWORD, SZ, boş SZ, EXPAND_SZ, BINARY, QWORD, varsayılan değer, WOW6432Node, HKCR, UsrClass.dat, `%%Startup` adlı anahtar, anahtar silme), servis başlangıçları, ilk oturum dosyaları; `diagnosticshub.standardcollector.service` 25H2 imajında yok → atlandı, anahtar oluşmadı.
  **Görülmeyen:** kurulan sistemde etkiler (VM), uygulamanın içinden değer ekleme / düzenleme (gerçek pencerede).
- **Simgeler: dosya yaması (2026-10-05 öğleden sonra, D-068):** kendi PE kaynak okuyucu / yazıcımız; `.mun` dosyalarının bütün simgeleri listelenir, herhangi biri .ico / PNG ile değiştirilir; orijinal `Windows\WinLove\IconBackup`, geri yükleme betiği, sahip / DACL birebir, WinSxS bağlantısı korunur, Windows yükleyicisiyle doğrulama; kod içeren dosyalar reddedilir. Sayfa iki sekme (Sistem simgeleri / Masaüstü ve Gezgin), paket yükle / dışa aktar. `wlcli icons|icon-extract|icon-patch|icon-verify|icon-image`. **Kanıt:** 301 test / 11.012 doğrulama; `tools\lab_icons.ps1` ALL PASSED (+ `-Lcu`: yamadan sonra KB5129195 kuruluyor, ScanHealth temiz); `tools\lab_icons_vm.ps1` ALL PASSED — yamalı imaj VMware'de kuruldu, masaüstüne ulaştı, yeni simgeler görünüyor. **Görülmeyen:** uygulamanın içinden yama + Uygula (gerçek pencere).
- **Başlat menüsü sayfası (2026-10-05 akşam, D-069):** yeni sayfa (Ayarlar / Tweaks'in altı): Sabitlenenler (Windows varsayılanı / Boş / Kendi listem; imajın uygulamaları simgeleriyle, Başlat önizlemesi = sıralanabilir liste, `applyOnce`) + Başlat ayarları. Motor: boş Başlat durumu her sürümde + ilkenin iki biçimi; `WriteFile` artık `base64:` ikili dosya taşır. **Kanıt:** 306 test / 11.179 doğrulama; 11 VM kurulumu (`tools\lab_vm.ps1`): boş Başlat Pro / Home her build'de, özel liste 26200.9457'de Pro ve Home'da birebir; 26200.8037 özel listeyi uygulamıyor (sayfa uyarır). **Görülmeyen:** `applyOnce` ile kullanıcının sonradan düzenlemesi (etkileşimli), uygulamanın içinden Uygula.
- **7TSP simge paketleri + iki arayüz hatası (2026-10-06, D-073):** Simgeler › "İkon paketi yükle…" → Arşivden (.7z / .zip · 7TSP) / Klasörden. `.res` okuyucu, Windows `tar.exe` (yoksa 7-Zip), paket `%LOCALAPPDATA%\WinLove\IconPacks`'e dönüştürülür ve D-068 yolundan kuyruğa girer; imajda olmayan gruplar artık kuyruğa girmiyor (klasör paketlerinde de — Uygula'da dosyanın bütün yamasını düşürüyordu). Gezinme çubuğu kısa pencerede kayıyor (Daralt sabit, artık üst üste binmiyor); risk onay dialogu pencereye sığıyor, liste kayıyor. `wlcli icon-pack`. **Kanıt:** Lumicons paketi 241 simge, 5 `.mun` dosyası yamalı kopyada Windows yükleyicisiyle 0 hata; 311 test / 11.321 doğrulama; render. **Görülmeyen:** uygulamanın içinden paket + Uygula (yönetici), kurulan sistemde.
- **Önceki kurulum + WinRE'siz imaj (2026-10-06, kullanıcı isteği + hata, D-074):** ISO / USB › ÖNYÜKLEME › "Kurulum ekranı: Önceki kurulumu kullan (24H2+)" — boot.wim'de `Setup\CmdLine` önceki kuruluma yönlenir (`wlcli boot-patch --legacy-setup`). Kullanıcının ISO'su yeni kurulumda ~%5'te düşüyordu: `removeComponent winre` install.wim'den `Winre.wim`'i silmiş, yeni kurulum WinRE'siz imajı kuramıyor. install.wim'in dosya listesi artık bağlamadan okunuyor (`wimFileExists`, `wlcli wim-file`, ~0,2 sn): WinRE'siz 24H2+ sürüm varsa kutu kendiliğinden açılır + uyarı; Bileşenler'deki WinRE notu güncellendi. **Kanıt:** birim testleri (sentetik dosya listesi, LZX'li metadata, bozuk girdi); `tools\lab_legacy_setup.ps1 -Vm` ALL PASSED — yalnız Winre.wim'i silinmiş sürüm: yeni kurulum 30 sn'de "Windows 11 yüklemesi başarısız oldu", önceki kurulum masaüstüne kurdu; kullanıcının ISO'sunda `Winre.wim` MISSING; render `--demo-no-winre`. **Görülmeyen:** kullanıcının kendi ISO'sunun (520 değişiklik) önceki kurulumla kurulması, gerçek donanım / Ventoy, ESD kaynakta algılama.
- **Canlı deneme (2026-10-06, kullanıcı isteği):** lab VM'leri (`od-k`, `od-l-home`) ve Belgeler\Virtual Machines\Windows 10 x64 silindi (VMware kitaplığındaki ölü kayıtlar kullanıcıda). Kullanıcının ISO'suna test ISO'sunun 2. sürümünden (Home Single Language) özgün `Winre.wim` eklendi → `Desktop\Win11_25H2_Turkish_x64_v2_WinLove_WinRE.iso` (`--no-prompt`, boot.wim'i kullanıcınınki: yeni kurulum); `build\lab\vm\winlove-winre` (EFI, 8 GB, 4 çekirdek, NAT) VMware penceresinde açık, kullanıcı kurulumu izliyor: WinRE geri gelince yeni kurulumun geçmesi beklenir.
- **Bir sonraki somut adım (2026-10-06):** kullanıcı `dist\WinLove.exe` ile aynı kaynaktan (WinRE kaldırılmış çalışma klasörü) ISO Oluştur → "Önceki kurulumu kullan" kendiliğinden işaretli olmalı → yeni ISO'yu kurar. Yalnız boot.wim değiştiği için Uygula'yı yeniden çalıştırmak gerekmez.
- **Yayın (2026-10-06, kullanıcı isteği):** `v1.0.1-alpha` GitHub'da (prerelease, `dist\WinLove.exe`, SHA-256 `ECAA209F…AED8`): D-073 + görev çubuğu yer tutucuları (InboxApps yedek paketleri, "Önerilenler bölümü", "Bulutun eklediği sabitlemeler" — VM'de doğrulanmadı). Sürüm `PROJECT_VERSION 1.0.1`, etiket "1.0.1 Alpha" (`WinLove.rc` elle).
- **Uygulamanın içinden test + AIO 3. adım (2026-10-07, kullanıcı PC başında değil, "her şeyi sen yap"):** yeni `tools\gui.py` (yönetici sunucu; tıklama / tuş / dosya seçici iletiyle, görüntü yalnız pencereden; `--profile` ile kullanıcının listelerine dokunmaz). AIO zinciri baştan sona uygulamada tıklanarak geçti (ISO aç → Win10 ESD'den Pro ekle → ISO Oluştur uyarısı → Windows 10 ortamını al → ISO); VM'de Win11 Pro Win10 ortamından **atlatmasız** kuruldu. Bulunup düzeltilen 3 hata: MCT ESD'sinin kurulum / PE imajları ekleme diyaloğunda; Win10 ortamında "Önceki kurulum" kutusu + yanlış özet; Win10 22H2'nin 19041 / "2004" görünmesi (güncelleme ve dil hedefi de yanlıştı) → etkinleştirme paketinden 19045 (D-077). Lab 39,9 → 16,6 GB temizlendi (AIO çıktıları, eski bisect kareleri), `dist\WinLove.exe.old` silindi.
- **Uygulamanın içinden test, 2. tur (2026-10-07):** test ISO'su → Pro bağla → Bileşenler'de WinRE + Simgeler › İkon paketi yükle › Arşivden (`.7z`, 238 simge) → Uygula'da risk onayı → 6 adım 0 hata (2 dk 19 sn) → ISO Oluştur'da "Önceki kurulumu kullan" **kendiliğinden işaretli** (WinRE uyarısıyla) → ISO; VM: klasik "Windows Kurulumu" ekranı, WinRE'siz Pro masaüstüne, Lumicons simgeleri görünüyor (`vm-gui-winre-icons` ALL PASSED). Kısa pencerede gezinme çubuğu kayıyor (Daralt sabit). Düzeltilen: preset karşılaştırması aynı değeri yazan iki ayarı (Ayarlar'da gizlenen sayfalar / Özel sayfa listesi) birbirine karşı "değişti" gösteriyordu; risk onayı Defender gibi ayarları her kayıt değeri için ham anahtarla ve "kalıcı olarak kaldırılacak" diye listeliyordu → ayar adı + seçenek, başlık / metin / düğme içeriğe göre; plan adımı "Ayarlar, dosyalar ve kayıt defteri". **Bilinen (düzeltilmedi):** Başlat menüsü sayfasında uygulama adları imajın dilinde değil (Türkçe imajda "Calculator"; kısayollar için desktop.ini + MUI, Store uygulamaları için resources.pri okunmalı); Bileşenler'de sağ panel açıkken üstteki özet metni kısalıyor.
- **Uygulamanın içinden test, 3. tur (2026-10-07):** kullanıcının preset'i (310 öğe → 521 işlem; en-US dil dosyaları kendi önbelleğinden) Home SL'ye uygulamada yüklendi + Başlat › "Bulutun eklediği sabitlemeler" kapatıldı → 11 dk, 0 hata. **Ağlı VM** (`vm-gui-preset-net`, önceki kurulum boot.wim'i): ilk oturumdan 33 dk sonra görev çubuğunda Outlook / M365 / Edge / Store yok (D-076 doğrulandı); Başlat'ta özel liste boş (26200.8037, D-069'daki bilinen durum). Ayrıca uygulamadan: Kayıt Defteri › Değer ekle / düzenle (çift tık) + .reg içe aktarma (UTF-16, QWORD, MULTI_SZ, HKCU), Mağazadan ekle (Windows Terminal + bağımlılık, SHA-256), Dil ekle (eu-ES, uupdump + Microsoft) → Uygula 8 adım 0 hata; salt okunur bağlamada 5/5 değer, eu-ES, Terminal, lang.ini doğrulandı. Düzeltilen: aynı bağlama klasörüne yeniden bağlanınca Simgeler eski simgeleri gösteriyordu (dosya simgesi önbelleği); süre tahmini (dil dosyası 60 → 3 sn; sayfa 45 dk–1 sa derken 11 dk sürdü); Mağaza uygulaması "Kaldırılan bileşen" sayılıyordu. `tools\vnc_shot.py --wake` (kararmış ekranı uyandırır).
- **Programlar (P22, 2026-10-07, kullanıcı seçimi "Ninite gibi"; D-078):** winget deposu winget'siz (imzalı `source2.msix` →
  `index.db`, Windows'un SQLite'ı), ~15 400 paket. Sayfa: arama (bütün depo), Kategori (12 kategori + **Bütün depo**),
  her kategoride öne çıkanlar + winget etiketleriyle "N program daha", **6 hazır paket** (Temel, Oyuncu, Geliştirici, Ofis
  ve okul, İçerik üretici, Gizlilik; kullanıcı istedi), sağda ayrıntılar (açıklama, lisans, kurulum türü, simge). Kurulum
  ilk oturumda WinLove'un WPF penceresinde, internet yoksa bekler. Kurulum Sonrası'nın eski "Hazır uygulamalar" dialogu
  kaldırıldı (düğme sayfayı açar). **Kanıt:** gerçek pencerede (gui.py) arama / kategori / Bütün depo / paketler /
  önizleme; VM `programs-net` (ağlı, 6 program): 7-Zip, VLC, Chrome, Discord kuruldu (ekran), Notepad++ / Spotify
  günlükten okunamadı (lab sanal diski siliyor) → `programs-diag` (-Diag; tanılama artık WinLove günlüklerini de kopyalıyor):
  ilk oturumdan ~1 dk sonra pencere, 5/6 kuruldu (7-Zip 14 sn … Discord, toplam 6 dk); **Spotify** `0x8A150056` "installer
  cannot be run from an administrator context" → **düzeltme:** bu kodda aynı winget komutu kullanıcının sınırlı yetkili
  geçici görevi olarak yeniden çalışır (UAC / parola yok; bu PC'de whoami ile Orta bütünlük doğrulandı) → VM `programs-spotify`:
  "refuses an administrator; running it as lab without elevation" → Spotify kuruldu (1 dk), 7-Zip de; ALL PASSED.
  **Not:** Windows 25H2 ilk oturumda Başlat menüsünü kendisi açıyor; pencere arkasında kalıyor (kullanıcı tıklayınca kapanır).
- **WinSxS en aza (2026-10-07, kullanıcı isteği "tiny iso gibi"; D-079):** Bileşenler › Temizlik › "WinSxS'i en aza indir
  (geri dönüşsüz)", tiny11 "core" izin listesi, yeni `ShrinkStore` / `Phase::Shrink` (Uygula'nın son adımı), otomatik
  güncellemeler de kapatılır. Ölçüm: WinSxS gezginde 10,33 GB ama 7,22 GB'ı System32 ile aynı dosya; açılan 2,97 GB; tek
  sürüm install.wim 6,96 → 4,90 GB. `wlcli store-shrink [--dry-run]`. **VM `shrink-max`** (ResetBase + küçültme + 4 program,
  ağlı, -Diag): ALL PASSED — kurulum, ilk oturum, masaüstü; WinSxS 427 klasör; SideBySide olayı 0; regedit, mmc, control,
  msinfo32, cleanmgr, notepad, 32 bit cmd, 7-Zip, Notepad++, VLC açılıyor; winget 4/4 (VC++ MSI dahil); `dism /online
  /get-packages` çalışıyor. Aynı koşullu çift (`shrink-max2` / `shrink-base`: aynı 4 program, 25 dk, ikisinde de ResetBase +
  güncellemeler kapalı): **C: 17,46 GB / 19,66 GB → kurulu sistemde −2,20 GB**, ikisi de ALL PASSED, SideBySide 0. taskmgr SYSTEM'den 0x80070005 — küçültmesiz sistemde de aynı (tanılamanın oturum dışından başlatması).
- **Yayın 1.1.0 Beta (2026-10-08, kullanıcı isteği):** alfadan betaya; `v1.1.0-beta` GitHub'da (ön sürüm, `dist\WinLove.exe`),
  notlar `CHANGELOG.md` (Türkçe + İngilizce; 1.0.2'den bu yana D-075…D-088). Sürüm `PROJECT_VERSION 1.1.0`, etiket
  "1.1.0 Beta" (`WinLove.rc` elle), README tablosu yeni özelliklerle. **Bir sonraki somut adım (kullanıcı seçimi):** çok
  dilli karşılama ekranı (kurulan Windows'un dili, 15 dil, İngilizce yedek) → 1.1.1.
- **Karşılamada akıcılık + son ekran (D-088, 2026-10-08):** uyumayan mesaj döngüsü, animasyonlu denetimler, kademeli sayfa
  geçişi, tema geçişinde yumuşak geçiş; son ekranda Windows 11 halkası, her adımda ne yapıldığı, okunur hız, "Her şey
  hazır". **Kanıt:** 18 storyboard çalıştırıldı, önizleme kareleri, VM `u3` ALL PASSED. **D-089:** OOBE'nin güncelleme adımını
  HOSTS (`sdx.microsoft.com`) ile atlamak VM `u4`'te işe yaramadı, geri alındı (Bilinen).
- **Karşılamanın ilk gerçek testi (D-087, 2026-10-08):** kullanıcının kendi ön ayarı ve VM'inde (yanıt dosyasında dil
  yok, parola boş, NAT ağı, Home SL) Windows bölge / klavye sordu, güncelleyip yeniden başladı, kilit ekranı yazılamayan bir
  parola istedi. Nedenler: `oobe.ps1` boş parolayı satır sonu + boşluk yazıyordu (XmlDocument girintisi), dil yoksa OOBE kendi
  sayfalarını açıyordu; ayrıca yanıt dosyası hesapları 42 günde parola istiyordu. **Düzeltildi** (PreserveWhitespace,
  sistemin dili / klavyesi oobeSystem'e, parola süresiz). **Kanıt:** VM `u1` (parolasız) + `u2` (parolalı), kullanıcının yanıt
  dosyası + ağ ile ALL PASSED. Ön ayar uygulanırken eski betik güncel sürümle değişir (kullanıcının ön ayarı hatalı betiği
  taşıyordu; birim testi). **Bir sonraki somut adım:** kullanıcı eski VM'i siler, `dist\WinLove.exe` ile **orijinal ISO'dan**
  başlayıp (çalışma klasöründeki imajda eski betik var) aynı ön ayarı yükler, Uygula → ISO → kurar. **Bilinen:** ağ varken OOBE'nin güncelleme ekranları (~3,5 dk, bir yeniden başlatma) Windows'un kendisi.
- **İlk oturum ekranı denemesi (D-086, 2026-10-08):** kullanıcı ilk oturumdaki "Windows hazırlanıyor" yerine kendi
  ekranımızı istedi. `DelayedDesktopSwitchTimeout 0` + tam ekran WPF penceresi (zamanlanmış görev ve Active Setup ile) VM'de
  Win10 / Win11'de denendi: Windows'un ekranı kalktı ama PowerShell ilk oturumda 10–20 sn'de açıldığı için masaüstü bizden
  önce hazır oldu — **bırakıldı**, geri alındı. Kalan: görevler pilde de çalışır (ilk oturum görevi parolayı pildeki
  dizüstünde de siler), `lab_vm.ps1 -ShotSeconds`. **Kanıt:** VM `w10f`, `spec7`, `w10g`, `spec8` ALL PASSED (parola
  silinmiş, ayarlar doğru); ekran görüntüleri `build\visual\*-signin-sheet.png`. "Biraz bekleyin" (OOBE'nin geçici
  oturumu) Windows'un kendi ekranı; değiştirilmiyor.
- **Kurulum ekranımız Windows Kurulumu'nun içinde (D-085, 2026-10-08, kullanıcı uyurken):** kullanıcı D-084'ün görünümünü
  beğenmedi, daha fazla ayar + Wi-Fi istedi ve akışı tarif etti ("logodan sonra bizim ekran, seçince yeniden başlayıp masaüstü,
  Windows OOBE'si yok"). Claude Design tuvali (claude.ai/artifact/G6srYK5bT9LRCtjxBJSLQm, 7 ekran + koyu) → WPF yeniden yazıldı:
  Windows 11 kurulumu görünümü, canlı çizimler, animasyonlar; yeni sayfalar Ağ (Wi-Fi) ve Tercihler (7 anahtar), saat dilimi,
  görev çubuğu hizası, saydamlık. Sihirbaz artık specialize'ın son komutu (SYSTEM, hesap yokken); hesap ve otomatik oturum
  Windows'un PreOobe kancasıyla OOBE'ye verilir, geçici hesap yok. Uygulamada Ağı sor / Tercihleri sor. **Kanıt:** 345 test; VM Win11 26200 (`spec6`) + Win10 19045 (`w10e`) ALL PASSED (ad, saat, tema, 7 tercih, gizlilik, temizlik).
  **Görülmeyen:** gerçek Wi-Fi ile bağlanma (VM'de kablosuz bağdaştırıcı yok; liste API'si bu bilgisayarda önizlemede
  çalıştı, bağlanma denenmedi), insan eliyle tıklanarak kurulum, ARM64, Home sürümü, OEM lisanslı gerçek bilgisayar.
- **Kendi karşılama ekranımız (D-084, 2026-10-08):** Katılımsız Kurulum › Hesap › Karşılama ekranı. Yanıt dosyası
  geçici `WinLoveSetup` hesabıyla bir kez oturum açtırır, WinLove'un tam ekran sihirbazı hesabı / bilgisayar adını / görünümü /
  gizliliği sorar, hesabı kurar, geçici hesabı siler. VM'de uçtan uca ALL PASSED. ISO Oluştur betik imajda yoksa reddeder.
  Sırada (fikir): sihirbaza Programlar paketleri ve duvar kağıdı sayfası; insan eliyle VM denemesi (VNC ile tıklama).
- **Görev çubuğu sabitlemeleri (D-083, 2026-10-07):** Başlat menüsü › Görev çubuğu sekmesi (Windows varsayılanı / Boş /
  Kendi listem, "Kullanıcı kaldırabilsin"); Microsoft'un Başlangıç Düzeni ilkesi, VM'de Edge/Store/Outlook yer tutucusu
  olmadan yalnız seçilenler. Eski "Sabitlenmiş uygulamalar" anahtarı kaldırıldı. NTLite listesinin 4 maddesi bitti.
  Sırada: **kendi OOBE'miz** (kullanıcı seçti; önce VM deneyleri), istenirse daha geniş bileşen listesi (D-059 taraması
  zaten 1227 aileden isteğe bağlı olanları aldı: yenisi paket paket VM denemesi ister).
- **Uyumluluk korumaları (D-082, 2026-10-07):** Bileşenler başlığında "Uyumluluk · N" (13 koruma, 7'si varsayılan açık),
  kalan uygulamaların çalışma zamanları (manifestten) her zaman korunur, Programlar seçiliyse App Installer; Bileşenler ve
  Servisler'de kilit, çakışan işlemler kuyruktan çıkar. Ayrıca: Programlar penceresinin kaydırma çubuğu temaya uydu (958a434).
  Sırada: daha geniş bileşen listesi (D-059 gibi paket paket denenecek), (4) görev çubuğu sabitlemeleri, kendi OOBE'miz.
- **Modlu Windows yedeği (D-081, 2026-10-07):** wimgapi dosya başına seçilir (bu PC'ninki → Windows ADK'nınki → kurulum
  ortamının `sources\wimgapi.dll`'i, Microsoft imzası şart); DISM'in bir parçası eksikse ADK'nın DISM'i; açılışta sağlık
  denetimi → Kaynak sayfasında uyarı; hata metinleri kullanılan kopyayı ve okunamayan ESD'nin katı kaynaklarını söyler.
  `wlcli host-check`, `wlcli wimgapi`, `--wimgapi=`, `--dism=adk`. Sırada: (3) uyumluluk korumaları, (4) görev çubuğu
  sabitlemeleri, sonra kendi OOBE'miz.
- **NTLite eksik listesi (2026-10-07 akşam, kullanıcı seçimi):** (1) sürücü deposu temizliği ölçüldü — 715 paket 443 MB,
  238 MB'ı ağ (kural gereği yok), Hello yüz zaten Özellikler'de yetenek, yazıcı çekirdeği kaldırılırsa sonradan yazıcı
  kurulamaz → kullanıcıyla anlaşılarak yapılmadı. (2) **Kurulum ortamı güncellemesi (D-080):** Safe OS → WinRE (Uygula),
  toplu güncelleme → boot.wim'in iki sürümü + kurulum dosyaları (ISO Oluştur › "Ortam güncellemesi"), Setup güncellemesi →
  `sources\`. Microsoft'un yalnız setup.exe/setuphost.exe kopyalayan yöntemi bizde kurulumu bozdu (0xC1900100, Setup
  Platform sürümü) → ortamın `sources\`'ı boot.wim'inkiyle eşitlenir; iki VM masaüstüne kurdu. Yeni lab araçları:
  `lab_winre.ps1`, `lab_media.ps1`, `lab_setup_logs.ps1` (WinPE'deki kurulum günlükleri), `lab_vm.ps1 -LogDisk -IsoArgs`.
  Sırada (kullanıcı): (3) uyumluluk korumaları, (4) görev çubuğu sabitlemeleri; araya: modlu Windows'ta DISM/wimgapi yedeği
  (bir kullanıcıda ESD→WIM 0x8007000B); sonra **kendi OOBE'miz** (kullanıcı seçti).
- **Çökme izi + düzeltme (2026-10-07):** yakalanmayan istisna `logs\crash-*.txt` (sembollü yığın) + `.dmp` yazar. İlk
  bulduğu: açılır liste açıkken kapanışta `Dropdown` yıkıcısı ölü menüye erişiyordu (render'da her açık listede) — düzeltildi.
- **AIO, 1. adım (2026-10-06, D-077):** ölçüldü: Win11 25H2 kurulum ortamı Win10 kuramaz (yeni ve önceki kurulum), Win10 ortamı ikisini de kurar. İmajlar'da "Yukarı / Aşağı taşı" (`wlcli reorder`), sürüm eklemede ad çakışmasına sürüm etiketi. Win10 22H2 TR ESD: `build\lab\win10\win10_22h2_tr_consumer.esd` (kullanıcı da kullanacak, silinmez). Lab: geçme = ACPI soft-off. **2. adım bitti:** ISO sayfası karışık AIO'yu uyarır, "Windows 10 ortamını al…" kurulum dosyalarını değiştirir (`wlcli setup-media`, lab'da Win10 + Win11 kuruldu). **Bir sonraki adım:** kullanıcı uygulamadan dener (Win11 ISO aç → Win10 ESD'den sürüm ekle → ISO Oluştur'da uyarı + düğme); ardından kullanıcının seçeceği tasarım iyileştirmeleri (öneri listesi: Özet sayfası, gezinme grupları, tablo iyileştirmeleri, Tweaks arama/filtre, Kaynak sayfası, animasyon, tema).
- **Preset tarifleri yenilenir (2026-10-06, D-076):** kullanıcı D-075 düzeltmesini doğruladı (kurulum çalıştı); görev çubuğunda Outlook vardı → preset'in eski Outlook tarifi. Preset uygulanırken bileşen tarifleri katalogdan yenileniyor. Lab `fixed` (D-075, kullanıcının preset'i, ağsız): ALL PASSED, görev çubuğunda Outlook yok. **Bir sonraki adım:** kullanıcı yeni `dist\WinLove.exe` ile preset'i yeniden yükler + Başlat › "Bulutun eklediği sabitlemeler ve içerik" açık → ağlı kurulumda Outlook olmamalı (VM'de doğrulanmadı).
- **İlk açılışta donma bulundu (2026-10-06, D-075):** sebep "Telemetri ve tanılama" bileşeni (TroubleShooting paketi); 14 lab kurulumuyla ikiye bölündü. Bileşen artık servisleri kapatıyor, çekirdek paketi asla kaldırmıyor, eski preset'ler okunurken dönüştürülüyor. `dist\WinLove.exe` yeni. "Donanım saati UTC" ayarı kullanıcının VM'inde saati 3 saat kaydırdı (ayar doğru, hepsi işaretlendiği için açıldı). Lab: `tools\lab_vm.ps1 -Cpus/-MemMB` (NVMe vmrun ile açılmıyor). **Bir sonraki adım:** kullanıcı orijinal ISO'dan preset'i yeniden uygulayıp kurar; lab `fixed` koşusunun sonucu `build\lab\out\vm-fixed-test.log`.
- **Yayın (2026-10-06, kullanıcı isteği):** `v1.0.2-alpha` GitHub'da (prerelease, `dist\WinLove.exe`, SHA-256 `E927C13A…67C1`): D-074 (Önceki kurulum, WinRE'siz 24H2+ imajda kendiliğinden). 311+ birim testi geçti (`-Dist`).
- **Bir sonraki somut adım (2026-10-06):** kullanıcı `dist\WinLove.exe` ile: bir imaj bağla → Simgeler › İkon paketi yükle… › Arşivden → `7TSP Lumicons Symbols.7z` → Uygula; kısa pencerede gezinme çubuğunu kaydır; çok yüksek riskli işlemle Uygula'da onay dialogunu dene.
- **Windows'un kendiliğinden kurdukları (2026-10-05 gece, D-070):** VM'de ölçüldü (ağ açık, işlem denetimi + AppX günlüğü): OneDrive Default profil Run kaydından, Outlook / Teams / Dev Home / Cihazlar Arası Windows Update zamanlayıcısından (`UScheduler_Oobe`) kuruluyor. Bileşenler sayfasında yeni grup (7 kanal); E/F (Pro, Home): hiçbiri kurulmadı. **Düzeltildi:** `appx` içeren bileşen tarifinden sonra HKLM yazımları 0x80070020 ile düşüyordu (DISM hive'ı tutuyordu). **Kalan:** görev çubuğundaki Outlook / Edge / Store yer tutucuları — `LayoutXMLPath` düzeni varsayılanların yerine geçmiyor, üstüne ekliyor (sıradaki işte).
- **Sıradaki iş (kullanıcı isteği, 2026-10-05):** Görev çubuğu: ögeleri silme ve uygulama sabitleme (Başlat menüsü sayfasının yanına). Not: Windows 11 görev çubuğu sabitlemeleri `LayoutModification.xml` (`CustomTaskbarLayoutCollection`, `PinListPlacement="Replace"`) ile — Başlat'ın "Boş" modunun yazdığı dosyayla aynı yol, tek dosyada birleştirilmeli; VM'de her sürümde ölçülecek.
- **Bir sonraki somut adım (2026-10-05 akşam):** kullanıcı `dist\WinLove.exe` ile Simgeler › Sistem simgeleri: bir imaj bağla, imageres.dll'de birkaç simgeyi .ico / PNG ile değiştir, Uygula; ardından Ayarlar'daki yeni tweak'ler ve Kayıt Defteri.
- **(eski) Sıradaki iş (kullanıcı isteği, 2026-10-05):** Simgeler sayfası baştan, özenle: `.dll.mun` / `.dll` / `.exe` ikon kaynaklarını
  toplu okuma, bütün simgeleri listeleme, dosyanın içindeki herhangi bir simgeyi değiştirme, orijinali yedekleme, ikon paketleri;
  yamalamada önyükleme döngüsü / bileşen deposu bozulması / güncellemenin geri alması riskleri önce lab'da ölçülecek (kural 6).
  Kullanıcı iki modu da istedi: güvenli (kayıt yönlendirmesi, D-065) **ve** dosya yaması (D-065'teki "yamalanmaz" kararı
  yeni bir kararla değişecek). Gözlem: bu makinede 142 `.mun` (imageres 23 MB, shell32 17 MB, DDORes 16 MB); `SystemResources\*.mun`
  WinSxS'teki dosyaya sabit bağlantı (2 bağlantı) → yama yeni dosya yazıp bağlantıyı koparmalı, WinSxS'e dokunmamalı.
- **Bir sonraki somut adım (2026-10-05):** kullanıcı yeni sürümü dener: Simgeler (bir ikon paketi klasörü), Uygulamalar › Mağazadan
  ekle, Kaynak'taki bağlı imajlar, Ayarlar'daki yeni satırlar; ardından VM'de kurulum (duvar kağıdı / Spotlight, simgeler, Defender,
  sanal bellek, Setup'ın dil listesi).
- **Önceki adım (2026-10-04):** kullanıcı Diller › "Dil ekle…" ile en-US'i uygulamanın içinden indirip Uygula'yı
  dener (imaj 26200.8037: uyarıdaki "Güncellemeleri bul" ile aynı LCU'yu da kuyruğa almak önerilir); ardından VM'de kurulum.
- **Önceki adım (2026-10-02):** kullanıcı yeni bileşenleri ve derin kaldırmayı dener; VM'de derin kaldırılmış imajın
  kurulumu ve Windows Update davranışı görülmeli.
- **Önceki adım (2026-10-01 gece):** kullanıcı yeni araçları uygulamada dener. Sonra ertelenen öneriler (`deferred-suggestions`).
- **Önceki adım (2026-10-01 akşam):** kullanıcı yeni sayfaları uygulamada dener (Kişiselleştirme, Wi-Fi, Compact OS, boot.wim sekmesi). WLM için karar bekleniyor: açma hızı ölçümü + kurucu prototipi (D-057). Önerilen küçük işler: ISO seçeneklerini presete eklemek, taşınabilir preset (dosyalar yanında).
- **Önceki adım (2026-09-30 akşam):** (`tools\lab_usb.ps1` MBR geçti; kalan `-Gpt` ve gerçek bellek) uygulamada P11 / P12 "imajda" gösterimini önceden Uygula'lanmış bir imajla ve
  "Güncellemeleri bul" akışını dener. Ardından (önceki adım sürüyor) VM'de kendi imajını deniyor (`docs/TESTING.md` → "VM kabul testi");
  testten gelen düzeltmeler sırayla. Log: `%LOCALAPPDATA%\WinLove\logs\WinLove-*.log` (oturum başına bir dosya).
- **Üçüncü tur (2026-10-01 akşam, kullanıcı seçimi 1,2,3,4,5,6,8):** yeni **Kişiselleştirme** sayfası (OEM + logo, varsayılan masaüstü / kilit ekranı / hesap resmi, yazı tipleri), Kurulum Sonrası'nda **Wi-Fi ağı**, Katılımsız Kurulum'da **Compact OS**, Sürücüler'de **Kurulum ortamı (boot.wim)** sekmesi (D-056). WinRE kaldırma ve OEM metin alanları zaten vardı. `tools\lab_branding.ps1` ALL PASSED (yönetici: bu makinede UAC "sormadan yükselt", testleri kendim çalıştırdım). **ESD hatası düzeltildi** (D-057): ESD yeniden paketleme sıkıştırmasız yazıyordu. **WLM araştırması:** ESD'den −%12,9 (D-057), ürün değil. Görülmeyen: kurulan sistemde etki (VM).
- **İkinci özellik turu (2026-10-01, kullanıcı seçimi "1,2,3,4,6,7,8,15", gece otonom):** yeni sayfalar **Uygulamalar**
  (.appx / .msix provision + varsayılan uygulamalar, D-050 / D-054), **Diller** (dil paketleri + arayüz dili / yerel /
  klavye / saat dilimi, D-053), **Görevler** (zamanlanmış görevler, kurulum sonrası schtasks, D-048), **Hosts** (+ Ayarlar ›
  Ağ'da DNS / DoH, D-049), **Dosyalar** (bilgisayardan imaja, D-051); Sürücüler'e **İmajdaki sürücüler** sekmesi (listele,
  kaldır, bu bilgisayarınkini al, D-052); Uygula'da **Diğer sürümlere de uygula** (D-055). Açılır menü artık kaydırılıyor
  (uzun listeler: saat dilimleri). 250 test / 6049 doğrulama, build temiz. **Kanıt:** birim testleri, render
  (`--demo-tasks/-hosts/-files/-image-drivers/-apps/-languages/-editions`), gerçek Windows Terminal paketinde manifest +
  bağımlılık bulma. **Gerçek imajda kanıtlandı (kullanıcı, yönetici, 2026-10-01 11:51, `tools\lab_features.ps1`): ALL PASSED** (sürücü
  listele / ekle / kaldır, pnputil dışa aktarma, intl oku / yaz / geri oku, ilişkilendirme, Terminal provision, görev +
  hosts + dosya kuyruğu commit ve `--also=2` ile ikinci sürüm, WIM tek kez yeniden yazıldı; ayrıntı D-048…D-055).
  **Görülmeyen:** dil paketi ekleme (medya yok), bunların uygulamanın içinden yapılması, kurulan sistemde etki
  (görevler kapandı mı, hosts, DNS, varsayılan tarayıcı, Terminal yeni kullanıcıda): VM.
- **USB'ye yazma (2026-09-30, kullanıcı seçimi — üç özellikten 3.sü, D-047):** ISO sayfasının USB sekmesi: yalnız
  USB / SD diskleri (sistem diski asla), MBR (BIOS + UEFI) / GPT (UEFI), FAT32, 4 GB'tan büyük install.wim → .swm,
  yanıt dosyası + boot.wim atlamaları ISO'daki gibi; her zaman görünen silme uyarısı + adıyla onay. **Kanıt:** disk
  listeleme gerçek makinede; birim testleri; hat testi (sahte yazıcı); render. **Gerçek yazma kanıtlandı (kullanıcı, yönetici, 2026-09-30 23:33, `tools\lab_usb.ps1`, MBR): 26 / 26 PASS** — VHDX disk 1 (dosya destekli sanal) yalnız `--allow-virtual` ile listelendi; `--yes` olmadan reddedildi; diskpart 1,4 sn (clean, MBR, FAT32, active, D:); bootsect FAT32 + MBR önyükleme kodunu yazdı; install.wim (6882 MB) 2 .swm parçasına bölündü, DISM 6 sürümü okudu; önyükleme sektörü 55 AA + BOOTMGR; 7733 MB 14 sn (VHDX, önbellek). **Görülmeyen:** `-Gpt`, gerçek bellek
  ve ondan önyükleme (UEFI + BIOS).
- **Güncelleme indirme (2026-09-30, kullanıcı seçimi — üç özellikten 2.si, D-046):** Güncellemeler sayfasında
  "Güncellemeleri bul": Microsoft Update Catalog'dan bağlı imajın sürümüne uygun en yeni LCU ve .NET → seçim dialogu
  → `<çalışma kökü>\updates\`e doğrulamalı / devam ettirilebilir indirme → kuyruk. **Kanıt:** `wlcli catalog` üç
  hedefte (11 25H2 x64, 10 22H2, 11 24H2 arm64) doğru teklif; .NET CU gerçekten indirildi, SHA-256 tuttu, önbellek ve
  Range ile devam çalıştı; birim testleri; render. **Görülmeyen:** uygulamanın içinden indirme (ağ + dialog akışı
  gerçek pencerede) ve 4,8 GB'lık LCU'nun Uygula'da imaja eklenmesi.
- **İmajdaki mevcut değerler (2026-09-30, kullanıcı seçimi — üç özellikten 1.si, D-045):** Kayıt Defteri ve Ayarlar /
  Tweaks artık bağlı imajın durumunu gösterir ("imajda"); imajdakini kaldırmak geri alma işlemi kuyruklar. Okuma
  `offreg.dll` ile (yönetici / RegLoadKey yok), bağlamadan sonra ~0,4 sn. `wlcli reg-check`. **Kanıt:** bozulmamış
  25H2 Pro hive'larında 277 katalog yazımından yalnız beklenen 3'ü "imajda"; birim testleri (sentetik hive + P11/P12
  mantığı); render. **Görülmeyen:** uygulamada gerçek bağlı imajda (önce Uygula'lanmış bir imajı bağlayıp P11/P12'ye
  bakmak). Yan bulgu: "Paylaş" menüsü ayarı çevrimdışı etkisizdi → kurulum sonrası da uygulanıyor (VM'de görülmedi).
- **Kullanıcının ilk gerçek Uygula'sı (2026-09-30, 148 işlem, 143 geçti, commit tamam):** bulunanlar ve düzeltmeler —
  durum çubuğundaki Uygula düğmesi Uygula sayfasında tepkisizdi (artık başlatıyor); Katılımsız Kurulum'da dört
  düğme ters çalışıyordu ve Ayarlar / Tweaks'te düğmenin anlamı belirsizdi (D-032); atlanan 5 adımın nedeni yalnız
  logdaydı (Tamamlandı ekranı artık adım + neden listeliyor); SecHealthUI / DesktopAppInstaller ve kalıcı capability
  artık seçilemiyor; paketiyle giden özelliği kapatmak başarı sayılıyor; commit sonrası WIM `[DELETED]` artıkları
  olmadan yeniden yazılıyor (`optimizeWim`, gerçek imaj kopyasında doğrulandı). Ayrıntı: ENGINE saha notları.
  **Hâlâ görülmeyen:** ISO + VM kurulumu.
- **Kaynak sayfası (2026-09-30, kullanıcı isteği):** son kullanılanlardan kaldırma (satır sonunda ×, `Del`, sağ tık
  menüsü) + çalışma kopyasını silme dialogu; UI çatısına sağ tık yönlendirmesi eklendi (`Widget::onContextMenu`).
  Dialogun kendisi render'da görülmedi (diskte çalışma kopyası yoktu); liste girdisini kaldırma ve menü görüldü.
- **Güç planı, ağ, klasik Fotoğraf Görüntüleyicisi (2026-09-30, kullanıcı onayı, D-044):** Kurulum Sonrası'na
  **Hazır komutlar** (11 komut: Yüksek / Nihai performans güç planı, uyku / ekran / disk süreleri, USB seçmeli askıya
  alma; ağ bulma, dosya ve yazıcı paylaşımı, Uzak Masaüstü, ping, ağları Özel yap). Ayarlar › Sistem › **Ağ**: LLMNR,
  IPv6 (varsayılan / IPv4'ü tercih et / kapalı), Wi-Fi etkin noktaları, yeni ağ sorusu, konuk SMB, SMB imzalama.
  Sistem › Diğer: **Klasik Windows Fotoğraf Görüntüleyicisi**. 121 ayar, 214 test. VM'de görülmedi.
- **Üç eksik alan (2026-09-30, kullanıcı: "eksiklerden 3 tane seç", D-043):** (1) **Sağ tık menüsü** — "Sahipliği al"
  (Türkçe / İngilizce etiket; `runas` fiili, Yöneticiler SID ile), "Klasöre kopyala / taşı", "Paylaş"ı kaldır;
  (2) **Bildirimler** (Sistem sekmesi) — uygulama bildirimleri, kilit ekranında, sesler, güvenlik ve bakım;
  (3) **Oyun** (Performans sekmesi) — Xbox Game Bar, Oyun modu, donanım hızlandırmalı GPU zamanlaması, pencereli oyun
  iyileştirmeleri. 114 ayar. Birim testli + render; VM'de görülmedi. Kısayol oku kaldırma bilinçli olarak eklenmedi
  (boş simge kaynağı Windows sürümüne göre siyah kare verebiliyor; ikili .ico taşıma yolu yok).
- **Tema / vurgu rengi (2026-09-30, kullanıcı isteği, D-042):** Uygulama ayarları artık sol menüde; temalar (Koyu,
  Açık, Yüksek kontrast, Sistem) zaten vardı. Eklenen: ekran 19'daki beş **vurgu rengi** (Bakır, Deniz, Nar, Gök,
  Zeytin) — kaydedilir, anında uygulanır. Render'da doğrulandı (koyu + Deniz, açık + Nar); kontrast testli.
- **Ayar kataloğu +38 ayar ve Sistem sekmesi (2026-09-30, kullanıcı: "daha çok tweak", D-042):** gizlilik /
  ipuçları / arama / depolama (ayrılmış depolama, uzun yollar) / oturum açma / Gezgin gezinti bölmesi / Başlat düzeni /
  görev çubuğu birleştirme / MRT / Store güncellemeleri / UAC / SmartScreen / Num Lock / fare hızlandırma / yapışkan
  tuşlar / AutoPlay. Toplam 103 ayar, 7 sekme. VM'de görülmedi.
- **Yedi yeni özellik (2026-09-30, kullanıcı seçimi 1,2,3,4,5,7,8; D-041):** Ayarlar / Tweaks'e görev çubuğu
  (sabitlemeler → yalnız Dosya Gezgini, sohbet / toplantı / Cortana düğmeleri), Copilot, Recall, Edge (ilk çalıştırma,
  kısayol, arka plan), BitLocker otomatik şifreleme, yeni **Güncelleme** sekmesi (otomatik güncelleme, yeniden
  başlatma, WU sürücüleri, özellik güncellemesi erteleme, teslim iyileştirme), masaüstü simgeleri, **duvar kağıdı**,
  **kilit ekranı resmi**, **OEM bilgisi** (yeni metin / dosya denetimleri, yeni `CopyFile` işlemi). Kurulum
  Sonrası'na **Hazır uygulamalar** (42 winget paketi, çoklu seçim). Uygula bitince **Raporu kaydet** (HTML).
  **Kanıt:** 210 birim testi + render'lar; gerçek imajda Uygula ve VM'de etkileri görülmedi.
  **Bir sonraki somut adım:** kullanıcı bu ayarlarla Uygula → ISO → VM; bozuk çıkanı logla birlikte bildirir.
  Kullanıcı ayrıca sürüm yükseltmenin yerini sordu: yalnız imaj bağlıyken İmajlar sayfasında görünüyor (anlatıldı;
  bağlı değilken de gösterip "önce bağla" demek açık öneri).
- **Başlat menüsü temizliği (2026-09-30, kullanıcı isteği, D-040):** Ayarlar / Tweaks → Başlat menüsü sekmesine
  "Sabitlenmiş uygulamalar ve kutucuklar" (Windows 11: `ConfigureStartPins`; Windows 10: `LayoutModification.xml` —
  yeni kuyruk işlemi `WriteFile`); "Reklam uygulamalarının otomatik kurulumu" varsayılan profilin
  `ContentDeliveryManager` değerleriyle genişledi (artık ilk oturumda da uygulanır); "Widget'lar" Windows 10'un
  "Haberler ve ilgi alanları"nı da kapatır. **Kanıt:** birim testleri + render; gerçek imajda Uygula çalıştırılmadı,
  kurulan Windows'taki etkisi görülmedi. **Bir sonraki somut adım:** kullanıcı üç ayarı kapatıp Uygula → ISO → VM.
- **OneDrive ve yeni Outlook OOBE'de kurulmasın (2026-09-30, kullanıcı VM testi, Windows 10, D-039):** kullanıcı
  Windows 10'da her şeyin çalıştığını, ama internet varken OneDrive ve Outlook'un yine kurulduğunu bildirdi. Neden:
  OneDrive bileşeni Windows 10'da listelenmiyordu (dosya `SysWOW64`'te); Outlook bir güncelleme kaydıyla iniyor.
  Düzeltme: `onedrive` tarifi Windows 10 yollarını da içeriyor; yeni, her imajda sunulan bileşen "Yeni Outlook'un
  kendiliğinden kurulması". **Kanıt:** yalnız katalog + unit test + render; gerçek imajda çalıştırılmadı, OOBE'deki
  etkisi görülmedi. **Bir sonraki somut adım:** kullanıcı Bileşenler'de ikisini seçip Uygula → ISO → internetli VM.
- **Üç yeni iş (2026-09-30, kullanıcı isteği, D-037 / D-038):**
  (1) **Yanıt dosyası kalıcı** — `answers.dat` (DPAPI), her değişiklikte yazılır, açılışta geri gelir; unit testli. **Bitti.**
  (2) **DISM'in reddettiği uygulamaları kendi kodumuzla kaldırma** — motor kullanıcının yönetici çalıştırmasında
  gerçek imajda kanıtlandı (`tools\lab_appx.ps1`, 25H2 Pro: SecHealthUI ve DesktopAppInstaller `0x80073CFA` → yerel
  kaldırma; dosyalar, `Applications` / `Staged` gitti, `Deprovisioned` yazıldı, DISM artık listelemiyor, paket listesi
  okunuyor; yapıştırılan çıktı son discard adımında kesildi, o ana dek 27 PASS / 0 FAIL). Ardından **Applier'a bağlandı**
  (`RemoveAppx` `0x80073CFA` alınca `removeAppxNative`) ve katalogdaki kilit kaldırıldı: iki uygulama artık diğerleri
  gibi seçilir (risk Yüksek, uyarı bandı). **Görülmeyen:** uygulamanın kendi Uygula akışında bu yol (yalnız `wlcli`
  ile denendi); commit edilmiş imajdan kurulan Windows'ta Windows Güvenliği / winget'in durumu (VM); Windows 10'da
  yerel kaldırma (kayıt düzeni aynı varsayıldı, denenmedi).
  (3) **boot.wim: gereksinim atlamaları Setup'ın kendi imajına** — motor kullanıcının yönetici çalıştırmasında
  kanıtlandı (`tools\lab_boot.ps1`, 14 / 14: önyükleme index'i 2'ye beş `LabConfig` değeri yazıldı, commit, yeniden
  bağlanıp okundu, index sayısı ve önyükleme index'i aynı, 13 869 akış sağlam; yama 27 sn). Ardından **ISO sayfasına
  bağlandı**: ÖNYÜKLEME altında "Gereksinim atlamalarını boot.wim'e de yaz" (varsayılan açık; Katılımsız Kurulum'da
  seçili atlamaları yazar, yanıt dosyasının ISO'ya girmesine bağlı değil), özet kutusunda "Kurulum ortamı" satırı,
  yeni ilerleme aşaması. boot.wim'in **kopyası** yamalanır ve ISO'da klasördeki dosyanın yerini alır — kurulum
  klasörü değişmez, kutu kapatılınca geri alınacak bir şey kalmaz. Unit test (yamalayıcı yerine sahte) + iki render.
  **Görülmeyen:** uygulamanın içinden gerçek bir ISO üretimi (DISM'li yol yalnız `wlcli` ile denendi); o ISO'nun
  TPM'siz VM'de kurulumu; boot.wim'e **sürücü** ekleme (betik `-Driver` olmadan çalıştı → arayüzü yazılmadı).
  **Bir sonraki somut adım:** kullanıcı uygulamada atlamalı bir ISO üretir, TPM'siz VM'de dener.
  **boot.wim'e sürücü ekleme ertelendi (kullanıcı kararı, 2026-09-30: elinde INF yok):** motor ve `wlcli boot-patch
  --driver=` duruyor ama kanıtsız; arayüzü yok. Ele alınırsa önce `tools\lab_boot.ps1 -Driver <inf>` geçmeli.
- **İmajlar — kısayollar (2026-09-30, kullanıcı isteği):** bağlama klasörünü aç (`Ctrl+E`, her sayfadan; inspector'da
  klasör düğmesi; sağ tık), komut istemini bağlama klasöründe aç, dosya konumunu aç (`Ctrl+Shift+E`), bilgileri
  kopyala (`Ctrl+C`), kaynağı yenile (`F5`). **Kullanıcının isteğiyle denenmeden teslim edildi:** yalnız derlendi,
  var olan birim testleri geçti; yeni test / render yok. Kullanıcı deneyecek.
- **Setup "<ProductKey> ayarını okuyamıyor" (2026-09-30, kullanıcı VM testi, Windows 10 22H2, D-036):** yanıt
  dosyasında `UserData` (EULA kabulü) `ProductKey`'siz yazılıyordu. Artık her zaman anahtarla yazılır: kullanıcının
  anahtarı / kurulacak sürümün genel anahtarı / sürüm belli değilse yer tutucu + "Setup sorsun". 16 genel anahtar
  imajın kendi `pkeyconfig`'ine karşı doğrulandı. **VM'de yeniden denenmedi.** Aynı oturumun logundan: sürüm silme
  (6 → 1 tek seferde), Uygula (132 / 134) ve ISO'ya `autounattend.xml` eklenmesi Windows 10'da da çalıştı;
  `Microsoft.DesktopAppInstaller` Windows 10'da da kaldırılamıyor (`0x80073CFA`) — artık Applier her sürümde bu
  kodda yerel kaldırmaya geçiyor (D-038); Windows 10'da denenmedi.
- **İmajlar sayfası — dört yeni özellik (2026-09-30, kullanıcı isteği, D-035):**
  (1) **Yeniden adlandır** (kalem / `F2` / sağ tık): `core::setImageText`, gerçek imaj kopyasında denendi (Türkçe
  karakter, `&` `<`; yönetici gerekmez). (2) **Çoklu seçim**: onay kutusu, Ctrl / Shift, Ctrl+A; seçilenleri sil,
  "yalnız seçilileri tut", tek WIM'e dışa aktar. (3) **Doğrula**: kendi doğrulayıcımız (`WimVerify` + LZX çözücü) —
  lab WIM'inde 94.409 akış 9 sn'de hatasız, ISO içinden yerinde 12 sn, XPRESS de; üç bit çevrilince tam üç bozuk akış
  bulundu. (4) **Sürümü yükselt** (bağlı sürümde; kuyruk işlemi, Uygula'nın ilk adımı): motor kullanıcının yönetici
  olarak çalıştırdığı `tools\lab_edition.ps1` ile kanıtlandı (Home → Pro 28 sn, commit, 14 / 14 denetim), ardından
  UI yazıldı: dialog, inspector düğmesi, sağ tık, Uygula özetinde "Sürümü yükselt" grubu, commit sonrası ad güncelleme.
  Render'da görülenler: çoklu seçim + menü, yeniden adlandırma ve yükseltme dialogları, doğrulama şeridi, sağlam /
  bozuk çubuğu, kuyruktaki yükseltme. **Uygulama içinde denenmeyen (kullanıcı testi):** gerçek yeniden adlandırma ve
  doğrulama akışı, fareyle Ctrl / Shift, ve **Sürümü yükselt → Uygula'nın baştan sona akışı** (motor adımları tek tek
  kanıtlı; Uygula içinde birlikte ve commit sonrası ad yazımı uygulamada hiç çalışmadı).
  **Bir sonraki somut adım:** kullanıcı `dist\WinLove.exe` ile Home'u bağlayıp Pro'ya yükseltir, ISO + VM'de dener.
- **Yanıt dosyası ISO'ya girmiyordu (2026-09-30, kullanıcı VM testi, D-034):** "ISO'ya ekle" kutusu varsayılan
  kapalıydı ve fark edilmedi → ISO'da `autounattend.xml` yok → Setup "gereksinimler karşılanmıyor". Düzeltme: ilk
  yanıt / içe aktarma kutuyu açar; ISO sayfası özetinde "Yanıt dosyası" satırı hep görünür (kapalıysa turuncu);
  ISO logu `answer file: …` yazar; Gereksinimler'e işlemci + disk boyutu denetimi eklendi. Form → ISO → kökte
  `autounattend.xml` akışı unit testte gerçek ISO üretilerek doğrulandı. **VM'de hâlâ görülmeyen:** Setup'ın bu
  dosyayı kabul edip denetimleri atlaması. Aynı logdan: uygulanmış imaja preset ikinci kez uygulanınca 44 AppX adımı
  "dosya bulunamadı" ile hata sayılıyordu (48 başarısız) → imajda olmayan uygulama artık başarı.
  Kullanıcının uygulama içi sürüm silmesi (6 → 5 → 1, Home Single Language) logda hatasız.
- **Sürüm silme (2026-09-30, kullanıcı isteği, D-033):** özellik P02'de vardı ("Index'i sil…") ama ISO açıkken
  kapalıydı, dosyayı küçültmüyordu ve tek tek siliyordu. Şimdi: `core::removeImages` (kalan sürümleri yeni dosyaya
  export + yer değiştirme; iptal edilebilir), ISO kaynakta da çalışır (önce çalışma klasörüne kopya), satıra sağ tık
  menüsü + "Yalnız bu sürümü tut…" + `Del`, kapalı düğmenin nedeni tooltip'te, yanıt dosyasındaki sürüm index'i
  kaydırılır. **Motor gerçek imaj kopyasında doğrulandı** (`tools\lab_editions.ps1`, yönetici gerekmez): 6 → 1 sürüm
  8 sn, 6,72 → 6,48 GB, 7-Zip akış testi hatasız. Menü ve dialoglar render'da görüldü; sağ tık → onay → silme zinciri
  unit testte. **Uygulama içinden gerçek silme (ilerleme şeridi, ISO kopyalama adımı) kullanıcı testi bekliyor.**
- **P07 v2 — sistem bileşenleri ve depo temizliği (2026-09-30, D-031):** `core/image/SystemComponents` (tarif: CBS
  paket aileleri + yollar + kayıt yazımları; gizli paket `Visibility` / `Owners` ile açılıp `DismRemovePackage`;
  junction'dan geçen yol reddi), `core/image/dism/StoreCleanup` (`dism.exe /StartComponentCleanup /ResetBase`, kendi
  aşaması: güncellemelerden sonra), `resources/catalog/components.json` (Edge, WebView2, Edge Güncelleyici + EdgeCore, OneDrive kurulumu,
  WinRE, depo temizliği), Bileşenler sayfasında "Sistem Bileşenleri" + "Temizlik" grupları, `wlcli cbs | component |
  store-cleanup`. Test imajı yönetici gerektirmeden incelendi (7-Zip + hive okuyucu): **Defender 25H2'de ayrı paket
  değil → kaldırma sunulmuyor**; FoD'lar zaten Özellikler'de. **Gerçek imajda denendi (kullanıcı, yönetici,
  `lab_components.ps1`, discard):** OneDrive'ın 5 gizli paketi DISM ile kaldırıldı, Edge klasörü silindi, imaj
  servislenebilir kaldı. Depo temizliği de çalıştı (13 sn: dokunulmamış imajda temizlenecek şey yok → temizlik
  planın başından güncellemelerin sonrasına taşındı). **Denenmeyen:** güncelleme eklenmiş imajda temizlik, commit +
  VM kurulumu. Yan düzeltmeler:
  Uygula listeleri / onay dialogu uygulamaları paket tam adıyla değil katalog adıyla gösteriyor; korumalı kayıt
  anahtarı silme tanıtıcı üzerinden (`deleteKeyByHandle`).
- **P18 Komut Paleti (2026-09-30):** `shell/PaletteIndex` (sayfalar, P12 ayarları, okunan bileşen / özellik / servis
  listeleri, o an çalışabilen komutlar; Türkçe-duyarlı katlama, puanlı eşleşme) + `shell/CommandPalette` (modal;
  `ui::SearchBox` "bare" + soluk tamamlama). Enter → sayfa + `reveal` (satır seçilir / kontrol odaklanır). Yeni
  kısayollar: `Ctrl+K`, `Ctrl+Enter`, `Ctrl+S`, `Ctrl+O`; dialog açıkken uygulama kısayolları artık çalışmıyor.
  Render'da `--palette[=sorgu]` ve `--keys=`. Unit testli (puan, durum, widget klavye / tık); gerçek pencerede
  klavye akışı kullanıcı testi bekliyor.
- **P17 Hakkında (2026-09-30):** `app/SystemInfo` (DISM yolu / sürümü, derleme tarihi, mimari — Ayarlar da buradan
  okur), `AboutPage` (`F1`): sürüm satırı, DISM / çalışma dizini / fontlar / üçüncü taraf, Lisanslar dialogu, log
  klasörünü aç. **Konmayan (spec §4):** Lisans satırı (proje lisansı seçilmedi), Güncellemeleri denetle (servis yok).
- **P16 Uygulama Ayarları (2026-09-30):** `AppSettings` (tema, hareketi azalt, dil, çalışma / bağlama klasörü),
  `SettingsPage` (`Ctrl+,`), `App::applySettings` (tema canlı, "Sistem" Windows'u izler; dil değişince arayüz durum
  korunarak yeniden kurulur — `--switch-lang` render'ıyla doğrulandı). **Ertelenen (spec §4):** vurgu rengi (token
  seti gerek), yoğunluk (çalışma zamanı ölçü işi), "işlem sonrası mount'u çöz" (P05 ekranları). Gerçek pencerede
  tema / dil değişimi kullanıcı testi bekliyor.
- **P15 Presetler (2026-09-30):** `app/state/Preset` (dosya biçimi, D-030), `PresetController` (kitaplık klasörü,
  adlandırılmış öğeler, A / B farkı), `PresetsPage`. Unit testli (dosya, kitaplık, öğeler, fark, yükleme), render'da
  doğrulandı; dosya diyalogları ve gerçek kitaplık klasörü kullanıcı testi bekliyor.
- **Önce kullanıcı testi iyi olur:** P05–P14 sayfalarının hepsi "test bekliyor". En riskli, hiç denenmemiş üç şey
  aynı VM kurulumunda görülebilir: (1) SetupComplete.cmd satırları (kayıt + kurulum sonrası), (2) ilk oturum
  görevi / RunOnce, (3) autounattend.xml'in Setup tarafından kabulü.
- **P14 Kurulum Sonrası (2026-09-30):** `core/postsetup` (plan JSON, betik üretimi, imaja yazma), tek kuyruk işlemi
  `SetPostSetup` (D-029), `PostSetupPage` (tablo, Alt+↑/↓, Bekle etiketi) + adım dialogları. Makine betiği scratch
  klasörde gerçekten çalıştırılıp doğrulandı; zamanlanmış görev + winget + SetupComplete çağrısı VM testi bekliyor.
- **P13 Katılımsız Kurulum (2026-09-30):** `core/unattend` (üret / oku / doğrula), `UnattendController`,
  `UnattendedPage` (adım çubuğu = çapalar, `ui::FormView`, canlı XML önizleme + değişen satır vurgusu),
  "ISO'ya ekle" → `IsoOptions::rootFiles` ile köke bellekten (D-028). ISO üretimi + geri okuma unit testte gerçek
  IMAPI ile doğrulandı. **VM'de kurulum denenmedi:** üretilen XML'in Setup tarafından kabul edildiği kullanıcı
  testi bekliyor (özellikle disk düzeni ve BypassNRO).
- **P12 Ayarlar / Tweaks (2026-09-30):** `resources/catalog/settings.json` (5 sekme, 13 bölüm, 39 ayar; toggle /
  dropdown / radio), `ImageSettingsController` — seçili seçenek **kuyruktan türetilir** (ayrı durum yok), bu yüzden
  P11 tweak'leri, Servisler ve presetlerle kendiliğinden tutarlı. Form kaydırılabilir. Sınır: imajdaki mevcut değer
  okunmuyor (form Windows varsayılanını gösterir). Kayıt değerleri bilgiye dayalı, gerçek kurulumda doğrulanmadı.
- **Yönetici gerektiren, terminalden doğrulanamayanlar:** P04 özellik okuma, P05 uygula, P07 AppX, P08 paket,
  P09 sürücü ekleme, P10 servis okuma/yazma, P11 kayıt defteri + ilk oturum dosyaları — gerçek imajda kullanıcı uygulama içinden test ediyor (terminal yönetici değil).
- **Ön okuma (D-027, 2026-09-30):** mount / geri yükleme biter bitmez `PreloadController` Özellikler → Bileşenler →
  Servisler listelerini sırayla okur; İmajlar şeridinde ve durum çubuğunda ikinci ilerleme. Unit testli, render'da
  doğrulandı (`--operation=read`); gerçek imajda kullanıcı testi bekliyor (yönetici gerekir).
- **İlk oturumda sıfırlanan kayıt değerleri (karar, 2026-09-30):** NTLite ile aynı yol kalır — SetupComplete.cmd +
  ilk oturum RunOnce (D-026 eki); unattend / Active Setup alternatifleri seçilmedi. İçe aktarılan .reg dosyalarının
  **her değeri** artık çevrimdışı yazım + kurulum sonrası yeniden içe aktarım. .reg düzeltmeleri: değersiz `[anahtar]`
  (CreateKey), "sil + varsayılan değeri yaz" kalıbında silmenin kuyrukta ezilmesi, tekrarlanan değerde sıra, HKCC
  ve HKU\S-1-5-18/19/20 kökleri. `reg.exe import` davranışı yerelde doğrulandı (ENGINE saha notu). **Gerçek kurulumda
  (VM) doğrulanmadı:** SetupComplete / RunOnce içe aktarımının kurulum sonunda çalışması kullanıcı testi bekliyor.
- **Build:** `./build.ps1 -Dist` yeşil, 214 unit test. Kullanıcıya her zaman `dist\WinLove.exe` verilir.
- **Kurallar:** kullanıcının diskinde klasör açma (lab = `build\lab`, çalışma kökü `%LOCALAPPDATA%\WinLove`),
  "Son kullanılanlar"a test yolu yazma, DISM'e giden yolları `nativePath` ile ver, asla push etme.
- **Açık konular / sonraya:** P06 USB sekmesi (bilerek yazılmadı: denenemeyen disk biçimlendirme kodu; ISO'yu
  Rufus / Ventoy yazar); güncellemelerde sürükle-sırala; imajdaki mevcut sürücüleri listeleme/kaldırma; "işlem
  sonrası mount'u çöz" (P16 §4); servis katalog notlarının ekranda gösterimi; `C:\WinLove` eski klasörü (kullanıcı
  unmount sonrası silebilir); Faz 4: DComp/dirty-rect (D-011), UIA, imzalama.
- **Lab:** `build\lab\iso\sources\install.wim` (test ISO'sundan kopya, 6,7 GB) duruyor — `lab_components.ps1`,
  `lab_editions.ps1` (kopyası üzerinde) ve `dism_smoke.ps1` bunu kullanır. İmaj incelemesinin geçici dosyaları silindi (yöntem: ENGINE saha notu "CBS / 25H2").

## Son eklenenler (P01)
- Motor: klasör kaynağı, `WindowsRelease` (sürüm adları), `LiveSystem`; `wlcli live`, `wlcli info <klasör>`.
- UI: `DropZone`, `InfoBar`, `Dialog` + Host modal katmanı, çok satırlı metin, Türkçe-duyarlı büyük harf, `DropTarget` (OLE), `FileDialog`.
- Uygulama: `AppState`, `RecentSources`, `Format`, `SourcePage` (+ `RecentList`, `LiveCard`), çok parçalı breadcrumb, `WinLove.exe <yol>`.

## Ortam doğrulaması (2026-09-28)
VS 2026 Community (MSVC 14.50/14.51), Windows SDK 10.0.26100, ADK Deployment Tools, dismapi.dll 10.0.26100, Python 3.14 (fonttools, pillow, playwright), Git. CMake yalnızca VS içinde. C: ~323 GB boş (lab ~7 GB). Ana ekran 144 DPI. PowerShell betik politikası kısıtlı (`-ExecutionPolicy Bypass`). UAC istemiyle yönetici betiği çalıştırılabiliyor.

## Geçmiş
- 2026-10-06 — Önceki kurulum (24H2+) seçeneği; WinRE'siz imajda kendiliğinden açılır (D-074).
- 2026-09-30 — Ön okuma: mount sonrası ikinci ilerleme ile sayfa listeleri önceden okunuyor (D-027).
- 2026-09-30 — İçe aktarılan .reg dosyaları kurulum sonrası da uygulanıyor; .reg ayrıştırma / kuyruk düzeltmeleri.
- 2026-09-30 — P12 Ayarlar / Tweaks geliştirildi (test bekliyor).
- 2026-09-30 — P13 Katılımsız Kurulum geliştirildi (test bekliyor); `ui::FormView` ortak form bileşeni.
- 2026-09-30 — P14 Kurulum Sonrası geliştirildi (test bekliyor).
- 2026-09-30 — P15 Presetler geliştirildi (test bekliyor).
- 2026-09-30 — P16 Uygulama Ayarları geliştirildi (test bekliyor).
- 2026-09-30 — P17 Hakkında geliştirildi (test bekliyor).
- 2026-09-30 — P18 Komut Paleti geliştirildi (test bekliyor). Faz 3 sayfalarının tamamı yazıldı.
- 2026-09-30 — P07 v2: sistem bileşenleri (Edge, WebView2, OneDrive, WinRE) ve bileşen deposu temizliği (D-031).
- 2026-09-30 — Kullanıcının ilk gerçek Uygula'sı (143 / 148); ondan çıkan düzeltmeler (D-032, kilitli uygulamalar, optimizeWim).
- 2026-09-30 — Sürüm silme: WIM kalanlarla yeniden yazılıyor, ISO kaynakta da çalışıyor, "yalnız bu sürümü tut" (D-033).
- 2026-09-30 — Yanıt dosyası doldurulunca ISO'ya kendiliğinden giriyor; işlemci / disk denetimi atlama; imajda olmayan AppX başarı (D-034).
- 2026-09-30 — İmajlar: çoklu seçim, yeniden adlandırma, WIM doğrulama (kendi LZX çözücümüz), sürüm yükseltme (D-035).
- 2026-09-30 — Yanıt dosyası: `UserData` her zaman `ProductKey` ile (genel anahtar / yer tutucu) (D-036).
- 2026-09-30 — Hazır komutlar (güç planı, ağ), ağ ayarları, klasik Fotoğraf Görüntüleyicisi (D-044).
- 2026-09-30 — Sağ tık menüsü, bildirimler, oyun ayarları (D-043).
- 2026-09-30 — Vurgu renkleri, ayarlar sol menüde; +38 ayar ve Sistem sekmesi (D-042).
- 2026-09-30 — Görev çubuğu, Copilot / Recall / Edge / BitLocker / Windows Update ayarları, masaüstü, duvar kağıdı, kilit ekranı, OEM; hazır uygulamalar; Uygula raporu (D-041).
- 2026-09-30 — Başlat menüsü temizliği: boş sabitlenenler / kutucuklar (`WriteFile`), reklam uygulamaları, widget (D-040).
- 2026-09-30 — OneDrive (Windows 10 yolları) ve yeni Outlook'un OOBE kurulumunu engelleyen bileşenler (D-039).
- 2026-09-30 — boot.wim yaması gerçek imajda kanıtlandı; ISO sayfasına "atlamaları boot.wim'e de yaz" (D-038).
- 2026-09-30 — Yerel uygulama kaldırma gerçek imajda kanıtlandı, Applier'a bağlandı, katalog kilidi kalktı (D-038).
- 2026-09-30 — Yanıt dosyası kalıcı (D-037); yerel uygulama kaldırma ve boot.wim yaması: motor, kanıt bekliyor (D-038).
- 2026-09-30 — İmajlar kısayolları: bağlama klasörü, komut istemi, dosya konumu, bilgileri kopyala, yenile (denenmedi).
- 2026-09-28 — Baştan sona inceleme (4 alan, paralel): ~35 hata düzeltildi. Öne çıkanlar: junction üzerinden ana
  makine ACL'si değişebilmesi (FileLocks), commit edilmeyen unmount'un başarılı raporlanması, DISM oturum yenileme,
  UDF çıkarmada yol dışına yazma + sınır dışı okuma, tıklamada yok edilen widget (use-after-free), Enter ile devre
  dışı onay düğmesi, iş sürerken pencere kapatma, uygulama sırasında kuyruk düzenleme, bozuk preset/ayar dosyasında
  çökme, argüman tırnaklama / yeniden başlatma döngüsü, REGEDIT4 ANSI, INF x86, servis adlarında imaj dili.
- 2026-09-28 — P02–P04 onaylandı; P05, P06, P07 (AppX), P08, P09 geliştirildi (test bekliyor).
- 2026-09-28 — P01 Kaynak geliştirildi (kullanıcı testi bekliyor).
- 2026-09-28 — Faz 2 tamamlandı: log/görevler/yetki, UDF+WIM okuyucular, DISM backend (gerçek imajda test), ChangeSet/Planner/Applier.
- 2026-09-28 — Faz 1 tamamlandı: widget sistemi, temel widget'lar, uygulama kabuğu, galeri, otomatik daralma.
- 2026-09-28 — Faz 0 tamamlandı; UI handoff alındı; proje planı yazıldı.
