# Durum — "Neredeyiz?"

> Her oturumun sonunda güncellenir. En üstte güncel durum; geçmiş en altta kısa satırlar.

## Güncel
- **Faz:** 3 — sayfalar. P01–P04 ✅. P05 Uygula, P06 ISO, P08 Güncellemeler, P09 Sürücüler, P10 Servisler, P11 Kayıt Defteri,
  P12 Ayarlar / Tweaks, P13 Katılımsız Kurulum, P14 Kurulum Sonrası, P15 Presetler, P16 Uygulama Ayarları, P17 Hakkında, P18 Komut Paleti:
  🟨 geliştirme bitti, kullanıcı testi bekliyor. P07 Bileşenler: 🟨 v2 (AppX + sistem bileşenleri + depo
  temizliği, D-031), test bekliyor.
- **Çalışma şekli:** kullanıcı "her seferinde durma" dedi — sayfa bitince build + test + `-Dist` + yerel commit,
  sonra doğrudan bir sonraki sayfa. Kullanıcı `dist\WinLove.exe`'yi paralel test ediyor.
- **Faz 3'ün bütün sayfaları yazıldı** (P01–P04 onaylı, P05–P18 kullanıcı testi bekliyor). Kural 1 gereği Faz 4'e
  geçmeden önce bu sayfaların kullanıcı onayı gerekir.
- **Bir sonraki somut adım:** kullanıcı VM'de kendi imajını deniyor (`docs/TESTING.md` → "VM kabul testi");
  testten gelen düzeltmeler sırayla. Log: `%LOCALAPPDATA%\WinLove\logs\WinLove-*.log` (oturum başına bir dosya).
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
- **Build:** `./build.ps1 -Dist` yeşil, 172 unit test. Kullanıcıya her zaman `dist\WinLove.exe` verilir.
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
