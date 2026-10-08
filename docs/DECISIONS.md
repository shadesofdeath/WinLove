# Karar Kaydı (ADR)

> Format: `## D-NNN — Başlık (tarih)` → Bağlam · Karar · Sonuç. Kararlar silinmez; değişirse yeni kayıt "D-xxx'i geçersiz kılar" der.

## D-001 — Custom-draw UI: D2D + DirectWrite + DirectComposition (2026-09-28)
Bağlam: Özgün, kompakt, Fluent olmayan bir arayüz isteniyor. Hazır toolkit'ler kendi görünümünü dayatıyor.
Karar: Tüm UI kendi framework'ümüz `wl::ui` ile çizilir. Win32 kontrolleri yalnızca sistem diyalogları (dosya aç/kaydet) için.
Sonuç: Widget'lar, erişilebilirlik (UIA) ve metin düzenleme bizim sorumluluğumuzda; zaman maliyeti Faz 1'de ödenir.

## D-002 — Motor soyutlaması `wl::image` (2026-09-28)
Bağlam: DISM C API'si eksik (AppX yok, kalıcı paketler kaldırılamaz), dism.exe çıktı ayrıştırma kırılgan.
Karar: Uygulama yalnızca `wl::image` arayüzünü kullanır; arkada dismapi/wimgapi/dism.exe/kendi native kodumuz. (`ENGINE.md`)

## D-003 — ChangeSet modeli (2026-09-28)
Karar: Hiçbir değişiklik anında imaja yazılmaz; hepsi `Operation` olarak ChangeSet'e girer ve Uygula ile işlenir. Presetler ChangeSet dosyasıdır.

## D-004 — Sayfa sırası: uçtan uca hat önce (2026-09-28)
Karar: Kaynak → İmajlar → Loglar → Özellikler → Uygula → ISO, ardından Bileşenler. Gerekçe: zor sayfalardan önce seç-değiştir-uygula-ISO-kur hattının gerçek imajda çalışması.

## D-005 — Uygulama `asInvoker` başlar (2026-09-28)
Karar: Admin yalnızca imaj işlemleri için gerekir; gerektiğinde "yönetici olarak yeniden başlat". UI geliştirme ve render testleri admin'siz çalışır.

## D-006 — Bağımlılıklar vendored (2026-09-28)
Karar: vcpkg yok; doctest, nlohmann/json, pugixml `third_party/` altında. Gerekçe: her AI oturumunda sıfır kurulumla build.

## D-007 — UI metinlerinin yaşayan kaynağı `resources/strings/` (2026-09-28)
Bağlam: Handoff'taki `07_copy/strings.*.json` başlangıç seti; sayfalar geliştikçe yeni anahtar gerekecek, handoff ise salt okunur.
Karar: Metinler `resources/strings/{tr,en}.json`'a kopyalandı ve bundan sonra orada yaşar. `tools/gen_strings.py` iki dilin anahtar ve `{yer tutucu}` eşitliğini denetler, `Str::` enum'u üretir. Tokenlar ve ikonlar ise doğrudan handoff'tan üretilir.

## D-008 — `base` katmanı (2026-09-28)
Bağlam: `ui` de hata tipine (`Result/Error`) ve UTF-8 yardımcılarına ihtiyaç duyuyor, ama `core`'a bağımlı olamaz.
Karar: `src/base` (`wl_base`, namespace `wl`) eklendi; `core` ve `ui` yalnızca buna bağımlı. İçine sadece gerçekten ortak olan girer (Result, UTF-8; ileride Log).

## D-009 — C++23 ve statik CRT (2026-09-28)
Karar: `std::expected` için C++23 (`/std:c++latest`). CRT statik (`/MT`): exe'ler USB/WinPE üzerinde yeniden dağıtılabilir paket olmadan çalışsın.

## D-010 — Üretilen kod depoya girer (2026-09-28)
Karar: `src/*/generated/*.g.h` commit edilir; build Python olmadan da çalışır. `build.ps1` her build'de üreticileri çalıştırır (değişiklik yoksa dosyaya dokunmaz), böylece JSON değişip üretim unutulamaz.

## D-011 — Önce HWND flip swapchain, DirectComposition sonra (2026-09-28)
Bağlam: Tasarım rehberi DComp görsel ağacı öneriyor (panel/dialog animasyonları için). İlk pencere için gereksiz karmaşıklık.
Karar: Şimdilik tek HWND flip-sequential swapchain, her karede tam çizim (yalnızca `invalidate` ile; boşta 0 CPU ölçüldü). DComp ve dirty-rect, animasyon ve widget invalidation'ı gerektiğinde (Faz 1.4) gelir.

## D-012 — Geçici TitleBar (2026-09-28)
Karar: `app/shell/TitleBar` widget ağacı olmadan kendini çizip hit-test ediyor ki pencere ve Snap Layouts erken doğrulanabilsin. Faz 1.4'te IconButton/Kbd/SearchTrigger widget'larına bölünecek; davranış (hit-test bölgeleri, hover durumları) korunacak.

## D-013 — Görsel doğrulama yalnızca kendi penceremiz (2026-09-28)
Bağlam: Ekran görüntüsüyle doğrulama denemesi kullanıcının önündeki başka bir pencereyi yakaladı (görüntüler hemen silindi).
Karar: Tüm ekranı yakalamak yasak. Yalnızca `--render` (offscreen) ve `tools/capture_window.py` (`PrintWindow`, yalnızca WinLove HWND'si).

## D-014 — Aktif menü öğesinde çizgi ikon (2026-09-28)
Bağlam: icons.md seçili nav öğesi için "filled" varyant diyor; ama handoff ekranları (04_screens) aktif öğeyi çizgi ikonla çiziyor ve bazı dolu varyantlar 16px'te içi dolu bloğa dönüşüyor (ör. `source`).
Karar: Ekranlar esas: aktif öğe çizgi ikon + text.primary + accent çubuk. Filled varyantlar üretilmeye devam eder (sekmeler vb. için).

## D-015 — Widget sistemi: mutlak koordinat, kendi çizen bileşik widget'lar (2026-09-28)
Karar: Her widget'ın `bounds`'u pencere DIP'inde mutlaktır (dönüşüm yok; hit-test ve çizim basit). Bileşik widget'lar ikon/etiket gibi parçaları çocuk widget yapmadan kendileri çizer; yalnızca etkileşimli parçalar (buton, tetikleyici) çocuk widget'tır. Kaydırma geldiğinde (ScrollView) içerik ofseti ScrollView'da uygulanacak.

## D-016 — Kaynak dosyaları .rc bağımlılığı (2026-09-28)
Bağlam: `strings/*.json` değiştiği hâlde exe'deki gömülü metin eski kaldı (ninja, RCDATA dosyalarını izlemiyor) → çalışma zamanı hatası.
Karar: `src/app/CMakeLists.txt` gömülen tüm dosyaları `OBJECT_DEPENDS` ile `WinLove.rc`'ye bağlar. Yeni gömülü dosya eklerken listeye de eklenmeli.

## D-017 — DISM API'si ADK'sız: kendi bildirimlerimiz + dinamik yükleme (2026-09-28)
Bağlam: `dismapi.h/.lib` yalnızca ADK ile gelir; build'i ADK'ya bağlamak her makinede kurulum ister.
Karar: Kullandığımız alt küme `core/image/dism/DismApi.h`'de (ADK 10.1.26100 başlığından, pack(1) düzeniyle) tanımlı; `dismapi.dll` System32'den `LoadLibraryEx` ile yüklenir, eksik giriş noktası `Unsupported` hatası verir.

## D-018 — Kaynak okuma yerinde, DISM yalnızca servis için (2026-09-28)
Karar: ISO/WIM/ESD'deki sürümleri listelemek kendi UDF + WIM okuyucumuzla yapılır (admin yok, bağlama yok, milisaniyeler). DISM (admin) yalnızca bağlama ve servis işlemleri (paket/özellik/sürücü) için. P01/P02 böylece yönetici olmadan açılır; admin isteği yalnızca "Bağla" anında gelir.

## D-019 — Operation bir değer tipi, sanal sınıf değil (2026-09-28)
Bağlam: Mimari taslak `Operation`'ı validate/apply metotlu soyut sınıf olarak çizmişti.
Karar: `Operation` = {kind, target, value, risk, sizeDelta}. Uygulama `Applier` içinde `kind` üzerinden switch; doğrulama sayfaların/kataloğun işi. Gerekçe: preset JSON'u, undo anlık görüntüleri ve karşılaştırma (P15 diff) değer tipinde bedava; backend'ler tek yerde.

## D-020 — Komut satırından kaynak açma (2026-09-28)
Karar: `WinLove.exe <yol>` (ISO/WIM/ESD/SWM/klasör) açılışta kaynağı açar. Dosya ilişkilendirme, "Birlikte aç" ve exe'ye sürükleme bu yolla çalışır; otomatik pencere testleri (capture_window) de açma akışını bununla sürer.

## D-021 — Canlı (çalışan) sistem düzenleme kapsam dışı (2026-09-28)
Bağlam: Tasarım Kaynak sayfasında "Bu bilgisayar (canlı)" kartı ve canlı sistem düzenleme öneriyordu; kullanıcı bu özelliği istemedi.
Karar: WinLove yalnızca çevrimdışı imajlarla çalışır (ISO/WIM/ESD/SWM/klasör). Kart, `LiveSystem`, `wlcli live` ve ilgili metinler kaldırıldı; DropZone tam genişlik. s4 yönetici dialogu yalnızca imaj bağlama/değiştirme için kalır.

## D-022 — Uygulama açılışta yönetici olarak başlar (2026-09-28)
Bağlam: Kullanıcı, "Bağla"ya basınca uygulama içi yönetici dialogu (s4) çıkmasını istemedi; mount/unmount/servis işlemleri zaten admin ister.
Karar: `WinLove.exe` yönetici değilse açılışta aynı argümanlarla `runas` ile kendini yeniden başlatır (tek UAC istemi). UAC reddedilirse yönetici olmadan devam eder; işlemler o zaman s4'ü gösterir. `--render` ve `--no-elevate` (capture_window.py kullanır) yükseltmez.
Sonuç: UIPI, yönetici penceresine Explorer'dan OLE sürükle-bırakı engeller. Yönetici iken `DropTarget` klasik `WM_DROPFILES`'a geçer (`ChangeWindowMessageFilterEx` ile izinli): bırakma çalışır, sürükleme sırasındaki vurgu yoktur.
Ayrıca: ilerleme metinlerinde "DISM /Mount-Image" gibi komut adları gösterilmez; motor dism.exe değil dismapi.dll'dir.

## D-023 — DISM bildirimlerimiz ADK başlığına karşı derleme anında doğrulanır (2026-09-28)
Bağlam: D-017 gereği dismapi.h'yi kendimiz bildiriyoruz; yanlış bir alan sırası sessizce bellek bozar.
Karar: ADK kuruluysa `tests/core/DismAbiTests.cpp` derlenir: her yapı için `sizeof`/`offsetof`, enum değerleri ve
bayraklar resmi `dismapi.h` ile `static_assert` ile karşılaştırılır; fark build'i kırar. ADK yoksa test atlanır
(build ADK'ya bağımlı olmaz). Yeni bir DISM yapısı eklendiğinde bu dosyaya da eklenir.

## D-024 — Kaynak açma DISM kuyruğunda beklemez (2026-09-28)
Bağlam: Tek motor iş parçacığı, açılıştaki mount incelemesi / özellik okuması bitene kadar "Açılıyor…" bekletiyordu.
Karar: `AppState::reader()` — dosya okumaları (kaynak açma) için ayrı iş parçacığı; DISM/WIMGAPI işleri `engine()`'de.
Yeni açma isteği eskisini geçersiz kılar (son istek kazanır). Sağlıklı mount incelemesi kilit taraması yapmaz.

## D-025 — ISO, Windows'un IMAPI2FS bileşeniyle üretilir (2026-09-28)
Bağlam: oscdimg yalnızca ADK'da; kullanıcıda ADK yok. Kendi UDF + El Torito yazıcımız büyük bir iş.
Karar: `core::buildIso` IMAPI2FS (imapi2fs.dll, Windows'la gelir) kullanır: UDF 1.02, BIOS + UEFI önyükleme
kataloğu, yönetici gerekmez. Katalog düzeni Microsoft ISO'larıyla karşılaştırılarak doğrulandı. İleride ISO 9660
köprüsü gerekirse (eski BIOS'lar) kendi yazıcımız düşünülür.

## D-026 — OOBE / ilk oturumda sıfırlanan kayıt değerleri kurulumdan sonra yeniden uygulanır (2026-09-28)
Bağlam: Kullanıcı notu — bazı tweak'leri Windows ilk oturum açılışında yok sayıyor (NTLite bunu SetupComplete ile
çözüyor). OOBE gizlilik sayfası konum / reklam kimliği / kişiye özel deneyim değerlerini yeniden yazar; ilk oturum
varsayılan temayı ve görev çubuğu düzenini uygular; ContentDeliveryManager kendini yeniden tohumlar.
Karar: Katalogda `"apply": "firstLogon"` işaretli tweak'ler `SetRegistryFirstLogon` işlemi olur: değer çevrimdışı
hive'a yine yazılır **ve** imaj içindeki `Windows\Setup\Scripts\WinLove\` altında bir .reg dosyasına kaydedilir:
HKLM/HKCR → `setupcomplete.reg` (`SetupComplete.cmd` ile SYSTEM olarak, OOBE'den sonra, ilk oturumdan önce içe
aktarılır; mevcut SetupComplete.cmd korunur, satır bir kez eklenir), HKCU → `firstlogon-user.reg` (Default profilin
RunOnce değeri ile her yeni kullanıcının ilk oturumunda `reg.exe import`). Ekranda "İlk oturumda" etiketi.
Sınır: OEM ürün anahtarıyla etkinleştirilen sürümlerde Windows SetupComplete.cmd'yi çalıştırmaz; RunOnce reg.exe
kısa bir konsol penceresi gösterebilir. P14 Kurulum Sonrası aynı klasörü/mekanizmayı genişletecek.
Ek (2026-09-30, kullanıcı kararı): yol NTLite ile aynı kalır — SetupComplete.cmd + ilk oturum; unattend /
Active Setup gibi alternatifler değerlendirildi, **seçilmedi** ("garantiye oynayalım"). Kapsam genişledi:
**içe aktarılan .reg dosyalarının her değeri** `SetRegistryFirstLogon` olur (hangi değerin sıfırlanacağı
bilinemez; yeniden içe aktarmak zararsız). İmajda hive'ı olmayan ama kurulu sistemde yazılabilen kökler (HKCC,
HKU\S-1-5-19/-20) yalnız kurulum sonrası dosyasına yazılır; SAM / SECURITY / başka kullanıcı SID'leri atlanır.
Ertelenmiş dosyalar Uygula boyunca eklenerek yazılır, sonda sıkıştırılır (`DeferredRegistry`).

## D-027 — Sayfa listeleri mount biter bitmez okunur (2026-09-30)
Bağlam: Özellikler, Bileşenler ve Servisler listeleri sayfaya ilk girişte okunuyordu; kullanıcı her sayfada ayrı
ayrı bekliyordu (özellik ayrıntıları özellik başına bir DISM çağrısı).
Karar: `PreloadController` — bağlama ve geri yükleme sonrasında tek motor işi üç listeyi sırayla okur
(`EngineOperation::Kind::Reading`, aşama ağırlıkları 0,6 / 0,3 / 0,1); İmajlar şeridi ve durum çubuğu mount'tan
sonra ikinci bir ilerleme gösterir. Her liste okunduğu an `AppState`'e yazılır; başarısız okuma yalnızca kendi
listesini "Failed" yapar (sayfada "Yeniden dene"). İptal edilirse kalan listeler boş bırakılır ve sayfaların kendi
`load()` yolu (eski tembel okuma) devreye girer — bu yol Uygula sonrası listeler geçersizleşince de kullanılır.
Okuma salt okunur olduğu için pencere kapatmayı engellemez (iptal edilir); mount/unmount/Uygula bitene kadar bekler.
Yeni bir sayfa imajdan liste okuyacaksa buraya bir aşama olarak eklenir.

## D-028 — Yanıt dosyası ISO'ya bellekten eklenir; form adımları çapadır (2026-09-30)
Bağlam: `autounattend.xml`'in windowsPE geçişi (dil, disk, LabConfig, anahtar) için dosya medya kökünde olmalı.
Kaynak, kullanıcının kendi kurulum klasörü olabilir; oraya dosya yazmak klasörünü değiştirir ve "ISO'ya ekle"
kapatıldığında dosya geride kalır.
Karar: `IsoOptions::rootFiles` — IMAPI imaj köküne dosya bellekten eklenir (`IFsiDirectoryItem::AddFile`, aynı adlı
dosya önce imajdan çıkarılır); klasöre hiçbir şey yazılmaz. Yanıt dosyası seçenekleri `AppState::unattend()` içinde
durur; ISO üretimi o anki seçeneklerden dosyayı kurar. Geçersiz dosya (Setup'ın reddedeceği ad / anahtar) ISO'yu
engeller. İmaj içine (`Windows\Panther\unattend.xml`) yazmak seçilmedi: windowsPE geçişini kapsamaz.
P13 formunda adımlar sihirbaz sayfası değil çapadır (tek form, önizleme hep tüm dosya).

## D-029 — Kurulum sonrası planı tek kuyruk işlemidir; ilk oturum adımları zamanlanmış görevle çalışır (2026-09-30)
Bağlam: Adımların sırası ve ortak seçenekleri var; ChangeSet ise (tür, hedef) başına tek işlem tutan sırasız bir
yapı. winget SYSTEM hesabında yok; ilk oturumda çalışan betiğin de yükseltilmiş olması gerekir (kurulumlar).
Karar: Plan bütün olarak tek `SetPostSetup` işleminin değeridir (JSON). Betikler D-026 klasörüne yazılır;
`SetupComplete.cmd` makine betiğini çağırır, makine betiği ilk oturum için bir zamanlanmış görev kaydeder
(`BUILTIN\Users` SID'i + `HighestAvailable` → oturum açan yöneticide UAC sorusu olmadan yükseltilmiş). HKLM `RunOnce`
seçilmedi (yükseltilmemiş çalışır), `FirstLogonCommands` seçilmedi (kullanıcı kararı: SetupComplete yolu; ayrıca
yanıt dosyası isteğe bağlı). Kopyalanacak dosyalar Uygula sırasında imaja taşınır, hedefe kurulu sistemde kopyalanır
(hedef `%USERPROFILE%` gibi kurulumdan önce var olmayan bir yer olabilir).

## D-030 — Preset = ChangeSet dosyası + ad + yanıt dosyası; durum kuyruktan okunur (2026-09-30)
Bağlam: P12 ayarları ve P14 planı zaten kuyruk işlemleri; P13 yanıt dosyası ise imaja değil ISO'ya ait olduğu için
kuyruk dışında (`AppState::unattend()`). Tasarım 17 presetlerde "Katılımsız" kategorisini de gösteriyor.
Karar: Preset dosyası ChangeSet JSON'unun üstüne iki anahtar ekler (`name`, `unattend`); biçim geriye dönük uyumlu
kalır. Yanıt dosyası preset içinde kendi XML'i olarak taşınır (tek okuyucu / yazıcı). Kitaplık uygulama verisindeki
`%LOCALAPPDATA%\WinLove\presets` klasörüdür (çalışma klasörü başka diske taşınsa da orada kalır; kullanıcının
belgelerine ya da C:\ köküne klasör açılmaz). Karşılaştırma ham işlemleri değil
adlandırılmış öğeleri gösterir; ad kaynağı P12 ayar kataloğudur.

## D-031 — Sistem bileşenleri tarifle kaldırılır; Defender kaldırma sunulmaz; depo temizliği dism.exe ile (2026-09-30)
Bağlam: P07 v1 yalnız AppX kaldırıyordu. Test imajı (Windows 11 25H2, 26200.8037, Pro) yönetici gerektirmeden
incelendi (hive'lar ve dosya listesi 7-Zip ile `build\lab` altına çıkarıldı): 3446 CBS paketinden 3249'u gizli ve
hepsinin `Owners` anahtarı var; **Defender ayrı bir paket değil** (`Microsoft-Windows-Client-Desktop-Required-PackageNN`
içinde); Media Player / IE / ISE / WMIC / VBScript / Hello Face gibi parçalar zaten görünür FoD (P04 Özellikler
kaldırıyor). Paket olmayan ama yer tutan şeyler: Edge (803 MB), WebView2 (796 MB), EdgeCore + EdgeUpdate (806 MB),
OneDriveSetup (86 MB, gizli `Microsoft-Windows-OneDrive-Setup-Package`), `Winre.wim` (643 MB). WinSxS 10,5 GB;
1713 paket "staged" (eski sürüm).
Karar:
- Sistem bileşeni = **tarif** (`core::ComponentRecipe`: CBS paket aileleri + imaj köküne göre yollar + çevrimdışı
  kayıt yazımları). Katalog `resources/catalog/components.json`; kuyrukta `RemoveComponent` işleminin değeri tarifin
  kendisi (preset ne yaptığını taşır, Applier katalog bilmez).
- Gizli paket: SOFTWARE hive'ında `Visibility = 1` + `Owners` silinir (install_wim_tweak'in bilinen yöntemi), sonra
  `DismRemovePackage`. Tarifin yolları da varsa paket **en iyi çaba**dır: DISM reddederse dosyalar yine silinir, log'a
  uyarı düşer (bileşen işlevsel olarak gitmiştir, WinSxS kopyası kalır). Yalnız paketten oluşan tarif reddedilirse
  adım hata verir.
- Hive düzenlenirken ve `dism.exe` çalışırken kendi DISM oturumumuz kapatılır (`DismSession::suspend` → `reload`).
- Tarif kullanıcı girdisidir (preset): yollar göreli, en az iki seviye, `..` / sürücü / akış yok, Windows'un
  vazgeçilmez klasörleri yasak, yol üzerindeki bağlantı (junction) reddedilir (imajın `Documents and Settings`'i ana
  makinenin `C:\Users`'ına gider).
- **Defender kaldırma sunulmaz**: 24H2+ imajlarda DISM ile sökülecek paket yok; bileşen (manifest) düzeyinde söküm
  NTLite'ın yıllarca uyumluluk verisiyle yaptığı ayrı bir motor. Kapatmak için Servisler / Ayarlar sayfaları var.
- Bileşen deposu temizliği (`CleanupImage`): DISM API'de karşılığı yok → `dism.exe /Image /Cleanup-Image
  /StartComponentCleanup /ResetBase` alt süreç olarak, çıktısından ilerleme. Başladıktan sonra iptal edilmez.
  **Sıra (aynı gün düzeltildi):** önce "planın ilk adımı" idi (bekleyen işlem varken DISM 0x800F0806 ile reddeder).
  Gerçek imajda deneme, Microsoft'un dokunulmamış medyasında temizlenecek bir şey olmadığını gösterdi (13 sn, kazanç
  yok): temizliğin işe yaradığı an, **bu çalıştırmada eklenen güncellemelerden sonrası**. Artık kendi aşaması var
  (`Phase::Cleanup`): Kaldır → Özellikler → Sürücüler → Güncellemeler → **Temizlik** → Ayarlar (Microsoft'un medya
  yenileme sırası). Aynı çalıştırmada bekleyen işlem bırakan bir adım varsa (ör. .NET 3.5 açmak) DISM reddeder: adım
  açıklamalı bir hatayla başarısız görünür, imaj etkilenmez.
Reddedilenler: 865 gizli paket ailesini ham liste olarak sunmak (çoğu çekirdek; seçimin sonucu test edilemez); USB
yazma (bu oturumda denenemeyen, yanlış diski silebilecek kod).
Doğrulama durumu: mantık unit testli (gerçek paket adlarıyla); gerçek imajda `tools\lab_components.ps1` (yönetici)
ve VM kurulumu kullanıcıda.

## D-032 — Düğme "açık" = o şey yapılır; ayar satırları durumunu yazıyla söyler (2026-09-30)
Bağlam: Kullanıcının ilk gerçek denemesi. Tasarım 11'de "Microsoft hesabı zorunluluğu", "TPM 2.0 denetimi" gibi
satırlar Windows'un davranışını gösteriyordu: düğme **kapatılınca** XML'e kod ekleniyordu ("basıyorum, düğme açılmadan
kodu ekliyor"). Tasarım 10'da (Ayarlar / Tweaks) düğmenin yanında yalnız ad vardı: "Reklam kimliği" düğmesi açıkken
özellik mi açık, tweak mi uygulanmış — anlaşılmıyordu.
Karar:
- Katılımsız Kurulum'daki bütün düğmeler "açık = WinLove bunu yanıt dosyasına yazar" okunur: "Microsoft hesabı
  zorunluluğunu kaldır", "TPM 2.0 / Secure Boot / RAM denetimini atla". Açıkken yanında ne yazıldığı görünür
  ("BypassNRO uygulanır", "LabConfig uygulanır"). Model (`UnattendOptions::bypass*`) değişmedi.
- Ayarlar / Tweaks'te düğme özelliğin kurulan Windows'taki durumunu göstermeye devam eder (katalog böyle kurulu),
  ama her satır bunu yazar: "Açık · Windows varsayılanı" / "Kapalı · değiştirilecek" (değişenler vurgu renginde);
  açılır liste ve radyo satırlarında yalnız işaret. Sayfa açıklaması da aynı şeyi söyler.
Tasarımdan sapma: metinler `resources/strings` içinde değişti (D-007: yaşayan kaynak orası).

## D-033 — Sürüm silme WIM'i kalanlarla yeniden yazar; ISO kaynakta da çalışır (2026-09-30)
Bağlam: "Index'i sil…" P02'de vardı ama (1) ISO açıkken kapalıydı — kullanıcı özelliği yok sandı, (2) `WIMDeleteImage`
dosyayı küçültmüyordu (akışlar kalıyor), (3) tek tek siliniyordu; en sık istenen "yalnız Pro kalsın" beş onay demekti.
Karar:
- Silme = kalan sürümleri yeni dosyaya export + yer değiştirme (`core::removeImages`, `optimizeWim` ile aynı yol).
  Özgün dosya yenisi tamamlanana dek dokunulmaz; iptal edilebilir. Önyüklemeli WIM'de (boot.wim) eski yerinde silme
  kalır (export önyükleme index'ini düşürür); ESD / bölünmüş imaj reddedilir ("önce WIM'e dönüştür").
- ISO kaynak: bağlamadaki gibi önce çalışma klasörüne kopyalanır, kaynak o klasöre geçer; ISO dosyası değişmez.
- Arayüz: adı "Sürümü sil…"; satıra sağ tık menüsü (Bağla / Dışa aktar / Sürümü sil… / Yalnız bu sürümü tut…), `Del`.
  Çoklu seçim yine yok (tablo tek seçimli kalır): "yalnız bunu tut" asıl ihtiyacı karşılıyor.
- Kalan sürümler yeniden numaralanır: seçim ve yanıt dosyasındaki sürüm index'i kaydırılır; yanıt dosyasının sürümü
  silindiyse 0'a (Setup sorar) döner.
Kapalı düğme nedenini tooltip'te söyler (bağlı imaj, ESD / bölünmüş, tek sürüm).

## D-034 — Yanıt dosyası doldurulunca ISO'ya kendiliğinden girer (2026-09-30)
Bağlam: Kullanıcı Katılımsız Kurulum'u doldurdu, ISO'yu üretti; VM'de Setup "gereksinimler karşılanmıyor" dedi —
ISO'da `autounattend.xml` yoktu. "ISO'ya ekle" kutusu önizleme başlığının sağ ucunda, varsayılan kapalıydı; ISO
sayfası dosyanın eklenmeyeceğini söylemiyordu, log da yazmıyordu.
Karar:
- İlk yanıt (ve XML içe aktarma) "ISO'ya ekle"yi açar. Kutu vazgeçmek için kalır; elle kapatılan kutuyu sonraki
  düzenlemeler açmaz. Preset kendi bayrağını taşır.
- ISO sayfası özetinde "Yanıt dosyası" satırı hep görünür; yanıt var ama kutu kapalıysa turuncu uyarı.
- ISO logu dosyanın eklenip eklenmediğini yazar (`answer file: …`).
- Gereksinimler'e işlemci ve disk boyutu denetimleri eklendi (LabConfig `BypassCPUCheck`, `BypassStorageCheck`):
  VM'lerde TPM kadar sık takılan iki denetim.

## D-037 — Yanıt dosyası oturumlar arasında saklanır, DPAPI ile (2026-09-30)
Bağlam: Yanıtlar yalnız bellekteydi; kullanıcı formu iki kez baştan doldurmak zorunda kaldı.
Karar: `AppState` her `setUnattend`'de `answers.dat`'a yazar, açılışta okur (`app/state/AnswerStore`). İçerik preset'teki
ile aynı belge (`includeInIso` + XML); parola XML'de yalnız Setup'ın kodlamasıyla durduğu için dosyanın tamamı
`CryptProtectData` ile (kullanıcı kapsamı) korunur — koruma başarısızsa dosya hiç yazılmaz. Yol `AppState`'e kurucu
parametresiyle verilir; testler ve render'lar vermez (kullanıcının yanıtları okunmaz, yazılmaz).
Presetler parolayı hâlâ korumasız (Base64) taşıyor: taşınabilir dosya olduğu için DPAPI orada işe yaramaz — açık konu.

## D-086 — İlk oturumun "Windows hazırlanıyor" ekranı yerine kendi ekranımız: denendi, bırakıldı (2026-10-08)
Bağlam: Kullanıcı, D-085 akışında hesabın ilk oturumundaki siyah "Windows hazırlanıyor" ekranının yerine kurulum
sihirbazının devamı gibi kendi ekranımızı istedi.
Denenen: OOBE sonrası kanca `HKLM\...\Policies\System\DelayedDesktopSwitchTimeout = 0` yazar (Windows o ekranı göstermez,
masaüstüne hemen geçer); hesabın ilk oturumunda sihirbazın "Her şey hazırlanıyor" görünümünde tam ekran bir pencere
(seçilen tema / vurgu, "Hoş geldin, {ad}", görev çubuğu → Başlat → seçilen görünüm adımları) masaüstü hazır olana dek
kalır, sonra solar; ilke ve parçalar ilk oturumdan sonra kaldırılır. Başlatma iki yolla denendi: oturum açılışında
zamanlanmış görev (VM `w10f`, `spec7`) ve Active Setup (`w10g`, `spec8`).
Sonuç (VM, Win10 19045 + Win11 26200, dördü de ALL PASSED ama amaç tutmadı): Windows'un ekranı gerçekten kalktı, fakat
PowerShell + WPF ilk oturumda (Windows o sırada uygulamaları kaydederken) 10–20 sn'de açılıyor; masaüstü bizden önce
hazır oluyor. Kullanıcı önce çıplak masaüstünü, sonra birkaç saniye bizim ekranı görüyor — Win10'da eskisinden kötü.
Karar: bırakıldı, Windows'un kendi ekranı kalır. Kalanlar: görevler pilde de çalışır (aşağıda), laboratuvarın
`lab_vm.ps1 -ShotSeconds`'ı. Yeniden denenecekse PowerShell yerine hızlı açılan yerel bir yardımcı gerekir.
Ek düzeltme (bu denemede bulundu): `Register-ScheduledTask` ayarsız kaydedilince Windows görevi **pildeyken başlatmaz**
(`DisallowStartIfOnBatteries` varsayılan açık). Karşılamanın ilk oturum görevi otomatik oturumun parolasını
(`DefaultPassword`) siliyor: pildeki bir dizüstünde parola kayıt defterinde açık metin kalırdı. Bütün görevler artık
`-AllowStartIfOnBatteries -DontStopIfGoingOnBatteries` ile kaydedilir.

## D-085 — Kurulum ekranımız Windows Kurulumu'nun içinde: logodan sonra bizim sayfalar, OOBE ve geçici hesap yok (2026-10-08)
Bağlam: Kullanıcı D-084'ün görünümünü beğenmedi ("Windows'un temasına yakın olmalıydı… Claude Design ile Windows'a yakışır
bir kurulum arayüzü"), daha fazla ayar ve kablosuz ağ istedi, sonra akışı netleştirdi: "format bitince Windows logosunda
dönüp bitince bizim setup arayüzümüz gelsin, seçenekler seçilince yeniden başlatılıp masaüstü gelsin; Windows'un OOBE'sine
dair bir kısım olmasın" — D-084'teki geçici hesap iki kez "Merhaba / Hazırlanıyor" bekletiyordu.
Karar:
- **Tasarım:** Claude Design tuvali ("WinLove Kurulum Ekranı", 7 ekran + koyu tema) → WPF. Windows 11 kurulumunun dili:
  bulanık renk bulutları üstünde yuvarlak kart, solda seçime göre **canlı çizim** (yazılan ad kartta, saat diliminin
  saati monitörde, tema / vurgu / görev çubuğu küçük masaüstünde, tercihler küçük Gezgin'de), sağda soru, Fluent denetimler
  (alt çizgili kutular, anahtar, açılır liste, radyo kartlar), sağ altta vurgu düğmesi, üstte adım çizgisi; sayfa geçişi
  kayma + solma; iş sürerken tam ekran "Her şey hazırlanıyor" (dönen halka, adım listesi, "Bilgisayarını kapatma").
  Renkler WinLove belirteçleri değil Windows'un Fluent renkleri (kurulumu yapan Windows'u görüyor; memory: oobe-windows-look);
  pencere seçilen temaya ve vurguya anında geçer.
- **Sayfalar:** Ağ (yalnız kablosuz bağdaştırıcı varsa: Native Wifi API ile liste, parola, otomatik bağlan, "Bağlanmadan devam
  et"; WPA2/WPA3/açık/OWE, kurumsal ağlar Windows'a kalır), Hesap (göster düğmeli parola), Bu bilgisayar (ad + **saat dilimi**),
  Görünüm (tema, 9 vurgu, **görev çubuğu hizası** — Win11, **saydamlık**), **Tercihler** (dosya uzantıları, gizli dosyalar, Gezgin
  Bu bilgisayar ile, klasik sağ tık — Win11, arama kutusu, Görev görünümü, Widget'lar / Haberler — ayarlar kataloğunun
  anahtarları), Gizlilik (Az veri: neleri kapattığı etiketlerle). Uygulamada her sayfa ayrı sorulabilir.
- **Akış (VM'de bulundu):** yanıt dosyası sihirbazı **specialize geçişinin son komutu** yapar (`cmd /c powershell … oobe.ps1`,
  Kurulum bekler): logo / "Yükleniyor %42"den sonra SYSTEM olarak, hiçbir hesap yokken tam ekran açılır. Sorular sorulur;
  tema / tercihler Default profile + ilk oturum .reg'ine, gizlilik ve Widget ilkeleri HKLM'ye, saat dilimi ve bilgisayar adı
  hemen yazılır. Hesap specialize'da **oluşturulmaz**: Windows'un iki FirstBoot kancası kaydedilir —
  `HKLM\SYSTEM\Setup\FirstBoot\PreOobe` (OOBE yanıt dosyasını okumadan önce: hesap + tek seferlik otomatik oturum + saat dilimi
  `Panther\unattend.xml`'in oobeSystem geçişine yazılır; OOBE hesabı D-084'te kanıtlanmış yolla kendisi kurar ve açar) ve
  `…\PostOobe` (OOBE'den sonra, ilk oturumdan önce: parolayı ilk oturumda silen SYSTEM görevi, ilk oturum animasyonu kapalı).
  Yanıt dosyası OOBE'nin bütün sayfalarını gizler (EULA, çevrimiçi hesap, kablosuz, yerel hesap, gizlilik, OEM).
- **Yanıt dosyası:** karşılama açıkken hesap / otomatik oturum / saat dilimi yazılmaz; bilgisayar adı **hep** yazılır
  (verilmemişse `WINLOVE-PC`): adsız dosyada OOBE rastgele `DESKTOP-…` verir. Kurulum dosyanın adını bizim komuttan sonra
  bir kez daha uygular (`shsetup.dll`, kendi kopyasından — diskteki dosyayı düzeltmek işe yaramadı): sihirbaz arkasında küçük
  bir **ad izleyicisi** bırakır, Kurulum adı yazınca bekleyen adı seçilene geri çevirir; specialize sonrası zorunlu yeniden
  başlatma seçilen adla açılır (PreOobe ad yine farklıysa bir sonraki açılış için yeniden adlandırır). Dosyanın adı ve saat
  dilimi sihirbazın ilk cevaplarıdır (`WelcomePlan.computerName/timeZone`); saat dilimi yoksa sistem dilinden tahmin.
  D-084 dosyaları (WinLoveSetup + ilk oturum komutu) okununca karşılamaya çevrilir; betik o yolu da hâlâ çalıştırır.
  İlk oturumun ilk saniyelerinde Windows'un varsayılan görünümü görünür, ilk oturum .reg'i birkaç saniyede uygular (D-026).
- **Bir adım başarısız olursa** kancalar kaydedilmez: yanıt dosyasında hesap olmadığından OOBE kendi hesap sayfasını açar,
  kurulum hesapsız kalmaz.
VM'de bulunan tuzaklar: (1) specialize'da Winlogon otomatik oturum değerleri yazılırsa OOBE'nin kendi `defaultuser0` oturumu
bozulur, "Biraz bekleyin…"de sonsuza dek kalır (spec1); (2) Kurulum specialize bitince yanıt dosyası kopyasını bellekten
yeniden yazar — o sırada yapılan düzenleme kaybolur (spec2, `Callback_Unattend_Serialize`); (3) yanıt dosyasında hesap yoksa
`HideLocalAccountScreen` yok sayılır, OOBE "Bu cihazı kimler kullanacak?" der (spec2); (4) Shell-Setup'ın specialize ayarları
(ComputerName yeniden, TimeZone) RunSynchronous komutlarından **sonra**, Kurulumun kendi kopyasından uygulanır; (5) OOBE `EnableFirstLogonAnimation`'ı 1'e geri
alır; (6) Win11 kurulum ortamı Win10 install.wim'i kurmaz ("Windows 11 yüklemesi başarısız oldu") — Win10 için Win10 ortamı.
Kanıt: 345 test / 11.935 doğrulama. VM (laboratuvar yanıtlarıyla sayfalar kendiliğinden ilerler; ağ ve kablosuz bağdaştırıcı yok → Ağ sayfası gizli): WinLove'un ürettiği yanıt dosyasıyla (D-084 dosyasından dönüştürülmüş) **Win11 25H2 26200 Pro (`spec6`) ve Win10 22H2 19045 Pro (`w10e`, Win10 kurulum ortamı) ALL PASSED** — sihirbaz logodan sonra görünür, Deneme yönetici ve kendiliğinden açılır, DENEME-PC, Türkiye saati, koyu + yeşil vurgu, tercihlerin yedisi (Win10'da Win11'e özgü klasik menü / hiza atlanır), gizlilik + Widget ilkeleri, ilk oturum animasyonu kapalı, DefaultPassword / Panther yanıt dosyası / oobe.json silinmiş; Windows'un hesap sayfası ve geçici hesap yok. Tuzakları bulan çalıştırmalar: spec1 (sonsuz bekleme), spec2 (OOBE hesap sayfası), spec3 / spec4 / spec5 (ad rastgele / dosyanınki / dosyanınki), w10a (Win11 ortamı Win10 kurmaz). Görülmeyen: gerçek Wi-Fi'ye bağlanma (liste önizlemede bu bilgisayarda çalıştı), insan eliyle tıklama, Home, ARM64, OEM lisanslı gerçek bilgisayar.

## D-084 — Kendi karşılama ekranımız: Windows'un hesap sayfaları yerine WinLove'un ilk açılış sihirbazı (2026-10-08)
Bağlam: Kullanıcı yeni özellik olarak "Kendi OOBE'miz"i seçti (hesap, bilgisayar adı, tema / vurgu, gizlilik — kurulumu yapan
kişi seçsin). Risk hesap oluşturma ve oturumun devri olduğu için önce VM deneyleri (`oobe1`…`oobe5`, `welcome1`…`welcome3`).
Karar:
- **Yöntem:** Windows'un OOBE sayfaları yanıt dosyasıyla gizlenir (EULA, çevrimiçi hesap, kablosuz, OEM, gizlilik); yanıt dosyası
  bir **kurulum hesabı** `WinLoveSetup` (rastgele 20 karakterlik parola, BCryptGenRandom) açar, bir kez kendiliğinden oturum
  açtırır; ilk oturum komutu `cmd /c start "" powershell … -File "%ProgramData%\WinLove\Oobe\oobe.ps1"` (beklemeden: komut
  listesi sürer). Sihirbaz (`resources/scripts/oobe.ps1`, WPF, tasarım belirteçleri, tam ekran) sorar, sonra:
  hesabı oluşturur (Administrators SID ile, dilden bağımsız), bilgisayarı yeniden adlandırır, gizlilik ilkelerini yazar ve Windows'un
  gizlilik sayfasını kapatır (`DisablePrivacyExperience`), görünümü yeni hesabın ilk oturumuna bırakır (Default profilin RunOnce'ı
  bir .reg alır — çevrimdışı yazılan tema ilk oturumda eziliyor, D-026), kurulum hesabını ve profilini silen bir başlangıç görevi
  kaydeder, yeni hesabın bir kez kendiliğinden açılmasını ayarlar ve yeniden başlatır.
- **VM'de bulunan üç tuzak (çözüldü):** (1) ilk oturum komutları Windows'un "Bu işlem birkaç dakika sürebilir" ekranı sırasında
  çalışır; o ekranın üstüne pencere çıkamaz → pencere girdi masaüstü `Default` olana ve `FirstLogonAnim` süreci bitene dek bekler;
  (2) Windows 11 ilk oturumda Başlat'ı açar ve Başlat en üstteki pencerenin de üstündedir → ön planda Başlat / Arama varsa
  sihirbaz bir Esc gönderip öne geçer; (3) Windows otomatik oturumdan sonra **parolayı silmez** (`AutoLogonCount` 0,
  `DefaultPassword` düz metin kalır) → yeni hesabın ilk oturumunda SYSTEM olarak çalışan bir görev siler ve kendini kaldırır.
  Ayrıca PowerShell'de `0xFF -shl 24` negatif Int32 → renkler Int64 ile hesaplanır.
- **Uygulama:** Katılımsız Kurulum › Hesap: **Karşılama ekranı** anahtarı; açıkken Bilgisayar adını / Tema ve vurguyu /
  Gizliliği sor, Önerilen tema, Önerilen gizlilik (Az veri: zorunlu tanılama, reklam kimliği / etkinlik geçmişi / yazma
  kişiselleştirmesi kapalı · Windows'un varsayılanı), Parolasız hesaba izin ver, **Önizle** (pencere bu bilgisayarda, tam ekran
  değil, hiçbir şey yapmaz). Yerel hesap / parola / otomatik oturum satırları kapanır (değerleri saklanır). Betik + `oobe.json`
  kuyruğa girer (Uygula imaja yazar; `ProgramData\WinLove\Oobe`); bağlanan her imajda yoksa yeniden kuyruğa alınır. Metinler
  yanıt dosyasının kurulum dilinde (tr → Türkçe, değilse İngilizce; yoksa uygulamanın). **ISO Oluştur** karşılama açıkken
  kurulacak sürümün dosya listesinde betiği arar (bağlamadan); yoksa ISO'yu yapmaz (kurulum, kuran kişiyi geçici hesapta bırakırdı).
- Motor: `core/unattend/Welcome` (`welcomeJson`, `welcomeOperations`, `welcomePlanFromOperations`, `editionsWithoutWelcome`,
  `randomWelcomePassword`), `UnattendOptions::welcome` / `withWelcome` (yazarken kurulum hesabı; okurken geri çözülür). CLI:
  `wlcli unattend <xml> --welcome`, `wlcli welcome <changes.json> --strings=… [--auto=…]`.
Kanıt: 344 test. VM (Pro 26200.8037, ağsız; laboratuvar yanıtlarıyla sayfalar kendiliğinden ilerler, yeni hesabın ilk oturumunda
denetim betiği sonucu kayıt diskine yazar): WinLove'un yazdığı yanıt dosyası + değişiklik kümesiyle (`lab_vm -AnswerFile`)
ALL PASSED — Deneme hesabı yönetici, DENEME-PC, koyu tema, yeşil vurgu (palet), gizlilik ilkeleri, `DisablePrivacyExperience`,
`WinLoveSetup` ve profili silinmiş, `DefaultPassword` yok, `AutoAdminLogon` 0, `oobe.json` silinmiş (`welcome2`). Görünürlük (`welcome3`, `welcome4`): animasyon
süreci görüldü ve beklendi, ön plandaki Arama kapatıldı; sihirbaz tam ekran (görev çubuğu da örtülü) "Hoş geldin" ve
"Gizlilik" sayfalarıyla VNC karelerinde, ardından yeni hesap koyu temayla açıldı. Her iki çalışma ALL PASSED.
Görülmeyen: ağ açıkken (Microsoft hesabı sayfaları yanıt dosyasıyla gizli), Home sürümü, Windows 10, insan eliyle girilen
yanıtlar (pencere, önizlemede ve laboratuvar yanıtlarıyla görüldü).

## D-083 — Görev çubuğu sabitlemeleri: Başlat menüsü › Görev çubuğu, Microsoft'un Başlangıç Düzeni ilkesi (2026-10-07)
Bağlam: NTLite eksik listesinden (4). D-067'nin "Sabitlenmiş uygulamalar" anahtarı OEM yöntemini kullanıyordu
(`LayoutXMLPath` + `PinListPlacement="Replace"`); D-070'in VM'lerinde 24H2+ bunu varsayılanların **üstüne ekledi**
(Edge, Store, Outlook yer tutucusu kaldı).
Karar:
- **Yöntem:** Microsoft'un BT belgesindeki makine ilkesi — `HKLM\SOFTWARE\Policies\Microsoft\Windows\Explorer`
  `LockedStartLayout = 1`, `StartLayoutFile = %ProgramData%\WinLove\TaskbarLayout.xml` (REG_EXPAND_SZ) + düzen dosyası
  (`CustomTaskbarLayoutCollection PinListPlacement="Replace"`, yorum yok; boşsa `#leaveempty`). Paketli uygulama `UWA
  AppUserModelID`, masaüstü kimliği `DesktopApplicationID` (Dosya Gezgini `Microsoft.Windows.Explorer`, Edge `MSEdge`),
  kısayol `DesktopApplicationLinkPath`. "Kullanıcı kaldırabilsin" = her sabitlemede `PinGeneration="1"` (24H2 Haziran 2025
  güncellemesinden beri: kaldırılan geri gelmez); kapalıysa ilke her oturumda yeniden sabitler.
- **Yalnız Windows 11:** Windows 10'da aynı ilke Başlat kutucuklarını da kilitler (Microsoft belgesi) → sekme Windows 10
  sürümünde uyarır, kendi düzenini kurdurmaz.
- **Arayüz:** Başlat menüsü sayfasına üçüncü sekme **Görev çubuğu** (Sabitlenenler · Görev çubuğu · Başlat ayarları): Windows
  varsayılanı / Boş / Kendi listem, "Kullanıcı kaldırabilsin"; aynı uygulama listesi (imajdan; başta Dosya Gezgini, imajın
  kendi kısayolunun adı ve simgesiyle) ve sıralama ızgarası (soldan sağa). `StartPinsController` iki yüzeyli
  (`Surface::Taskbar`, uygulamaları Başlat denetleyicisinden); plan kuyruktan geri okunur, presetle gider. Nav rozeti ikisinin
  toplamı. Eski anahtar `taskbar-pins` katalogdan çıktı.
- Bu işte bulunan eski hata: Shell yıkılırken sayfalar denetleyicilerden sonra yıkılıyordu (StartMenuPage'in yıkıcısı serbest
  bırakılmış denetleyiciye yazıyordu, yeni render'da 0xC0000005). `~Shell` önce çocukları kaldırır.
Kanıt: VM (Pro 26200.8037, ağsız): liste → görev çubuğunda yalnız Dosya Gezgini, Not Defteri, Terminal, Hesap Makinesi,
Ayarlar (Edge, Store, Outlook yer tutucusu yok); boş → hiç sabitleme yok; ikisi de masaüstüne kadar ALL PASSED. 341 test
(düzen XML'i, kuyruktan geri okuma, iki yüzeyin ayrı planları, kipler, PinGeneration).
Görülmeyen: kullanıcının bir sabitlemeyi kaldırıp yeniden oturum açması (PinGeneration'ın geri getirmemesi), ağ açıkken
sonradan gelen yer tutucular, Home sürümü.

## D-082 — Uyumluluk korumaları: kalan uygulamaların çalışma zamanları ve kullanıcının açtığı korumalar (2026-10-07)
Bağlam: NTLite eksik listesinden kullanıcının seçtiği (3). Bir kaldırma ya da devre dışı servis, imajda kalan bir uygulamanın
ya da kullanıcının istediği bir özelliğin (güncelleme, yazdırma, Wi-Fi …) çalışmasını bozabiliyordu; sayfa yalnız notla uyarıyordu.
Karar:
- **Kalan uygulamaların çalışma zamanları (her zaman):** uygulama listesiyle birlikte her paketin `AppxManifest.xml`'i
  (WindowsApps, yedekleme semantiği) okunur, `<PackageDependency Name>`'leri `AppxComponent::needs`. Kalan bir uygulamanın
  ihtiyaç duyduğu paket kaldırılamaz; uygulamalar da kuyruktaysa serbest. Ölçüm: 25H2 Pro'da 48 hazır uygulamanın 36'sı
  VCLibs / UI.Xaml / NET.Native / WindowsAppRuntime'a bağlı ama bunlar **hazır paket listesinde değil** (kaldırılamaz,
  kilit gerekmez); Windows 10 22H2 Pro'da `Microsoft.VCLibs.140.00` listede ve 40 uygulamanın 39'u ona bağlı — kilit orada.
- **Uyumluluk korumaları** (`resources/catalog/compat.json`, 13 koruma; açık olanlar `settings.json` → `guards`, yeni
  kullanıcıda katalog varsayılanı): Windows Update, Store ve uygulama kurulumu, Yazdırma, Wi-Fi ve Bluetooth, Ses, Edge
  tabanlı uygulamalar (WebView2), Kurtarma (varsayılan açık); Windows Güvenliği, Xbox, Ağ paylaşımı, Uzak Masaüstü, Kamera,
  Doğu Asya metni (kapalı). Her biri uygulama önekleri, `components.json` kimlikleri (WinSxS küçültme, derin kaldırma, WinRE,
  WebView2 …) ve **devre dışı bırakılamayacak** servisler sayar (el ile / otomatik serbest). Yalnız kesin bağımlılıklar:
  fazlası kullanıcıyı kapatmaya iter, eksiği imajı bozar.
- **Programlar sayfası:** program seçiliyken App Installer kaldırılamaz (pencere winget ile kurar) — otomatik koruma.
- **Uygulama:** Bileşenler'de tutulan satır kilit simgesi + soluk kutu; tıklayınca uyarı; grup kutusu tutulanları atlar;
  Inspector "UYUMLULUK" bölümünde neden ("Korunuyor: …" / "Kullanan: …") ve nereden değişir. Başlıkta "Uyumluluk · N" →
  pencere (koruma · neyi korur, kutular, Kaydet). Servisler'de korunan servisin seçim kutusunda kilit, "Devre dışı" reddedilir.
  Kuyruk her değiştiğinde (koruma açıldı, bir uygulama kuyruktan geri alındı, preset, Ayarlar / Tweaks, geri al) artık
  tutulan işlemler kuyruktan çıkar ve uyarı ne çıktığını söyler (`CompatController`, çekirdek kural: `core/ops/Compat`).
Kanıt: 340 test (çekirdek kural, manifest ayrıştırma, katalog: her bileşen kimliği ve uygulama öneki gerçek kataloglarda;
denetleyici: Store kilidi, VCLibs'in uygulamalarla serbest kalıp Hesap Makinesi geri alınınca çıkması, Programlar → App
Installer, Spooler / WlanSvc). Gerçek imaj: 25H2 ve Windows 10 bağımlılık grafiği `wlcli appx` ile; korumalardaki 38 servis
adının hepsi 25H2 imajında var.
Görülmeyen: bir korumanın bozduğunu söylediği şeyin VM'de bozulduğu (servisler bilinen Windows davranışı; VCLibs bağımlılığı
Microsoft'un manifest beyanı).

## D-081 — Modlu Windows: DISM / wimgapi yedeği, sağlık uyarısı, okunamayan ESD'nin açıklaması (2026-10-07)
Bağlam: bir kullanıcıda ESD → WIM "İşlem başarısız (0x8007000B) wimgapi call failed" (install.esd, iki sürüm, 22631.3007).
WinLove `dismapi.dll` / `wimgapi.dll`'i ana makinenin System32'sinden yükler (D-017); kullanıcının sorusu: "modlu
windowslarda uygulama düzgün çalışmayabilir". 0x8007000B (ERROR_BAD_FORMAT) iki yoldan gelir: dosyanın biçimini o
wimgapi bilmiyor (eski / değiştirilmiş kopya ya da Windows'un yazmadığı bir ESD) ya da kopya bozuk.
Karar:
- **wimgapi dosya başına seçilir** (`WimGapi.cpp`): her genel işlem önce bu PC'ninkiyle dosyayı açıp ilk sürümün dosya
  listesini okur (`WIMCreateFile` + `WIMLoadImage(1)`, ~50 ms, dosya sürümü başına hatırlanır); okuyamazsa sırayla
  **Windows ADK'nınki**, sonra **dosyanın yanındaki** (`sources\wimgapi.dll`, kurulum ortamının kendi kopyası) denenir.
  System32 dışındaki kopya yalnız Microsoft imzalıysa yüklenir (gömülü ya da bu PC'nin kataloğu — WinLove yönetici
  çalışır, ortam kullanıcının dosyasıdır). Dönüşüm (`exportImages`: ESD → WIM, SWM → WIM, sıkıştırma değişimi) yeni
  dosyaya yazarken yarıda kalırsa da diğer kopyalarla baştan dener.
- **Hata metni kopyayı söyler:** "wimgapi call failed: wimgapi.dll 10.0.26100.8972 (system)". Hiçbiri okuyamazsa:
  "no wimgapi could read the image (tried …)" + ESD ise katı kaynaklarının kendi başlıkları (`solidResources`):
  sıkıştırma ve parça boyu; LZMS dışıysa "Windows writes LZMS only: this ESD was made by another tool". Yarım dosya kalmaz.
  0x8007000B'nin arayüzdeki açıklaması (`Remedy::WimLibrary`).
- **DISM:** System32'de `dismapi.dll`, `dism.exe` ya da `Dism\` altındaki çekirdek parçalardan biri (DismCore, DismProv,
  Wim/Folder/Imaging/LogProvider) yoksa ve ADK kuruluysa onun DISM'i kullanılır (`dismLocation()`; `dism.exe` çağrıları
  da). Ayarlar / Hakkında'daki DISM satırı yüklenen kopyayı gösterir ("· Windows ADK").
- **Sağlık denetimi** (`checkHostDism`, açılışta, ~0.1 sn): dismapi, wimgapi, dism.exe, 12 DISM sağlayıcısı, wimmount.sys
  var mı, Microsoft imzalı mı; WIMMount hizmeti devre dışı mı. Kendi kernel32.dll'i doğrulanamayan bir PC'de (katalog
  silinmiş) yalnız eksikler. Sorun varsa Kaynak sayfasında uyarı şeridi: ilk sorun + sayı + ne yapıldığı (ADK kullanılıyor /
  ADK kurulu / "ADK Deployment Tools kurulursa onun DISM'i kullanılır").
- `wlcli host-check`, `wlcli wimgapi <dosya>`, seçenekler `--wimgapi=<dll>` (System32'ninkinin yerine; yedeği kanıtlamak
  için), `--dism=adk`.
Kanıt: 335 test (imza: kernel32 katalogdan "Microsoft Windows", sahte dll imzasız; sahte LZX-solid ESD'de hata metni ve
yarım dosya yok). Lab: `--wimgapi=version.dll` (giriş noktaları yok) ile Win10 ESD'nin 2. sürümü ADK kopyasıyla 21 sn'de
WIM'e döndü, `wlcli verify` 12 185 akış sağlam. `--dism=adk` ile 25H2 install.wim salt okunur bağlandı: paketler,
özellikler, uygulamalar System32 DISM'iyle birebir aynı (591 satır, fark yok).
Görülmeyen: kullanıcının dosyası (elimizde yok) — 0x8007000B'nin onda hangi yoldan geldiği; ADK'sız modlu bir PC'de ortam
kopyasının bu PC'nin kataloğuyla doğrulanması (bu PC'de 26100.1 kopyası doğrulandı); Windows 10 ana makine.

## D-080 — Kurulum ortamı da güncellenir: WinRE (Safe OS), boot.wim, kurulum dosyaları (2026-10-07)
Bağlam: NTLite karşılaştırmasında kullanıcının seçtiği eksik: toplu güncelleme yalnız install.wim'e gidiyordu; WinRE ve
kurulum ekranı (boot.wim) medyanın yaşında kalıyordu. Katalog Safe OS ve Setup dinamik güncellemelerini tanıyor ama
kullanmıyordu. Yol: Microsoft'un "Update Windows installation media with Dynamic Update" sırası.
Karar:
- **Katalog:** `CatalogTarget::dynamicUpdates` → Safe OS ve Setup sorguları da yapılır (uygulamada açık; `wlcli catalog
  --dynamic`). İndirme sonucu katalog türünü taşır (`DownloadedUpdate::kind`): Safe OS paketinin dosya adı ne olduğunu söylemez.
- **WinRE (Uygula):** Safe OS paketi kuyruğa `AddPackage` "safeos" olarak girer (Güncellemeler'de "WinRE (Safe OS)"),
  Planner onu imajın güncellemelerinden sonraya koyar. `updateWinRe`: `Windows\System32\Recovery\Winre.wim` dışarı
  kopyalanır, bağlanır, aynı çalıştırmadaki toplu güncelleme (servis yığını için; bilinen 0x8007007E yok sayılır) ve Safe OS
  eklenir, `/ResetBase /Defer`, dışa aktarılır (önyükleme sürümü korunur), gizli + sistem olarak geri konur
  (`replaceImageFileFrom`). İmajda WinRE yoksa adım atlanır. `wlcli winre-update`.
- **boot.wim (ISO Oluştur):** toplu güncelleme boot.wim'in bütün sürümlerine (`BootPatch::lcu`; 0x8007007E'de ikinci
  geçiş), temizlik, sonra kurulum sürümünün **bütün `sources\` klasörü** ve önyükleme yöneticisi (`bootmgfw.efi`,
  `bootmgr.efi`, `boot.stl`) alınır, boot.wim dışa aktarılıp önyükleme sürümü işaretlenir. `wlcli boot-patch --lcu
  --setup-files`.
- **Kurulum dosyaları:** Setup güncellemesi açılır (`expand -F:*`; ortamda olmayan dillerin klasörleri alınmaz), üstüne
  boot.wim'in `sources\`'ı, önyükleme yöneticisi ortamdaki her `bootmgfw/bootx64/bootia32/bootaa64.efi` ve `bootmgr.efi`
  yerine. Kurulum klasörü değişmez: ISO / USB yazıcısının değiştirilen dosyaları (`isNew`: yeni dosya ve klasörler de).
  `wlcli iso --setup-du --boot-files`.
- **Microsoft'tan sapma (ölçüldü):** Microsoft yalnız `setup.exe` + `setuphost.exe`'yi boot.wim'den kopyalıyor. Bizim
  denememizde (Eylül toplu güncellemesi 14 Eylül, Setup güncellemesi 22 Eylül) kurulum "Windows 11 yüklemesi başarısız
  oldu" dedi; günlük: ikinci aşama ortamın diske kopyasından çalışır ve SetupHost başka yapının Setup Platform'unu reddeder
  ("Determine if the expected version of Setup Platform has been loaded" → 0xC1900100). Yalnız boot.wim güncellenince
  "medya sürücüsü eksik" hatası. → ortamın `sources\`'ı boot.wim'inkiyle bütünüyle eşitlenir.
- **Arayüz:** Güncellemeler › Katalog'da dört tür; Setup güncellemesi ve toplu güncelleme `AppState::mediaUpdate`'e
  (kuyruğa değil: Uygula'dan sonra da ISO için durur). ISO Oluştur / USB: "Ortam güncellemesi — boot.wim ve kurulum
  dosyalarını güncelle" (yanında KB'ler, özette satır, süreye +450 sn).
Kanıt: 332 test; WinRE lab 26100.8031 → 9545 (80 sn), VM'de `reagentc /boottore` ile güncel WinRE açıldı; boot.wim lab iki
sürüm 8037 → 9457 (443 sn); ISO'da `setup.exe` / `bootx64.efi` boot.wim'inkiyle aynı; ayrıştırma VM'leri yukarıdaki iki
hatayı gösterdi, kurulum günlüğü `tools\lab_setup_logs.ps1` ile WinPE'den alındı; eşitlemeyle `wlcli iso --setup-du
--boot-files` yolu iki VM'de masaüstüne kadar kurdu (WinRE'si güncel ve özgün install.wim). Uygula yolu (`wlcli apply`,
"safeos" işlemi): WinRE 8031 → 9545, 75 sn.
Görülmeyen: ARM64; Windows 10 ortamı; aynı haftanın Setup güncellemesiyle Microsoft'un dar yöntemi.

## D-079 — WinSxS en aza indirilir (tiny11 "core" yöntemi), geri dönüşsüz; Bileşenler › Temizlik (2026-10-07)
Bağlam: Kullanıcı: "winsxs boyutunu maksimum seviyede düşürmeye çalış… tiny iso dosyaları yapıyorlar"; uyarıyla, hangi
sayfaya konacağı bana bırakıldı. Ölçüm (25H2 Pro, bağlı imaj): gezginde WinSxS 10,33 GB / 18 058 klasör, bunun 7,22 GB'ı
System32 & co. ile sabit bağlantı (silmek yer açmaz), yalnız WinSxS'te duran 3,12 GB.
Karar:
- **Motor (`core/image/dism/StoreShrink`):** WinSxS'te kalanlar tiny11 Coremaker listesi: `Catalogs`, `FileMaps`, `Fusion`,
  `InstallTemp`, `Manifests`, `SettingsManifests`, `Temp`; her mimaride common-controls (+ .resources), gdiplus,
  isolationautomation, i..utomation.proxystub, vc80.crt, vc90.crt ve bunların `policy.N.M.*` yönlendirmeleri; servis yığını
  (`microsoft-windows-servicing*`, `s..ngstack*`, `s..stack-*`). Gerisi silinir: SeRestore ile POSIX silme (ACL / sahiplik
  değişmez), olmazsa sahiplik alınarak; junction'a girilmez. Önce her dosyanın bağlantı sayısıyla ölçülür (yalnız bağlantı
  sayısı 1 olan bayt "açılan"dır). `pending.xml` varsa (aynı çalıştırmada açılan özellik ilk açılışta tamamlanacak) ve
  servis yığını bulunamazsa reddeder. `wlcli store-shrink <mount> [--dry-run]`.
- **İşlem:** yeni `ShrinkStore` (hedef `component-store-shrink`, tek yuva), Planner'da yeni son aşama `Phase::Shrink`
  (Ayarlar'dan da sonra: dil / varsayılan uygulama adımları dism.exe kullanır). Risk onayında listelenir, presetle taşınır.
- **Arayüz:** ayrı sayfa değil, ISO Oluştur da değil (orası imaj içeriğini değiştirmez): Bileşenler › Temizlik'te
  ResetBase'in yanında ikinci satır "WinSxS'i en aza indir (geri dönüşsüz)", yüksek risk; boyut sütunu bağlanan imajda
  ölçülür (bileşen yoklamasıyla, ~5 sn). Seçilince Ayarlar'ın "Otomatik güncelleştirmeler: Kapalı" yazımı
  (`WindowsUpdate\AU\NoAutoUpdate = 1`) aynı kuyruk düzenlemesiyle eklenir; satır çıkarılınca ayar kalır (Ayarlar gösterir).
  Uyarı bandı "uygulanacak" der (kaldırma değil).
Kanıt: 330 test; kuru çalıştırma (25H2 Pro): 17 642 klasör / 50 154 dosya / 10,12 GB gider, 416 klasör kalır, 2,97 GB
açılır (~5 sn). Lab: temizlik + küçültme 22 sn, sonrasında DISM oturumu hâlâ açılıyor; tek sürüm install.wim 6,96 → 4,90 GB.
VM `shrink-max` (ağlı, -Diag): kurulum + ilk oturum + masaüstü, WinSxS 427 klasör, SideBySide olayı 0, sistem ve
kurulan programlar açılıyor, winget 4/4 (VC++ MSI dahil), `dism /online /get-packages` çalışıyor. Aynı koşullu çift:
kurulu sistemde C: 17,46 GB / 19,66 GB → −2,20 GB (STATUS).
Görülmeyen: kurulu sistemde aylar içinde ne bozulduğu; Windows Update elle çalıştırılınca ne olduğu; otomatik güncellemeler
kapalıyken Defender imzalarının ne zaman güncellendiği (25 dk'da ikisinde de güncellenmedi); ARM64 / Windows 10
imajı; küçültülmüş imaja sonradan bir şey eklenmesi (DISM'in reddetmesi beklenir).

## D-078 — Programlar: winget deposu winget'siz okunur, ilk oturumda WinLove'un penceresiyle kurulur; hazır paketler (2026-10-07)
Bağlam: Kullanıcı yeni özellik olarak "Program kurulumu (Ninite gibi)"yı seçti: "binlerce uygulama datası içerebilir,
güncel veriyi çekebilir". Hazır *ayar* profillerini reddetti (her yeni özellikte güncellenmesi gerekir); hazır *program*
paketlerini ise kendisi istedi ("geliştiriciler için, oyun oynayanlar için"). İnternet yoksa kurulum sonraki oturuma
ertelenmez: bağlantı beklenir, kullanıcı bunu ekranda görür.
Karar:
- **Motor (`core/programs`):** `cdn.winget.microsoft.com/cache/source2.msix` indirilir, Authenticode imzası
  `WinVerifyTrust` ile denetlenir (imzalayan "Microsoft Corporation"), içinden `Public\index.db` (`IAppxFactory`) çıkarılır,
  Windows'un kendi SQLite'ı (`winsqlite3`) ile okunur — winget kurulu olmasa da. Arama ad / kimlik / moniker / etiket
  sıralı; etikete göre ve bütün depo listesi. Bir paketin ayrıntısı `versionData.mszyml` (MSZIP, SHA-256 = dizindeki
  hash) → birleşik manifest (SHA-256) → simge (`IconSha256`); hepsi `<çalışma>\winget`'te önbellek, dizin günde bir
  yenilenir. `wlcli programs-index | programs-search | programs-show`.
- **Kurulum:** seçimler Kurulum Sonrası planında (`PostSetupPlan::programs` + pencere metinleri): tek kuyruk işlemi,
  presetle taşınır. `postsetup-machine.cmd` (SYSTEM) "WinLove Programs" görevini kaydeder (BUILTIN\Users, en yüksek yetki,
  oturum açılışından 15 sn sonra). `programs.ps1` (WPF; WinLove'un açılış ekranı dili, renkler tokens.json'dan, açık /
  koyu) Kurulum Sonrası görevini bekler, winget'i kaydettirir, ağı bekler (bantta), programları tek tek
  `winget install --id X --exact --silent --source winget` ile kurar; "zaten kurulu" ve "yeniden başlatma" kodları
  başarıdır, başarısız olan bir kez yeniden denenir. Bitince görev kendini siler (`%ProgramData%\WinLove\programs-done.txt`);
  pencere kapatılırsa sonraki oturumda kalanlarla sürer.
- **Sayfa (P22, Uygulamalar'ın altında):** arama (bütün depo), Kategori (katalogdaki 12 kategori + Bütün depo), Yalnızca
  seçili, 6 hazır paket kartı, tablo + sağda ayrıntılar. Kategori = `programs.json`'daki öne çıkanlar + winget etiketleriyle
  "N program daha". Katalog yalnız kimlik + etiket taşır (ad, sürüm dizinden gelir): bakım istemez;
  `tools/check_programs.py` her kimliği ve etiketi dizinde arar. App Installer (winget) Uygulamalar'da kaldırılacaksa
  sayfa kırmızı bant gösterir. Kurulum Sonrası'nın eski "Hazır uygulamalar" dialogu kaldırıldı, düğme bu sayfayı açar.
- **Önizleme:** "Kurulum penceresini önizle" pencereyi bu bilgisayarda `dryRun` ile açar (hiçbir şey kurulmaz).
- **Yönetici istemeyen yükleyiciler:** winget `0x8A150056` ("cannot be run from an administrator context", Spotify) verirse
  aynı komut, oturumdaki kullanıcının sınırlı yetkili geçici görevi olarak yeniden çalışır (parola / UAC yok), çıkış kodu
  dosyadan okunur, görev silinir.
Kanıt: 330 test; gerçek pencerede (gui.py) arama, kategori, Bütün depo (15 403 paket), paketler, ayrıntılar, önizleme;
VM `programs-diag`: 6 programdan 5'i ilk oturumda ~6 dk'da kuruldu, Spotify `0x8A150056` → düzeltme → VM `programs-spotify`:
Spotify yükseltilmeden kuruldu. VM `shrink-max`: küçültülmüş WinSxS'li sistemde 4/4 (VC++ MSI dahil).
Görülmeyen: internet yokken bekleme (VM), bütün depodan rastgele programlar, ARM64 imaj.

## D-077 — AIO: Win10 + Win11 karışık ISO Win10 kurulum ortamıyla kurulur; sürüm sıralama, ad çakışması (2026-10-06)
Bağlam: Kullanıcı AIO ISO istedi (Win10 + Win11 karışık, tek mimari — kullanıcı seçimi). Sürüm ekleme / yeniden adlandırma /
silme zaten vardı (D-033, D-035, D-058); bilinmeyen kurulum tarafıydı.
Ölçüm (`tools\lab_aio.ps1`, AIO = Win11 25H2 Pro + Win10 22H2 Pro; Win10 resmî MCT ESD'si, SHA-1 doğrulandı; misafirin
kendisinin kapandığı `vmware.log` "PIIX4: PM Soft Off" ile doğrulandı):
| Kurulum ortamı | Kurulan | Sonuç |
|---|---|---|
| Win11 25H2 (yeni kurulum) | Win11 | masaüstü |
| Win11 25H2 (yeni kurulum) | Win10 | "Windows 11 yüklemesi başarısız oldu" |
| Win11 25H2 (önceki kurulum, D-074) | Win10 | dosyalar kopyalandı, "önyüklenecek şekilde hazırlayamadı" |
| Win10 22H2 (ESD'den MCT medyası: index 1 dosyalar, 2+3 boot.wim) | Win10 | masaüstü |
| Win10 22H2 | Win11 25H2 | masaüstü (yanıt dosyasında LabConfig atlatmaları vardı) |
Karar: karışık AIO'nun kurulum ortamı **Win10'unki** olmalı (24H2+ ortamı Win10 kuramaz). Motor: `core::reorderImages`
(`wlcli reorder`, Setup sürümleri dosya sırasıyla listeler), İmajlar satır menüsünde "Yukarı / Aşağı taşı"; sürüm eklerken
aynı ada sahip yeni sürüm sürüm etiketini alır ("Windows 11 Pro (24H2)", aynı sürümse tam sürüm numarası —
`core::distinctEditionNames`). Laboratuvar: `lab_vm.ps1 -InstallWim/-ImageIndex/-BootWim/-SetupFolder`, lab VM'lerinde
`logging = "TRUE"` (bu makinede VMware günlüğü genel olarak kapalı) ve **geçme koşulu artık ACPI soft-off**: elle
`vmrun stop` edilen VM bundan sonra "geçti" sayılmaz (D-075 bölmesinde elle kapatılanlar yanlışlıkla PASS yazmıştı).
**2. adım (aynı gün):** `core::setupMediaBuild` / `editionsMediaCannotInstall` (boot.wim'in build'i `SourceInfo.boot`'tan,
dosya okumadan) ve `core::replaceSetupMedia` (ISO / kurulum klasörü / MCT ESD'si → önce yanındaki `.media` klasöründe
tamamlanır; `sources\install.*`, `sources\$OEM$`, `autounattend.xml` kalır; yeni ortam 24H2+ ya da başka mimariyse
reddedilir); `wlcli media-check`, `wlcli setup-media`. ISO sayfası: karışık AIO 24H2+ ortamdaysa uyarı şeridi
("Windows 10 sürümleri kurulamaz: <sürümler>") + **"Windows 10 ortamını al…"** (dosya seçici → `ImageController::
replaceSetupMedia`, ISO kaynak önce çalışma klasörüne) ve ÖZET'te "Kurulum ekranı: Windows 10 sürümleri kurulamaz"
(önceki kurulum da çözmez — ölçüldü). Render `--demo-aio`.
Kanıt: 317 test / 11.411 doğrulama; `lab_aio.ps1 -Cases swap-w10,swap-w11` ALL PASSED: Win11 medyası + AIO →
media-check 1 (Win10 Pro işaretli) → setup-media (ESD) → media-check 0, install.wim aynı → Win10 ve Win11 masaüstü.
**3. adım (2026-10-07, kullanıcı yokken, `tools\gui.py` ile uygulamanın içinden):** Kaynak › Dosya aç… (test ISO'su) →
İmajlar › Araçlar › Başka imajdan sürüm ekle… (Win10 ESD, yalnız Pro) → 7 sürüm (3 dk) → ISO Oluştur'da uyarı + "Windows 10
ortamını al…" (ESD, 31 sn) → uyarı kalktı → ISO 29 sn'de yazıldı, `media-check`: her sürüm kurulabilir. VM: bu ortamla Win11 Pro
**atlatmasız** (TPM'siz, Secure Boot'suz) masaüstüne kuruldu (`lab_vm.ps1 -NoBypass`, ALL PASSED). Bulunan ve düzeltilenler:
(1) sürüm ekleme diyaloğu MCT ESD'sinin "Windows Setup Media" ve iki Windows PE imajını da işaretli listeliyordu → yalnız
Windows sürümleri (`core::isWindowsEdition`), hiç yoksa hata bildirimi; (2) Win10 ortamında "Önceki kurulumu kullan (24H2+)"
seçilebiliyordu ve özet "yeni kurulum" diyordu → kutu kapalı + devre dışı, açıklama ve özet "önceki kurulum (ortamın kendisi)"
(`core::mediaOpensPreviousSetup`); (3) Win10 22H2 XML'de 19041 → "10 2004" etiketi, ve bağlı imajda "Güncellemeleri bul" /
dil indirme 2004'ü hedefliyordu → 19041 sürümlerin build'i dosya listesindeki etkinleştirme paketinden (`wimFolderNames`,
`core::enablementBuild`; 22H2 = 19045). ESD'de (LZMS) okunamıyor: ESD → WIM sonrasında doğru.

## D-076 — Preset uygulanırken bileşen tarifleri güncel katalogdan gelir (2026-10-06)
Bağlam: D-075'ten sonra kullanıcının kurulumunda görev çubuğunda Outlook vardı. Preset'teki `outlook-install` tarifi 1.0.1'den
eski (`Windows\InboxApps\OutlookPWA.msix` yok; "Bulutun eklediği sabitlemeler" ayarı da preset'te yok). Ağsız lab kurulumunda
(`fixed`) Outlook yok: sabitleme ağ varken ilk oturumda geliyor. Tarif operasyonun içinde taşındığından katalog düzeltmeleri
eski preset'lere hiç ulaşmıyordu.
Karar: `ComponentController::withCurrentRecipes` — Shell::applyPreset'te katalogda kimliği olan her RemoveComponent'in tarifi,
başlığı ve riski güncel katalogdan; katalogda olmayan kimlik kendi tarifini korur (çekirdek yine doğrular, D-075 dönüşümü de
kalır — wlcli / eski kuyruk için). Kanıt: birim testi (314 / 11.392). **Görülmeyen:** ağlı VM'de güncel Outlook tarifi +
`cloud-content` ile görev çubuğunun Outlook'suz kalması.

## D-075 — Telemetri bileşeni paketi kaldırmaz; TroubleShooting paketi hiçbir tarifle kaldırılamaz (2026-10-06)
Bağlam: Kullanıcının "her şey işaretli" preset'iyle (515 işlem, HSL) kurulan sistem, dosya kopyalamadan sonraki ilk açılışta
(specialize) logosuz siyah ekran + dönen halkada takılıyordu. VM diski: `IMAGE_STATE_GENERALIZE_RESEAL_TO_OOBE`, setupapi /
olay günlüğü yok, açılış izinde LSM ~110 sn start-pending.
Ölçüm (lab_vm, 14 kurulum, ikiye bölme): kayıt + servisler (397), diller (31), eski sürücü sınıfları + ResetBase, Store
uygulamaları, Edge/OneDrive/yer tutucular, medya, yazı tipleri/Yardım/Uzaktan Yardım, Defender tanımları, **Defender tamamen**
(servis anahtarları silinerek de) → masaüstü. Yalnız `telemetry` (`Microsoft-OneCore-TroubleShooting-Package` + WOW64) içeren
3 kurulumun 3'ü de takıldı (31 / 11 / 7+ dk siyah ekran; geçenlerde 1-4 dk).
Karar: `telemetry` artık paket kaldırmaz: DiagTrack + dmwappushservice Start=4, AllowTelemetry=0 (`always`, düşük risk).
Çekirdek (`SystemComponents`): bu iki aile `validateComponentRecipe`'te reddedilir; eski preset'in tarifi okunurken paket
düşürülür, yerine aynı üç yazım eklenir (uyarı günlüğe). Tarif operasyonun içinde taşındığı için katalog tek başına yetmezdi.
Kanıt: birim testleri (313 / 11.382). **Görülmeyen:** düzeltilmiş motorla kullanıcının preset'inin kurulumu (lab `fixed`
koşusu başlatıldı, sonucu kullanıcı testiyle birlikte); kullanıcının çalışma klasöründeki imajdan paket zaten silinmiş —
orijinal ISO'dan yeniden uygulanmalı.

## D-074 — Önceki kurulum (24H2+) ve WinRE'siz imaj (2026-10-06)
Bağlam: Kullanıcı: "boot setup yeni arayüze geçti, eski düzene geçirebilir miyiz, ISO oluştur'a özellik olarak ekleyelim";
aynı oturumda: "hazırladığım ISO %5'te hata veriyor, eski sürüm setup'ta sorun yok". Kullanıcının ISO'sunda `removeComponent winre`
uygulanmıştı: install.wim'de `Recovery\ReAgent.xml` var, `Winre.wim` yok. 24H2'den beri boot.wim'de
`HKLM\SYSTEM\Setup\CmdLine = winpeshl.exe`, winpeshl `X:\setup.exe`'yi (yeni kurulum) başlatıyor; "Kurulumun önceki
sürümü" `X:\sources\setup.exe`. Yeni kurulum kurduğu imajın WinRE'sini kullanıyor (NTLite: "Modern setup uses winre.wim").
Karar:
- **Motor:** `BootPatch::legacySetup` — D-038'in boot.wim yamasına bir değer: `Setup\CmdLine = cmd /c start /min wpeinit &&
  \sources\setup` (NTLite topluluğunda gerçek donanım + Ventoy/YUMI ile denenmiş biçim; yalın `X:\sources\setup.exe` çoklu
  önyükleme çubuklarında sürücü yüklemesini bozuyordu: wpeinit'i winpeshl çalıştırıyordu). İmajda `sources\setup.exe` yoksa hata
  (kurulum imajı değil). `wlcli boot-patch --legacy-setup` (yalnız başına da).
- **WinRE algılama, bağlamadan:** `wimFileExists` (WimVerify'ın kaynak okuyucusu + dizin girdisi ayrıştırıcı: güvenlik verisi,
  102 baytlık girdi, ek akışlar 8'e hizalı) bir sürümün dosya listesinden yol arar; ~0,2 sn, yönetici gerekmez, ISO içinde de
  çalışır. `editionsWithoutWinre` 26100+ sürümlerde `Windows\System32\Recovery\Winre.wim`'e bakar. `wlcli wim-file`.
- **Arayüz:** ISO / USB sekmelerinde ÖNYÜKLEME › "Kurulum ekranı: Önceki kurulumu kullan (24H2+)", varsayılan kapalı; WinRE'siz
  sürüm varsa kendiliğinden açılır (kullanıcı dokunduysa seçimi korunur), yanında uyarı; özette "Kurulum ekranı" satırı, kapalı
  ve WinRE yoksa turuncu "yeni kurulum — WinRE yok, hata verir". Bileşenler'deki WinRE notuna bu bilgi eklendi.
**Kanıt:** 312+ test; `tools\lab_legacy_setup.ps1` (yönetici, kendim): CmdLine yazıldı, geri okundu (özgünü `winpeshl.exe`),
13 869 akış sağlam. `-Vm`: test ISO'sunun 4. sürümünden **yalnız** `Winre.wim` silindi, iki ISO yalnız boot.wim'de farklı —
yeni kurulum ilk 30 sn'de "Windows 11 yüklemesi başarısız oldu" (hata yeniden üretildi); önceki kurulum aynı ISO'yu kesintisiz kurdu, ilk oturumdan sonra
kendini kapattı (ALL PASSED, ~25 dk; kareler `build\lab\out\vm-legacy-*`).
Kullanıcının ISO'sunda `wlcli wim-file … Winre.wim` → MISSING. Render `--demo-no-winre` (ISO / USB, tr / en).
**Görülmeyen:** kullanıcının kendi ISO'sunun (520 değişiklik) önceki kurulumla kurulması — öteki değişiklikler ayrıca
denenmedi; gerçek donanım / Ventoy; ESD kaynakta WinRE algılama (desteklenmiyor: kutu kendiliğinden açılmaz).

## D-073 — 7TSP simge paketleri; uzun gezinme çubuğu ve risk dialogu kaydırılır (2026-10-06)
Bağlam: Kullanıcı: "Simgeler sayfasında tek tek değiştirmek yerine bu gibi paketleri de direkt uygulatabilmeli"
(`7TSP Lumicons Symbols.7z`). Aynı oturumda iki hata: kategori sayısı arttıkça gezinme çubuğunun altında öğeler "Daralt" ile üst
üste biniyordu; Uygula'nın risk onayı uzun listede pencereye sığmıyor, onay kutusu ve düğmeler ekran dışında kalıyordu.
Karar:
- **7TSP biçimi** (`core/image/icons/ResFile`): `Pack.ini` (`Pack=`, `Base by=`) + `Resources\<hedef>.res` — hedef başına bir
  derlenmiş kaynak dosyası (`imageres.dll.mun.res` …). `.res` kendi okuyucumuzla `ResourceTree`'ye okunur (sınır denetimli),
  `listIconGroups` aynen çalışır; her simge grubu WinLove paket düzenine `.ico` olur (`<hedef>\<id>.ico`, adlı gruplar adıyla)
  ve D-068'in denetimli yama yolundan geçer — yeni yazma yolu yok. Simge dışı kaynak türleri yok sayılır.
- **Arşiv:** Windows'un kendi `tar.exe`'si (libarchive 3.8: Windows 11'de 7z + zip), olmazsa kurulu 7-Zip; ikisi de mutlak yol
  ve `..` reddeder. Dönüştürülen paket `%LOCALAPPDATA%\WinLove\IconPacks\<paket adı>`'da kalır (kuyruktaki kaynaklar oraya
  bakar); açılan geçici kopya silinir. Klasör olarak seçilen 7TSP paketi de tanınır.
- **Eşleşmeyenler kuyruğa girmez, sayılır:** imajın dosyasında olmayan grup (başka build için yapılmış paket) — **düzeltme:**
  klasör paketleri de bunu kuyruğa alıyordu, Uygula'da o dosyanın bütün yaması düşerdi; kod içeren hedefler (`Display.dll`) ve
  `.mun` olmayanlar (`explorer.exe.mui`) D-068 gereği yamalanmaz.
- **Sayfa:** "İkon paketi yükle…" artık küçük bir menü: "Arşivden (.7z / .zip · 7TSP)…" / "Klasörden…".
- **Gezinme çubuğu:** öğeler bir kaydırma alanında (tekerlek + ince kaydırma çubuğu, klavyeyle gidilen öğe görünür kalır);
  "Daralt" altta sabit, liste kaydırılabiliyorsa üstünde sabit bir ayırıcı. Yeterince yüksek pencerede görünüm aynı.
- **Dialog:** kutu pencereden büyümez (kenarlarda en az 24 px); fazla içerik kendi içinde kayar. Risk onayında liste kayar,
  "Riskleri anladım" ve düğmeler her zaman görünür. `--demo-apply=confirm` (40 satır).
Kanıt: `wlcli icon-pack "7TSP Lumicons Symbols.7z" build\lab\iconpack7tsp --source=C:\Windows\SystemResources` (yalnız okuma):
241 simge; imageres 225 grup (1'i bu build'de yok), imagesp1 9, shell32 1, themecpl 1, zipfldr 2 — her yamalı kopyayı Windows
yükleyicisi açıyor, 0 hata; çıkarılan #3 Lumicons klasörü. 311 birim testi / 11.321 doğrulama (.res gidiş-dönüş, bozuk / taşan
girdi, Pack.ini, dönüştürme, tar ile zip, kuyruk + eksik grup + kod içeren hedef). Render: kısa pencerede gezinme ve risk dialogu.
**Görülmeyen:** paketin uygulamanın içinden yüklenip gerçek imaja Uygula'yla yazılması (yönetici) ve kurulan sistemde görünüşü;
Windows 10'un eski `tar.exe`'siyle 7z.

## D-072 — Hakkında sayfası, 1.0 Alpha, GPL-3.0 ve GitHub (2026-10-05)
Bağlam: Kullanıcı: "Hakkında sayfası oluştur; Türkiye bayrağı, benim GitHub'ım, proje GitHub sayfası; tatlı, profesyonel.
Projenin tamamen ücretsiz kalacağını ve sevgiyle yapıldığını yaz. GitHub'a 1.0 Alpha adıyla yükleyip paylaşabilirsin."
Lisans ve depo görünürlüğü soruldu: **GPL-3.0**, **herkese açık `shadesofdeath/WinLove`**.
Karar:
- Hakkında (P17) gezinme çubuğunda (Uygulama ayarları'nın altında; `F1` de açar). Sıra: marka işareti + WinLove + "Sürüm 1.0 Alpha ·
  build · mimari"; **Türk bayrağı** + "Türkiye'de sevgiyle yapıldı"; söz paragrafı (ücretsiz, reklam / hesap / ücretli sürüm yok, veri
  toplamaz, GPL-3.0); **Bağlantılar** (Geliştirici, Proje, Sorun bildir, Lisans — dış bağlantı simgeli sade düğmeler, tarayıcıda açılır;
  yalnız `https://`); **Sistem** (DISM, çalışma dizini, yazı tipleri, üçüncü taraf); Lisanslar / Log klasörü.
- **Tasarımdan sapma:** bayrak renkleri tema belirteci değil, Türk Bayrağı Kanunu'nun renkleri (al `#E30A17`, beyaz): `AboutPage.cpp`
  içinde sabit. Bayrak kanundaki oranlarla Direct2D ile çizilir (2:3; ay dış çemberi 1/2 G'de çap 1/2 G, iç çember 1/16 G ötede çap 2/5 G;
  yıldız çapı 1/4 G, merkezi 0.8208 G, bir ucu uçkurluğa). Bunun için `Canvas` sabit renkli daire ve çokgen dolgusu kazandı.
- Sürüm: `PROJECT_VERSION 1.0.0` (dosya sürümü), görünen etiket `WL_VERSION_LABEL = "1.0 Alpha"` (Hakkında, exe ProductVersion, GitHub).
- `LICENSE` (GPL-3.0 tam metni), `README.md` (Türkçe; özellikler, ekran görüntüleri `docs/screenshots/`, kurulum, söz, derleme).
- Lisanslar dialogu WinLove'un kendi lisansını ve Win11Debloat şablonunu (MIT) da sayar.
- Bileşenler sayfası: imaj bağlanınca başlayan uygulama okuması, o arada hazır gelmiş listeyi (örnek veri / yeni okuma) "okunamadı" ile
  eziyordu; sonuç artık yalnız liste hâlâ "yükleniyor"ken uygulanır.

## D-070 — Windows'un kendiliğinden kurdukları: OneDrive, Outlook, Teams, Dev Home, Telefon, M365, Copilot (2026-10-05)
Bağlam: Kullanıcı: "Ne yaparsak yapalım OneDrive ve Outlook bir şekilde kuruluyor ve masaüstüne geliyor; derinlemesine incele,
kökünden kazı." Ardından: "Bir çok uygulama oradan kuruluyor; yalnız Outlook ve OneDrive için değil, hepsini kapsamlı kaldırsın."
Ölçüm (VMware, 25H2 TR 26200.8037, ağ açık, `tools\lab_vm.ps1 -Network -Diag`: konukta `tools\vm_diag.ps1` specialize
aşamasından ilk oturumdan 30–35 dk sonrasına kadar işlem oluşturma denetimi (komut satırlarıyla), AppX dağıtım günlüğü, 30 sn'de
bir masaüstü / Başlat / Run / zamanlayıcı / uygulama farkı; sonuçlar ikinci bir sanal diske — VHD — yazılır):
| Deney | İmaj | Sonuç |
|---|---|---|
| A | dokunulmamış Pro | OneDrive: ilk oturumda Gezgin, Default profildeki Run kaydını çalıştırıyor (`OneDriveSetup.exe /thfirstsetup`), kullanıcıya kurup kendini güncelliyor. Outlook: imajda 1.0.0.0 **yer tutucu** paket; oturumdan ~15 dk sonra `MoUsoCoreWorker` → `usoclient OutlookUpdate` gerçek Outlook'u indirip **tüm kullanıcılara provision** ediyor. Aynı zamanlayıcı (UScheduler_Oobe): `TFLUpdate` (Teams), `DevHomeUpdate`, `CrossDeviceUpdate`, `EdgeUpdate`, `IA` (Store "iş açısından kritik" güncellemeleri), `LXP` (dil paketleri). Masaüstüne yalnız Edge geldi (yerel hesap). |
| B | Pro + eski `onedrive` / `outlook-install` + Outlook uygulaması | OneDrive ve Outlook **kurulmadı**; ama Başlat'ta ve görev çubuğunda Outlook / M365 **yer tutucu sabitlemeleri** duruyor (tıklanınca Store'dan kurar) |
| C, D | B + Başlat "Boş" + görev çubuğu düzeni (Pro, Home) | **WinLove hatası bulundu:** bileşen tarifi `appx` listesini okuyunca DISM, imajın SOFTWARE hive'ını oturum kapanana dek tutuyor → aynı Uygula'daki sonraki her HKLM yazımı 0x80070020 |
| E, F | yeni grup (7 kanal) + Başlat "Boş" + görev çubuğu düzeni (Pro, Home) | 20/20 adım; **hiçbiri kurulmadı** (OneDrive, Outlook, Teams, Dev Home, Cihazlar Arası, M365; Home'da imajdaki Copilot uygulaması görev çubuğunda — `copilot-app` bundan sonra eklendi); Başlat temiz. Görev çubuğu: `LayoutXMLPath` düzeni varsayılanların **yerine geçmiyor, üstüne ekliyor** (Edge, Store, Outlook yer tutucusu kalıyor) → görev çubuğu işinde çözülecek |
Karar:
- Bileşenler sayfasında yeni grup **"Windows'un Kendiliğinden Kurdukları"**: `onedrive` (taşındı), `outlook-install`, `teams-install`,
  `devhome-install`, `crossdevice-install`, `m365-install`, `copilot-app`. Her kanal (zamanlayıcısı olanlar): `UScheduler_Oobe\<görev>`
  silinir + `UScheduler\<görev>\workCompleted = 1` + imajdaki yer tutucu / hazır paket kaldırılır (`appx`) + `Deprovisioned\<aile>`
  (özellik güncellemeleri geri getirmez). Teams'e ek `ConfigureChatAutoInstall = 0`. Hepsi `always` (her imajda sunulur);
  katalog kuralı `always` girdilerde "yol / paket / sürücü sınıfı" istemez. İçerik listesinde kaldırılan uygulamalar da görünür.
- `EdgeUpdate`, `IA`, `LXP` kanal olarak sunulmaz: yeni uygulama kurmuyorlar (ölçüldü: IA yalnız mevcut uygulamaları kaydetti / güncelledi).
- **Düzeltme (`removeComponent`):** `appx` adımından sonra DISM oturumu kapatılıp yeniden açılır (hive serbest kalır). Gerçek imajda
  doğrulandı: 20 adım, 0 hata (önce 3 hata).
- Laboratuvar: `lab_vm.ps1 -Network` (NAT, `e1000`: bu VMware'de `e1000e` ve `vmxnet3` açılışta çöküyor), `-Diag`, `-FirstLogon`,
  `-ShutdownAfter`; VM hiç açılmazsa artık "PASS" yazmaz.
Kanıt: 306 test / 11.217 doğrulama; altı VM kurulumu (A–F), raporlar `build\lab\out\vm-od-*\diag`. **Görülmeyen:** Microsoft hesabıyla OneDrive'ın
masaüstünü kendi klasörüne taşıması (yerel hesapla ölçüldü; `onedrive-kfm` ayarı var); 35 dk'dan uzun süre / sonraki toplu güncellemeler.

## D-069 — Başlat menüsü sayfası: Windows 11 sabitlemelerini temizleme ve kendi listesi, her sürümde (2026-10-05)
Bağlam: Kullanıcı: "Windows 10'da XML ile Başlat'ı temizliyorduk, Windows 11'de olmuyor; kendimize has profesyonel bir yol
bulalım, NTLite'ta bile yok. Başlat menüsünü kullanıcı istediği gibi özelleştirsin, temizlesin, uygulama sabitlesin; yüklü
sistemde test et." Ardından: "Tüm Windows sürümlerinde çalışsın, sağlam bir altyapısı olsun."
Araştırma (alt ajan): Microsoft'un `ConfigureStartPins` ilkesi (JSON, `applyOnce` 24H2 + KB5062660'tan), Grup İlkesi biçimi
(`Policies\Microsoft\Windows\Explorer`: DWORD + JSON dosya yolu), OEM `LayoutModification.json` (yalnız ekler),
Win11Debloat'ın boş `start2.bin`'i (Default profile). Hepsi VM'de ölçüldü (`tools\lab_vm.ps1`, VMware, 25H2 TR):
| Deney | Sürüm / build | Ne yazıldı | Sonuç |
|---|---|---|---|
| 1 | Pro 26200.8037 | PolicyManager boş liste | Sabitlenenler boş |
| 2 | Pro + KB5129195 (yeni Başlat) | iki ilke biçimi, özel liste, applyOnce | liste birebir, sırasıyla; Önerilenler gizli |
| 3 | Home 26200.8037 | aynı | ilke **yok sayıldı**: Microsoft sabitlemeleri + reklam yer tutucuları |
| 4 | Home + KB5129195 | aynı | liste birebir (güncel build Home'da da okuyor) |
| 5 | Home 26200.8037 | + boş `start2.bin` | Sabitlenenler boş, reklam yok |
| 6 | Home 26200.8037 | + OEM `LayoutModification.json` | OEM sabitlemeleri Microsoft'unkilerin yanına eklendi — kullanılmaz |
| 7 | Home 26200.8037 | boş `start2.bin` + OEM JSON | boş (şablon OEM'i bastırır) |
| 8 | Home + KB5129195 | boş `start2.bin` + özel liste | liste birebir |
| 9 | Pro 26200.8037 | boş `start2.bin` + özel liste | boş (liste uygulanmadı) |
| 10 | Pro 26200.8037 | özel liste, şablonsuz | Microsoft sabitlemeleri + reklam — eski build özel listeyi hiç uygulamıyor |
| 11 | Pro 26200.8037 | şablon + özel liste, `applyOnce` yok | boş |
Karar:
- **Altyapı (her sürüm):** boş Başlat durumu (`start2.bin`, 972 bayt, Win11Debloat MIT — `third_party/win11debloat`,
  koda Base64 gömülü) Default profile yazılır → hiçbir sürümde Microsoft sabitlemesi / reklam yer tutucusu gelmez; üstüne
  ilkenin iki biçimi (PolicyManager JSON + Grup İlkesi DWORD + `%ProgramData%\WinLove\StartPins.json`) listeyi koyar.
  Özel listeyi 26200.9457 (KB5129195) ve sonrası her sürümde (Pro, Home) birebir uyguluyor; 26200.8037 yalnız boş listeyi
  uyguluyor — orada şablon sayesinde Başlat temiz, boş açılır (Microsoft'un reklamları yerine). Eşik `core::startAppliesCustomPins`
  (26100/26200 UBR ≥ 9457); sayfa eski build + "Kendi listem" + kuyrukta güncelleme yoksa uyarır. Windows 10 için boş
  `LayoutModification.xml` ("Boş" modunda).
- `WriteFile` işlemi ikili dosya taşır: değer `base64:…` (`core::imageFileBytes`).
- **Sayfa (P21):** Başlat menüsü — Sabitlenenler (Windows varsayılanı / Boş / Kendi listem, `applyOnce`; imajın
  uygulamaları: paket bildirimleri, Ayarlar, Başlat kısayolları + hedef simgeleri, Edge; Başlat'a benzer önizleme =
  sıralanabilir liste) ve Başlat ayarları (katalogun "start" sekmesi). Eski "Sabitlenmiş uygulamalar ve kutucuklar"
  düğmesi katalogdan kalktı (yerini bu sayfa aldı). `wlcli start-apps`.
Kanıt: 306 birim testi / 11.179 doğrulama; yukarıdaki on bir VM kurulumu (hepsi masaüstüne ulaştı, ekran görüntüleri `build\lab\out\vm-start*`).
**Görülmeyen:** `applyOnce`'ın kullanıcı düzenlemesine izin verdiği (etkileşimli test), uygulamanın içinden liste kurup Uygula.

## D-068 — Simgeler: Windows'un simge dosyalarını yerinde yamalama (D-065'in "yamalanmaz" kararını genişletir) (2026-10-05)
Bağlam: Kullanıcı: "Tek tek .ico eklemek yerine .dll.mun simge dosyalarını toplu okusun, bütün simgeleri listelesin, mun
dosyasının içinden herhangi birini düzenleyebilsin, orijinali yedeklensin, ikon paketi eklenebilsin; Windows boot loop'a
düşmesin, profesyonel özen göster." Ardından: "Yamalamalı olanı da ekleyelim (diğer araçlar gibi)." D-065 bileşen deposu
ve güncellemeler yüzünden yamayı reddetmişti; ölçünce ikisi de çözülebilir çıktı.
Karar:
- **Kendi PE kaynak okuyucu / yazıcımız** (`core/image/icons/PeResources`): UpdateResource / LoadLibrary yok (imaj başka
  mimaride olabilir, dosya bayt bayt korunmalı). Kaynak bölümü dosyanın sonundaysa (bütün `.mun`'lar: x86 PE, `.rdata` +
  `.rsrc`, kod yok) yerinde yeniden yazılır; değilse `.rsrc2` bölümü eklenir. Gömülü imza düşer, sonda başka veri varsa
  reddedilir; SizeOfImage, bölüm boyları, PE sağlama toplamı güncellenir (Windows'un sağlama toplamıyla aynı, testli).
- **Simge grupları** (`IconGroups`): RT_GROUP_ICON ↔ .ico; değiştirme grubun RT_ICON kimliklerini yeniden kullanır, fazlası
  için en büyüğün üstünden yeni kimlik alır, yalnız o grubun kullandığı artıklar silinir; başka grupla paylaşılan simge
  korunur. Kaynak: .ico ya da WIC'in okuduğu her resim → Windows'un kendi yapısı (16–64 px 32 bit bitmap + 256 px PNG).
- **Güvenli yazma** (`IconPatch`), her adım tutmazsa hiçbir şey yazılmaz: yalnız `Windows\` altında, WinSxS / servicing /
  boot / drivers / config hariç, **kod içermeyen** PE'ler (kod içeren dosyanın katalog karması bozulur, Akıllı Uygulama
  Denetimi / WDAC çalıştırmayı engelleyebilir → onlar için yönlendirme sekmesi); temel = dosyanın şimdiki hâli, ilk yamada
  orijinal `Windows\WinLove\IconBackup`'a; yeni bayt bizim ayrıştırıcımızla (simge dışı her kaynak aynı, yeni gruplar
  tam istenen) **ve Windows'un yükleyicisiyle** (`LoadLibraryEx(AS_IMAGE_RESOURCE)` + her görüntüye
  `CreateIconFromResourceEx`) denetlenir; geçici adla, orijinalin güvenlik tanımlayıcısı aynen (`SetKernelObjectSecurity`,
  sahip TrustedInstaller — `SetSecurityInfo` miras bayraklarını yeniden hesaplıyordu, SDDL değişiyordu) yazılıp yeniden
  adlandırılır: yalnız bu ad değişir, WinSxS'teki sabit bağlantı orijinal kalır. `restore-icons.cmd` (WinRE'den de) her
  orijinali geri koyar. Bir güncelleme dosyayı yenilemişse (bağlantı sayısı yine > 1) geri yükleme dokunmaz (eski yedek
  sürümü düşürürdü), yeni yama yedeği güncel orijinalle değiştirir.
- **Kuyruk:** `OpKind::PatchIcons` (dosya başına bir işlem; değer `{"groups": {"#3": kaynak}}` / `{"restore": true}`),
  Ayarlar aşamasında = güncellemelerden sonra. Risk orta.
- **Sayfa:** Simgeler iki sekme: "Sistem simgeleri (dosya yaması)" (dosya listesi + arama, ızgara, değiştir / orijinale
  döndür / .ico kaydet / imajdaki orijinali geri yükle) ve D-065'in yönlendirmesi. Paket klasörü iki türü birden taşıyabilir
  (`<dosya>\<id>.ico` / `iconpack.json` "files"); "Paketi dışa aktar…". `drawFileIcon` resim dosyalarını da çizer.
  Çalışan sistem yamalanmaz (D-021).
Kanıt: 301 birim testi / 11.012 doğrulama (gerçek imageres / shell32 / explorer.exe kopyaları: gidiş-dönüş, büyüme / küçülme, paylaşılan
kimlik, bölüm ekleme, sağlama toplamı, Windows yükleyicisi, PNG'den simge, kuyruk, paketler);
`tools\lab_icons.ps1` (yönetici, 25H2 Pro) **ALL PASSED**: sahip + DACL birebir, WinSxS kopyası orijinal, yedek, geri yükleme
betiği, Windows bütün simgeleri açıyor, üst üste yama, geri yükleme, explorer.exe ve WinSxS reddi, `DISM /ScanHealth` temiz;
`-Lcu` ile: yamadan sonra KB5129195 kuruluyor, ScanHealth yine temiz, bu güncelleme `imageres.dll.mun`'a dokunmadı (yama
kaldı). **VM (`tools\lab_icons_vm.ps1`, VMware 17.6, 2026-10-05 13:13) ALL PASSED:** imageres.dll.mun'da klasör / Bu Bilgisayar / Geri Dönüşüm / sürücü simgeleri yamalı Pro imajı katılımsız kuruldu, ilk oturumda masaüstüne ulaştı (önyükleme döngüsü yok) ve kendini kapattı; ekran görüntüsünde Geri Dönüşüm Kutusu ve Bu Bilgisayar yeni simgeyle. VMware bu diskte NVMe'yi reddetti ("Failed to configure virtual device 'nvme0'") → SATA. **Görülmeyen:** uygulamanın içinden yama + Uygula (gerçek pencerede), güncellemenin dosyayı yenilediği durumda geri yükleme, Akıllı Uygulama Denetimi açıkken .mun yaması.

## D-067 — GitHub tweak seti, Ayarlar sekmeleri yeniden düzenlendi, Kayıt Defteri yalnız özel kayıtlar (2026-10-05)
Bağlam: Kullanıcı: "Tweak sayfasına GitHub'daki en ünlü, güncel tweak'lerden bizde olmayanları seç, bana seçtir." Ardından:
"Yanlış kategoride olanları taşı; Kayıt Defteri sayfasında neden tweak var? Orada yalnız özel kayıt ekleme / düzenleme olsun,
tweak'ler tweak sayfasına."
Karar:
- **Araştırma:** winutil (Chris Titus), Win11Debloat, Winhance, Sophia Script, Optimizer, AtlasOS, ReviOS, xd-AntiSpy,
  schneegans üreticisi kaynak dosyalarından tarandı (2026-10-05). Yalnız kayıt değeri / servis başlangıcı olanlar; plasebo ya da
  güvenilmez olanlar (Max Cached Icons, Psched, PlatformAoAcOverride, Win11'de tepsi simgeleri) alınmadı. 90 aday 21 grupta
  sunuldu, kullanıcı hepsini seçti → **154 yeni satır** (bazı adaylar birden çok satır: ör. dört uygulama izni, üç Windows
  Güvenliği sayfası). Katalog 129 → 283 ayar.
- **Sekmeler 7 → 10:** Gizlilik · Yapay zekâ · Uygulamalar · Performans · Görünüm · Gezgin · Başlat ve görev çubuğu ·
  Güncelleme · Güvenlik · Sistem. Yeni bölümler: Eşitleme ve cihazlar, İnternet iletişimi, Windows yapay zekâ özellikleri,
  Uygulamalarda yapay zekâ, Microsoft uygulamaları, Diğer uygulamalar, Geliştirici, Bakım, Sesler, Gezinti bölmesi ve Bu
  Bilgisayar, Pencereler, Microsoft Defender ve SmartScreen, Hesap denetimi ve oturum, Sistem koruması, Ayarlar uygulaması,
  Aygıtlar ve yazıcılar, Hata ve kurtarma. Yanlış yerdekiler taşındı (ör. Game DVR → Oyun, "Görevi sonlandır" ve saatte saniye →
  Görev çubuğu, Copilot / Recall → Yapay zekâ, BitLocker → Güvenlik, Galeri / Giriş / OneDrive → Gezinti bölmesi, pano
  eşitleme → Eşitleme). Sekmeler 1280 px'te iki dilde de sığıyor.
- **İki ayar aynı değeri yalnız bilerek yazar** (yeni test): dropdown + metin kutusu çiftleri (sanal bellek, DNS, Ayarlar'da
  gizlenen sayfalar) ve Spotlight / kilit ekranı ipuçları. Yoksa birini seçmek ötekinin değerini sessizce kuyruktan alırdı.
  "Ayarlar'da gizlenen sayfalar" bu yüzden önerilenlerde değil.
- **İmajda olmayan servis atlanır** (motor): `SetServiceStart` önce `Services\<ad>` anahtarına bakar (`OfflineRegistry::keyExists`);
  yoksa günlüğe "skipped" yazar, anahtar oluşturmaz (ör. WSAIFabricSvc 24H2 öncesinde yok).
- **Kayıt Defteri (P11) yalnız kullanıcının kendi girdileri:** `tweaks.json`, `TweakCatalog` ve kategori kartları kaldırıldı;
  35 tweak'in hepsinin Ayarlar'da birebir karşılığı vardı (betikle denetlendi), hiçbir şey kaybolmadı. Sayfa tek tablo:
  "Değer ekle…" diyaloğuyla yazılan değerler (anahtar, ad, tür: REG_SZ / EXPAND_SZ / MULTI_SZ / DWORD / QWORD / BINARY /
  değeri sil / anahtarı sil, veri, "kurulumdan sonra yeniden uygula") ve içe aktarılan .reg dosyaları. Onay kutusu / Boşluk
  aç-kapa, Enter / çift tık düzenle, Delete / ✕ kaldır. Metin ↔ değer çevirisi çekirdekte (`core/image/RegistryInput`,
  gidiş-dönüş testli). İçe aktarılan değerler her zaman kurulumdan sonra da yazılır (D-026); elle eklenen değerde seçim
  kullanıcının (varsayılan açık). D-045'in "imajdaki tweak" gösterimi P11'den kalktı, P12'de sürüyor.
Kanıt: 289 birim testi / 10.168 doğrulama; render (`--demo-tweaks` 10 sekme, `--demo-registry`, `--demo-registry=dialog`, 1280 ve 1440, tr / en);
`tools\lab_settings_d067.ps1` (yönetici): katalogdaki **her** ayar varsayılan dışı hâliyle gerçek imaja uygulanır, her değer
motorun okuyucusuyla ve bir örnek reg.exe ile geri okunur. ALL PASSED (2026-10-05 12:11, Pro 26200): 275 ayar → 578 işlem hatasız; 557 / 557 değer motorun okuyucusuyla imajda, her tür ve hive için reg.exe örnekleri (DWORD, SZ, boş SZ, EXPAND_SZ, BINARY, QWORD, varsayılan değer, WOW6432Node, HKCR, UsrClass.dat, `%%Startup` adlı anahtar, anahtar silme), servis başlangıçları, ilk oturum dosyaları; `diagnosticshub.standardcollector.service` 25H2 imajında yok → atlandı, anahtar oluşmadı.
**Görülmeyen:** kurulan sistemde etkiler (VM) — özellikle kurulumda sıfırlanabilenler (Akıllı Uygulama Denetimi, HVCI; ilk
oturumda yeniden yazılıyor), yeni Başlat menüsü değerleri (25H2), Copilot / ajan politikalarının yeni sürümlerde adı.

## D-066 — Uygulamalar: Microsoft Store'dan arayıp indirme (2026-10-05)
Bağlam: Kullanıcı "store.rg-adguard.net'ten verileri aldır" dedi. rg-adguard Cloudflare bot denetimi arkasında (programdan
istek: HTTP 403 "Just a moment…"); bot korumasını aşmak yapılmaz.
Karar: rg-adguard'ın kullandığı kaynaklar doğrudan, yalnız Microsoft uç noktaları (`core/store/MsStore`):
1. arama `storeedgefd.dsx.mp.microsoft.com/v9.0/manifestSearch` (yalnız 12 karakterli Store kimlikleri; winget kimlikleri değil),
2. ürün `displaycatalog.mp.microsoft.com/v7.0/products/<id>` → `WuCategoryId`, paket ailesi,
3. Windows Update FE3 `GetCookie` → `SyncUpdates` (kategori; anonim MSA bileti, Store istemcisinin gönderdiği gövde) → uygulamanın
   ve çerçevelerinin bütün paketleri (`InstallerSpecificIdentifier` = tam ad, `AdditionalDigest` SHA-256),
4. `GetExtendedUpdateInfo2` (…/secured) → dosya adresi (`tlu.dl.delivery.mp.microsoft.com`), indirme, SHA-256 denetimi,
   `<tam ad>.<uzantı>` olarak `<çalışma kökü>\store\<ürün kimliği>`.
Seçim: uygulamanın en yeni paketi (varsa bundle), çerçevelerden imaj mimarisi için en yenisi. **Şifreli paketler**
(`.eappx`, `.emsixbundle` …: Store DRM) lisanssız imaja kurulamaz: seçilmez; uygulamanın yalnız şifreli paketi varsa açık hata.
Uygulamalar'da "Mağazadan ekle…" penceresi (arama, sonuçlar, İndir ve ekle), indirme şeridi + Durdur; bitince uygulama mevcut
`.appx/.msix` yolundan kuyruğa girer (bağımlılıklar yanında bulunur). `wlcli store-search`, `wlcli store-get`.
Kanıt: birim testleri (arama, ürün, SyncUpdates ayrıştırma, seçim, şifreli paket, adres, URL güveni); `tools\lab_store.ps1`
(yönetici, kendim, 2026-10-05 01:22 ALL PASSED: Wikipedia + VCLibs indirildi, SHA-256, imaja provision, listelendi).
**Görülmeyen:** uygulamanın içinden (gerçek pencerede) arama / indirme; kurulan sistemde uygulamanın açılması.

## D-065 — Simgeler: ikon paketi ve tek tek .ico ile Windows simgeleri (2026-10-05)
Bağlam: Kullanıcı "profesyonel bir Windows ikonları yamalama ekranı, ikon paketi yükleyebilme" istedi.
Karar (D-068 dosya yamasını ekledi; bu yönlendirme ikinci sekme olarak kaldı): imageres.dll / shell32.dll **yamalanmaz** (kaynak düzenleme bileşen deposunu bozar, ilk toplu güncelleme orijinalleri geri
koyar). Simgeler `ProgramData\WinLove\Icons\<yuva>.ico`'ya kopyalanır (CopyFile) ve kabuk onlara yönlendirilir: masaüstü
öğeleri (Bu Bilgisayar, kullanıcı klasörü, Ağ, Geri Dönüşüm boş / dolu, Denetim Masası) `HKLM\SOFTWARE\Classes\CLSID\{…}\DefaultIcon`
+ varsayılan profilde ve ilk oturumda `Explorer\CLSID\{…}\DefaultIcon` + `ThemeChangesDesktopIcons = 0` (tema geri koymasın);
Gezgin (klasör, açık klasör, sürücü, çıkarılabilir, CD/DVD, ağ sürücüsü, kısayol oku) `Explorer\Shell Icons`; sistem sürücüsü
`DriveIcons\C\DefaultIcon`. 14 yuva. Paket: .ico klasörü, dosya adı / takma adla eşleşir (TR/EN, büyük-küçük harf, boşluk
önemsiz) ya da `iconpack.json` ("name", "author", "icons": {yuva: dosya}); paket dışına çıkan yol reddedilir. "Kısayol okunu
kaldır" saydam bir .ico üretir. Yeni sayfa **Simgeler** (Kişiselleştirme'nin altı): kartlar, şimdiki → yeni simge önizlemesi
(`Canvas::drawFileIcon`: PrivateExtractIcons → WIC → D2D, imajın kendi imageres.dll'inden), tıkla = .ico seç, Delete = geri al.
Kanıt: birim testleri (yuvalar, takma adlar, paket okuma, manifest, kuyruk işlemleri, ok, sıfırlama); render `--demo-icons`.
**Görülmeyen:** kurulan sistemde simgelerin görünmesi (VM) — özellikle ilk oturum teması ile sıra.

## D-064 — Bu bilgisayarda bağlı imajları tanıma ve onlarla çalışma (2026-10-05)
Bağlam: "Başka Windows dizinlerinde mount edilmiş sistemleri algılasın, NTLite gibi."
Karar: Kaynak sayfasında "Bu bilgisayarda bağlı imajlar" tablosu (yönetici gerekir: DISM): her DISM mount'u (`inspectMounts`),
klasör, sürüm, WIM, durum (hazır / salt okunur / yeniden bağlanacak / bozuk / WIM yok / kayıtsız artık / WinLove'da açık). Çift
tık / Enter: imaj benimsenir (gerekirse remount), kaynağı açılır ve bağlı imaj olur — başlangıçtaki kurtarmayla aynı yol;
uygulama, kaydetme, ayırma o klasörde yapılır. Delete: onayla kaydetmeden ayırma (bozuk olanlar için onarım yolu). Bir imaj
açıkken başka birine geçilmez. Kaynak sayfası her açılışta listeyi yeniden okur. `--demo-mounts`.
Kanıt: render; motor parçaları (`inspectMounts`, `repairMount`, `unmountSafely`) mevcut lab'larla kanıtlı. **Görülmeyen:** başka
araçla bağlanmış imajın uygulamada benimsenmesi.

## D-063 — Defender'ı kökünden kaldırma; .msu'da DISM API → dism.exe yedeği; D-060'ın LCU sonucu düzeltildi (2026-10-05)
Bağlam: Kullanıcı: "Defender'ın sadece veri tabanını siliyor, tamamını silmeyi eklemelisin, risk kullanıcıya ait."
Karar:
- Bileşenler › Güvenlik "Microsoft Defender (tamamen)" (yüksek risk): tanım + Group Policy paketleri, Program Files / ProgramData
  Defender klasörleri, WdBoot / WdFilter / WdNisDrv, servis anahtarları (WinDefend, WdNisSvc, WdNisDrv, WdFilter, WdBoot, Sense),
  sağ tık EPP anahtarları, tepsi Run değeri ve **Windows Güvenliği uygulaması** — tariflere yeni `appx` alanı (her sürüm; DISM
  reddederse yerel kaldırma). Bileşen deposu kopyaları kalır (ScanHealth temiz); not, sonraki güncellemenin dosyaları kısmen geri
  getirebileceğini söyler. Yalnız kapatmak için D-062'deki Ayarlar anahtarı.
- **Ölçüm düzeltmesi:** D-060'ta ve bu kayıtta görülen "kaldırmadan sonra LCU eklenemiyor" hatası ("An error occurred applying the
  Unattend.xml file from the .msu package") kaldırmadan değil: **dokunulmamış** kopyada da aynı (lab_lcu_after T1). DISM günlüğü:
  "Active offline session not registered", `0x800401E3` — bu makinede DISM API (DismAddPackage) UUP tabanlı .msu'yu artık
  kuramıyor; aynı dosyayı `dism.exe` aynı imaja 359 sn'de kurdu. → Applier: .msu API'de 0x800401E3 ile düşerse `dism.exe
  /Add-Package` ile kurulur (ilerleme yüzdesi okunur). D-060'ın "önce güncelleme sonra derin kaldırma" sırası zararsız kaldı.
- **Ölçülen kısıt (dism.exe yedeğiyle):** dokunulmamış kopyaya LCU kuruldu (lab_lcu_after T1, 360 sn); Defender tamamen
  kaldırıldıktan **sonra** aynı LCU `0x800F0982` (PSFX: eşleşen bileşen yok — fark dosyaları silinen tabanı arıyor) ile düşüyor.
  → tarifte `afterUpdates`: Planner onu güncellemelerden sonraki DeepRemove aşamasına koyar; not "bundan sonra toplu güncelleme
  eklenemez, aynı Uygula'da kuyruğa al" der.
Kanıt: `tools\lab_defender.ps1 -WithLcu` (yönetici, kendim, 2026-10-05): kaldırma adımları PASS (dosyalar / servisler / uygulama /
paketler gitti, ScanHealth iki kez temiz), sonradan LCU FAIL 0x800F0982 (beklenen, belgelendi); `tools\lab_lcu_after.ps1 -Cases T1`
yedek yolla INSTALLED. **Görülmeyen:** kurulan sistemde Defender'sız açılış ve Windows Update (VM).

## D-062 — Kurulumun dil listesi, yeni ayarlar (simge boyutu, Denetim Masası, sanal bellek, Defender kapalı), duvar kağıdında Spotlight (2026-10-05)
Bağlam: Kullanıcı istekleri: Setup'ta eklenen dil seçilebilsin (lang.ini); masaüstü simge boyutu; Denetim Masası görüntüleme
ölçütü; sanal bellek boyutu ve sürücüsü; Defender'ı pasife alma; "duvar kağıdı ayarlama başarılı olmadı, Windows'un kendi
Bing duvar kağıdı değişiyor".
Karar:
- **lang.ini:** Uygula, kuyrukta dil paketi ya da arayüz dili varsa commit'ten önce `dism /Gen-LangINI /Distribution:<kurulum
  klasörü>` çalıştırır (`ApplyJobOptions::setupFolder`; klasör `…\sources\install.wim`'in üstü, içinde `sources\lang.ini` varsa).
  Ölçülen: en-US eklenince `en-US = 2`, `tr-TR = 3` (3 = kurulum ortamının dili, boot.wim Türkçe kaldığı için doğru); hata
  ölümcül değil. Kurulum ekranının kendisi (boot.wim / WinPE dil paketi) hâlâ kapsam dışı. `wlcli apply --setup=<klasör>`.
- **Ayarlar kataloğu:** Görünüm › Masaüstü "Masaüstü simge boyutu" (Shell\Bags\1\Desktop IconSize 32/48/96/128, ilk oturum);
  Gezgin › Denetim Masası "Görüntüleme ölçütü" (StartupPage + AllItemsIconView: kategori / büyük / küçük simge);
  Performans › Sanal bellek: açılır menü (Windows yönetsin / C:'de boyutu Windows / kapalı) + metin "Özel sanal bellek"
  (yeni metin biçimi `"format": "pagefile"`: "D: 4096 8192" ya da "D:" → `PagingFiles` REG_MULTI_SZ; yarım yazılan değer
  kuyruğa girmez, satır biçimi söyler; imajdaki değer okunup gösterilir); Sistem › Güvenlik "Microsoft Defender" anahtarı
  (kapalı: WinDefend / WdNisSvc / WdNisDrv / WdFilter / WdBoot / Sense devre dışı, Defender politikaları, tepsi simgesi Run
  değeri silinir, sağ tık taraması Shell Extensions\Blocked; dosyalar imajda kalır, geri açılabilir; yüksek risk).
- **Duvar kağıdı:** 25H2'de yeni kullanıcının teması `aero.theme` (img0.jpg) — biz onu değiştiriyoruz; ama yeni cihazlarda
  masaüstü Windows Spotlight ile başlayabiliyor ve resmin yerine Bing görselleri geliyor. Kişiselleştirme'de duvar kağıdı
  seçilince ilk oturuma `CloudContent\DisableSpotlightCollectionOnDesktop = 1` ve Spotlight masaüstü simgesini gizleyen değer de
  kuyruğa girer; ipucu bunu söyler. Resim kaldırılınca yalnız aynı değerdeki bu iki işlem de çıkar.
Kanıt: birim testleri (pagefile biçimi, REG_MULTI_SZ, katalog satırları, duvar kağıdı işlemleri); `tools\lab_settings_d062.ps1`
(yönetici, kendim, 2026-10-05 00:39 ALL PASSED, 16 işlem geri okundu); lang.ini `lab_languages` B bölümünde + araştırma imajında.
**Görülmeyen:** kurulan sistemde (VM) simge boyutu, Denetim Masası, sanal bellek, Defender'ın gerçekten başlamaması ve
Spotlight'ın masaüstüne gelmemesi; Setup'ın "Yüklenecek dil" listesinde İngilizce.

## D-061 — Diller: Windows Update (UUP) dil dosyaları, build'e göre otomatik bulma, dil odaklı sayfa (2026-10-04)
Bağlam: Kullanıcı 26200.8037'nin en-us dosyalarını uupdump.net'ten elle indirip Diller'den ekledi; Uygula'da hepsi düştü.
İki ayrı neden ölçüldü (kopya imaj, dism.exe ve CBS günlüğü):
1. Tarayıcı yalnız LoF ISO adlarını tanıyordu: dil paketinin kendisi (`…Client-LanguagePack-Package-amd64-en-us.esd`) listeye
   hiç girmedi; express (PSF) meta veri cab'ları (`…-Package-amd64_80a67e0b.cab`, `…-Package.cab`) paket sanıldı → 0x80070002.
2. Dil özellikleri DISM'de yetenek (capability) olarak kuruluyor: CBS cab'ın klasörünü kaynak ekliyor ve bağımlılıkları
   (Speech → Basic + TextToSpeech) **LoF adıyla** (`…-Package~31bf3856ad364e35~amd64~~.cab`) arıyor; UUP adını bulamayınca
   `CBS_E_ONDEMAND_LOCALSOURCE_NOT_FOUND` (0x800F0912), dil paketi kuruluyken bile.
Karar:
- `LanguagePacks` iki adlandırmayı da tanır: dil paketi (LoF .cab / UUP .esd), özellikler, yazı tipleri, **bileşen dilleri**
  (`<Paket>-Package-amd64-en-us.cab` ↔ `<Paket>~31bf…~amd64~en-US~.cab`, "satellite"); express meta veri atlanır.
  `cbsFileName` DISM'in aradığı adı verir; `classifyPackageIdentity` imajın kurulu paketlerini aynı sınıflara çevirir.
- Uygulama (`LanguageInstall`): UUP adlı cab `%TEMP%\WinLove\lang\…` altına LoF adıyla kopyalanıp oradan eklenir; .esd dil
  paketi `WIMApplyImage` ile (ACL'siz) klasöre açılır, DISM klasörü genişletilmiş paket olarak alır. Planner dil
  dosyalarını kurulum sırasına dizer: paket → Temel → yazı tipleri → el yazısı → OCR → metin okuma → konuşma → bileşen
  dilleri (hepsi LCU'dan önce).
- **"Dil ekle…" (otomatik bulma):** imajın build / revizyon / mimarisi → `api.uupdump.net/listid.php` (aynı revizyon, yoksa
  aynı build'in en yeni Insider olmayan sürümü) → `get.php` dosya listesi (SHA-256 + Windows Update adresi). Yalnız dil
  paketi olan diller sunulur (26200.8037 x64: 43). Seçilen dillerin paketi, Temel'i, yazısının yazı tipleri ve isteğe bağlı
  el yazısı / OCR / metin okuma / konuşma / **imajda kurulu bileşenlerin** dilleri (`<çalışma kökü>\languages\<build>`)
  doğrudan Microsoft sunucularından (yalnız *.microsoft.com / *.windowsupdate.com, kullanıcı-bilgisi hilesi reddedilir)
  indirilir, SHA-256 denetlenir ve DISM'in istediği adla kaydedilir; önceden indirilmiş doğru dosya yeniden indirilmez.
  uupdump.net yalnız listeyi verir; baytlar Microsoft'tan gelir. Liste oturum boyunca build başına önbellekte.
- **Sayfa:** dil başına bir satır (Dil · Durum · Özellikler · Bileşen dilleri · Boyut); imajdakiler ikincil, eklenecekler
  vurgu renginde; Delete bir dili tümüyle (yalnız ona gereken yazı tipleriyle) kuyruktan çıkarır, arayüz dili onu
  gösteriyorsa sıfırlanır. İndirme sırasında listenin üstünde şerit + "Durdur" (.part dosyaları devam için kalır). İmajda
  toplu güncelleme varken dil kuyruklanırsa uyarı + "Güncellemeleri bul" (Microsoft: sonradan eklenen dil, toplu güncelleme
  yeniden kurulana dek temel sürümde kalır). Klasörden eklemede seçilenin bağımlılıkları kendiliğinden eklenir.
- `wlcli uup-languages <build> [--lang= --parts= --packages-of=<mount> --download=]`.
Kapsam dışı (şimdilik): LXP (yalnız appx'i olan 45 kısmi dil), boot.wim / WinRE / kurulum ekranı dili (WinPE dil
paketleri UUP'ta yok), ISO'daki `lang.ini`.
Kanıt: birim testleri (UUP / LoF adları, express ayıklama, standart ad, bağımlılık, yazı tipi, plan sırası, uupdump JSON,
URL güveni; sayfa denetleyicisi: satırlar, seçim, kuyruktan çıkarma); render `--demo-languages[=dialog|fetch]`;
`tools\lab_languages.ps1` (yönetici, kendim, 2026-10-04 13:00 ALL PASSED): A) kullanıcının UUP adlı klasörü → 22/22 adım, en-US kurulu ve arayüz dili;
B) `uup-languages` ile Home imajına göre otomatik indirme (32 dosya, adlar DISM'in istediği gibi) → 33/33 adım.
**Görülmeyen:** uygulamanın içinden indirme (gerçek pencerede ağ + dialog akışı), commit sonrası kurulan sistemde İngilizce
arayüz (VM), dil sonrası LCU'nun yeniden kurulması.

## D-060 — Derin kaldırma: eski donanım sürücüleri, bileşen deposu tutarlı (2026-10-02)
Bağlam: Kullanıcı NTLite'taki gibi derin kaldırmayı istedi ("uyarılarını da kullanıcıya söylemeliyiz"). D-059'daki ölçüm:
sürücü yükünü WinSxS'ten silmek depoyu bozuyordu ("repairable").
Araştırma (25H2 Pro, kopya imaj): her Windows'la gelen sürücü kendi deployment'ı (`dual_<inf>_…`); onu çekirdek paketin bir
update'i kuruyor. Sürücü veritabanı ikiye bölünmüş: çoğu DRIVERS hive'ında, önyüklenebilir sınıflar (disket, FDC, PCMCIA,
1394, SBP2) SYSTEM\DriverDatabase'de.
Karar:
- **Motor** (`core/image/DeepRemoval`): bir sürücü paketi her yerden birlikte çıkar — COMPONENTS (bileşen + deployment),
  WinSxS yükü + manifest, DriverStore\FileRepository + `<kültür>\<inf>_loc`, iki DriverDatabase (DriverPackages,
  DriverInfFiles, DeviceIds değerleri, yalnız bize ait DriverFiles), `Windows\INF\<inf>` + `.pnf`, yük dosyalarının diğer
  hard link'leri (System32\drivers\*.sys) ve onları çalıştıran servis anahtarları. Böylece depo tutarlı: `/ScanHealth` temiz.
- **Yalnız sabit bir eski sınıf listesi** (modem, teyp, ortam değiştirici, disket + FDC, 1394 / 61883 / AVC / SBP2, PCMCIA,
  barkod / OPOS / uzak POS); başka sınıf preset'le bile istenemez (`isDeepRemovableClass`, doğrulamada ve kaldırmada).
  Sınıf tanım INF'leri (`c_*.inf`) kalır. Tarif `driverClasses` (GUID) taşır; sürücüler çalışma anında imajın kendi
  veritabanından bulunur (her Windows sürümünde).
- **Katalog:** "Eski Donanım Sürücüleri (derin kaldırma)" grubu, 6 girdi, hepsi yüksek risk, `deep: true`. Sayfada derin
  girdi seçilince kırmızı uyarı; her girdinin notu güncelleme kısıtını anlatır.
- **Toplu güncelleme kısıtı (ölçüldü):** derin kaldırılmış imaja KB5129195 (26200.9457) eklenemedi — "An error occurred
  applying the Unattend.xml file from the .msu package" 0x80070002; dokunulmamış kopyaya aynı .msu 6 dakikada kuruldu.
  Önce güncelleme sonra derin kaldırma: ALL PASSED, ScanHealth iki adımda da temiz. → Planner'da yeni **DeepRemove**
  aşaması güncellemelerden (ve Apps'ten) sonra; derin olmayan bileşen kaldırmalar eski yerinde (Remove).
- Görülmeyen: kurulan sistemde Windows Update'in toplu güncellemesi (VM); notlar "başarısız olabilir" der.

## D-059 — Bileşenler: paket düzeyinde bileşenler, bileşen deposu taraması (2026-10-02)
Bağlam: Kullanıcı Bileşenler sayfasının NTLite'a göre çok az şey sunduğunu söyledi ("gerçekten iyi bir tarama
yaparak bileşen deposunu genişletmeliyiz"). Sayfada uygulamaların dışında 6 sistem bileşeni vardı; D-031'de 865 gizli
paket "sonucu öngörülemez" diye dışarıda bırakılmıştı.
Tarama (25H2 TR Pro, `tools\lab_scan_components.ps1` + `tools\analyze_cbs.py`): 1227 nötr paket ailesi, 928 kurulu, 860'ı
gizli; COMPONENTS hive'ından bileşen → deployment → paket sahipliği; WinSxS boyutları. Her paket için yalnız onun
ağacına ait bayt ("tek sahipli") hesaplandı.
Karar:
- **Yalnız gerçekten isteğe bağlı olanlar** (kullanıcı: "wifi ethernet sürücüleri falan gerçekten gerekli bileşen,
  ekleme"): 29 yeni girdi, 6 yeni grup — Gizlilik (telemetri), Güvenlik (Defender hazır tanımları, Application Guard),
  Multimedya (Fotoğraf Görüntüleyici, DLNA, Miracast alıcısı, WMP paylaşımı, Play To, Windows Sonic, 3D ekran
  koruyucular), Yazı Tipleri (Japonca, Basit / Geleneksel Çince, Korece), Kurumsal (App-V, UE-V, BranchCache, Kiosk,
  FCI, İş Klasörleri, Çevrimdışı Dosyalar, RemoteApp, Uzaktan Yardım, Sınav), Diğer (Edge DevTools istemcisi, POS,
  kurtarma diski oluşturucu, kurulum yardımı, biyometrik kayıt). **Bilerek yok:** MTP (telefon / kamera), BitLocker,
  gpedit, ağ / depolama sürücüleri, varsayılan duvar kağıtları (Kişiselleştirme aynı dosyaya yazar). Birim testi
  katalogda Wi-Fi / Ethernet / WPD / SecureStartup / Storage paketi olmadığını denetler.
- Tarif yalnız `packages` taşıyabilir (yol şartı kalktı). Kaldırma eski yoldan: hive'da kilit açılır, DismRemovePackage;
  bir üst paketle giden alt paket "zaten gitmiş" sayılır (DISM'e sorulur).
- **Var mı / boyut çalışma anında** (`core::ComponentStoreIndex`, offreg + dizin listesi, hive yüklenmez, ~4 sn):
  kurulu paket aileleri (SOFTWARE), paket ağacı (.mum), sahiplik (COMPONENTS: `c!` → deployment → `i!CBS_` →
  paket; toplu güncellemeler sahip sayılmaz), WinSxS klasör boyutları. Boyut = girdinin paket ağaçlarının tek
  sahipli baytları (yolları da varsa ikisinden büyüğü).
- Kanıt: `tools\lab_cbs_removal.ps1` (yönetici, kendim) — 33 aday tek tek kaldırıldı, ScanHealth "bozulma yok",
  export 6637 → 5949 MB (−688 MB); gönderilen katalogla (30 girdi, OneDrive dahil) yeniden: ALL PASSED, ScanHealth temiz,
  6637 → 5793 MB (−844 MB). Çalışma anı boyutları Python analiziyle bayt bayt aynı. Görülmeyen: bu imaja sonradan
  toplu güncelleme eklenmesi, kurulan sistemde etki (VM).
- **Windows'la gelen sürücüler: yapılmadı (2026-10-02, ölçülerek).** Eski sınıfların sürücüleri (yazıcı 53, modem 28,
  diğerleri ~5 MB) WinSxS'te `amd64_dual_<inf>` bileşenleri ve hepsinin sahibi çekirdek paketler
  (Client-Desktop-Required-Package011120 / 0111, Common-DriverClasses-Core): paket düzeyinde kaldırılamaz. FileRepository
  dosyaları WinSxS'e hard link — yalnız onları silmek ISO'yu küçültmez. Deney: 153 modem sürücüsünün WinSxS yükü
  silinince `/ScanHealth` "The component store is repairable" (önce: bozulma yok). Bozuk depo toplu güncellemeyi
  kırabilir, RestoreHealth sürücüleri geri getirir. ~85 MB için Windows Update'i riske atmak yok; ancak manifest ve
  kayıtları da tutarlı biçimde söken bir "derin kaldırma" (NTLite tarzı) yeni bir karar ve VM'de güncelleme testi ister.

## D-058 — Kaynak ve İmajlar araçları: SHA-256, arama, sıkıştırma, SWM, çoğaltma, sürüm ekleme, yakalama (2026-10-01)
Bağlam: Kullanıcı Kaynak / İmajlar sayfalarının NTLite'a göre eksiklerinden 1, 2, 3, 4, 5, 6, 7, 9'u seçti ("profesyonelce").
Karar:
- **Kaynak:** birden çok dosya bırakılınca ilki açılır, diğerleri okuyucu iş parçacığında okunup son kullanılanlara
  eklenir (`AppState::rememberSource`). Son kullanılanlar sağ tık → **SHA-256 doğrula…**: canlı yüzde, Microsoft'un
  değerini yapıştırma kutusu (büyük harf, "SHA256:" öneki, boşluklar kabul — `core::normalizeSha256`), eşleşiyor /
  eşleşmiyor, kopyala. Hesap `core::sha256File` (CNG), dialog kapanınca ya da uygulama kapanırken iptal.
  Başlık eylemi **Klasörden imaj oluştur…** (`WIMCaptureImage`, yönetici; değilse yükseltilmiş yeniden başlatma):
  klasör → .wim (varsa yeni sürüm olarak eklenir), ad / açıklama / LZX-XPRESS.
- **İmajlar:** birden fazla sürüm varsa arama kutusu (`/` odaklar; ad, görünen ad, sürüm kimliği, açıklama, index) +
  mimari filtresi (`EditionFilter`). Başlıkta **Araçlar** menüsü: *Sıkıştırmayı değiştir…* (LZX / XPRESS / yok / ESD;
  her sürüm yeni dosyaya export, önyükleme index'i korunur, uzantı .wim ↔ .esd değişir), *SWM'e böl…* (3800 / 2000 /
  1000 / 650 MB) ya da parçalı kaynakta *SWM → WIM…*, *Başka imajdan sürüm ekle…* (ISO / WIM / ESD / SWM; sürümler
  onay kutularıyla, `WIM_EXPORT_ALLOW_DUPLICATES`, ortak akışlar bir kez). Satır menüsünde **Çoğalt…** (aynı sürüm
  yeni adla sona; dosyalar ortak, birkaç MB).
- ISO kaynakta yazan araçlar önce çalışma klasörüne kopyalar (diğer düzenlemeler gibi); bağlıyken / meşgulken
  hiçbiri çalışmaz (`toolsRefusal`). ESD bölünmez, parçalı imajda yalnız "SWM → WIM" çalışır.
- Motor önce `wlcli` ile: `hash`, `recompress`, `swm-split`, `swm-merge`, `duplicate`, `append`, `capture`;
  `tools\lab_imagetools.ps1` gerçek imajda.

## D-057 — WLM (WinLove Method) sıkıştırma araştırması; ESD yazma hatası düzeltildi (2026-10-01)
Bağlam: Kullanıcı LZMS'den güçlü, uygulamaya özel bir sıkıştırma ("WinLove Method") istedi.
Bulgular (25H2 TR Pro, yönetici gerekmeden ölçüldü, `build\lab\compress\results.txt`):
- **Hata:** `exportImage(…, Lzms)` sıkıştırmasız dosya yazıyordu (13,0 GB); wimgapi'ye belgelenmemiş
  `0x20000000` (solid) bayrağı gerekiyor. Düzeltildi + başlık denetimi; ISO sayfasının "ESD" yeniden
  paketlemesi bundan etkileniyordu. Gerçek ESD: 4,872 GiB (5,23 GB), 657 sn; tek solid kaynak, **64 MiB'lık
  bağımsız LZMS parçaları**. Ekleme (ikinci sürüm) çalışıyor: +51 MB.
- **İçerik:** ham verinin %48,6'sı x64 PE, %14,3'ü x86 PE, %3,2 + %2,3 kaynak-yalnız PE (MUI), 646 MiB gömülü
  WIM (WinRE.wim, LZX: ESD küçültemez). WinRE açılınca 1.585 MiB; %24,3'ü ana imajdaki dosyalarla bayt bayt aynı.
- `cabinet.dll` LZMS'i wimgapi'nin ESD kodlayıcısından %26,5 kötü (aynı veri) → referans her zaman gerçek ESD.
- **WLM v0** (içerik gruplama + WinRE açma + x86/x64'te BCJ2 + LZMA2 1 GiB sözlük): **4,245 GiB (4,56 GB),
  ESD'den −%12,9**. Gruplar: x64 7,12→1,62 GiB, x86 1,92→0,39, kaynak 0,79→0,38, diğer 3,71→1,85 (0,50).
  Sıkıştırma ~66 dk (7-Zip, 2 iş parçacığı); açma süresi ölçülmedi.
**WLM v1 (2026-10-01 akşam, kullanıcı: "önerdiğin sırayla devam et"):** gerçek biçim + kendi kodlayıcı / çözücümüz (`core/wlm`, LZMA SDK 26.02 — kamu malı — `third_party/lzma`). Dosya: 512 baytlık başlık (özgün WIM başlığı içinde), gruplar bağımsız bloklar hâlinde (varsayılan 1 GiB = sözlük; kodda x86 dal filtresi + LZMA2), sonda LZMA2'li tablo (arama tablosu özgün sırayla: bayrak, başvuru sayısı, SHA-1, boyut, grup içi konum; XML olduğu gibi). Açma: bloklar paralel çözülür, akışlar arka arkaya düz bir WIM'e yazılır, her SHA-1 denetlenir; arama tablosu özgün sırayla, önyükleme metadata'sı / bayraklar korunur. v1'de WinRE açılmaz (dış imajın metadata'sını yeniden yazmak gerekir). **Ölçüm (WinRE.wim, 1,66 GB ham):** LZX 674 MB · ESD 430 MB · WLM 64 MiB blok 444 MB (+%3) · **WLM 1 GiB blok 378 MB (−%12,1)**; açma 6 sn. **Kanıt:** birim testleri (sentetik WIM gidiş-dönüş, bozuk blok); wimgapi açılan WIM'i LZX'e aktarıyor; `tools\lab_wlm.ps1` (yönetici) WinRE.wlm ALL PASSED — DISM `/Get-WimInfo` ve salt okunur bağlama. Uygulama: Kaynak `.wlm` açar (sürümler tablodaki XML'den), İmajlar "WLM → WIM" ve "WLM'e paketle…"; ISO sayfasına WLM bilerek eklenmedi (kurulum WLM okuyamaz, kurucu yok). 
**WLM v1 tam Pro (25H2, 13,95 GB ham):** 5,178 GB — ESD 5,232 GB'den yalnız **−%1,0** (v0 −%12,9). Neden: WinRE.wim (674 MB LZX) v1'de olduğu gibi duruyor (~%7), x64 grubu 7 bağımsız 1 GiB blok (oran 0,250; v0'da tek akış 0,228), BCJ (v0: BCJ2). Paketleme 53 dk (tek iş parçacığı, ~12 GB), açma 62 sn (92.788 SHA-1). `lab_wlm.ps1 -Wlm pro.wlm` ALL PASSED: DISM bağladı, sürüm Professional, wimgapi LZX'e aktarınca özgün dosyanın boyutu (6,482 GB). v2 için: WinRE'yi açıp yeniden kurma (açılışta wimgapi ile LZX + dış metadata'daki SHA-1'in güncellenmesi), BCJ2, blok başına bir önceki bloğun sözlüğü. 
**WLM v2 tam Pro:** grup başına tek LZMA2 akışı (1 GiB pencere) + WinRE açılıp yeniden kuruluyor (6.216 akış ortak) → **4,668 GB, ESD'den −%10,8**. Paketleme 70 dk (~12 GB bellek), açma 163 sn. `lab_wlm.ps1` ALL PASSED: DISM bağladı, Professional, yeniden kurulan Winre.wim doğrulandı, önyükleme indeksi 1, LZX, 637 MB. 
**Kaldırıldı (2026-10-01, kullanıcı: "gerek yok, WLM sil"):** WLM kodu, LZMA SDK, testleri, `lab_wlm.ps1`, uygulamadaki `.wlm` desteği ve `dump-streams` çıkarıldı; ölçümler bu kayıtta kalır. Kalan: ESD yazma düzeltmesi (`0x20000000`). 
Karar: WLM henüz ürün özelliği değil. Kurulum WLM'i okuyamaz; yol: boot.wim'de kendi kurucumuz
(`winpeshl.ini` → wlsetup: disk düzeni, WLM'den sanal WIM ile `WIMApplyImage` — `WIMInitFileIOCallbacks`
denenmedi —, `bcdboot`, Panther\unattend.xml). Yalnız temiz kurulum. Sıradaki adımlar: açma hızı, "diğer"
grubun iyileştirilmesi, `WIMInitFileIOCallbacks` deneyi, VM'de prototip.

## D-056 — Kişiselleştirme: OEM, varsayılan görseller, yazı tipleri; Wi-Fi; Compact OS; boot.wim sürücüleri (2026-10-01)
Bağlam: Kullanıcı NTLite'a göre eksik kolay özelliklerden 1, 2, 3, 4, 5, 6, 8'i seçti. 2 (WinRE kaldırma, D-031)
ve OEM metin alanları (Ayarlar › OEM bilgisi, D-041) zaten vardı — gözden kaçırılmıştı.
Karar:
- **Yeni sayfa Kişiselleştirme** (Hosts'un altı): OEM alanları (Ayarlar'dakiyle aynı işlemler, iki görünüm) + logo,
  masaüstü / kilit ekranı / hesap resmi, yazı tipleri. Hepsi kuyruk işlemi → presetlere girer.
- **Görseller Windows'un kendi varsayılanlarının yerine yazılır** (`SetPicture`): her hedef dosya imajdaki boyutu ve
  biçimiyle yeniden üretilir (WIC, ortalanmış kırpma). 25H2: img0 / img19 (açık / koyu) + 1920×1200 eşleri,
  Screen\img100.jpg (+ PersonalizationCSP değerleri), user*.png / user.bmp, System32\oemlogo.bmp (120×120) +
  OEMInformation\Logo. Ayarlar'daki eski "Duvar kağıdı" ve "Kilit ekranı resmi" (ProgramData kopyası + HKCU yolu,
  D-041) kaldırıldı: Windows 11 ilk oturumda temayı uygularken ezebiliyordu ve iki yol çakışırdı.
- **Sabit bağlantılar:** Windows'un dosyalarının çoğu WinSxS'e hard link; yerinde yazmak bileşen deposunun kopyasını
  da değiştirir. `replaceImageFile` önce bağı koparır (yalnız bu ad), sonra yeni dosyayı SeRestorePrivilege + backup
  semantics ile yazar (TrustedInstaller ACL'si). `writeImageFile` / `copyImageFile` da artık bundan geçer.
- **Yazı tipi** (`AddFont`): Windows\Fonts + Fonts değeri, ad dosyanın 'name' tablosundan ("Segoe UI (TrueType)",
  koleksiyon "Cambria & Cambria Math (TrueType)", CFF "(OpenType)").
- **Wi-Fi:** Kurulum Sonrası adım türü; SYSTEM olarak `netsh wlan add profile … user=all`, profil dosyası hemen silinir.
  Elle (SSID, parola, WPA2 / WPA3 / açık, gizli) ya da bu bilgisayardan (wlanapi; anahtar yönetici olarak okunur).
  Parola presette ve medyada açık metin — dialog söylüyor.
- **Compact OS:** yanıt dosyasında windowsPE `ImageInstall\OSImage\Compact`.
- **boot.wim sürücüleri:** Sürücüler › "Kurulum ortamı (boot.wim)" sekmesi; aynı tarama ağacı, seçim `AppState`'te,
  `IsoController::bootPatch` → mevcut `patchBootImage`. Presetlere `bootDrivers` olarak girer.
Kanıt: **`tools\lab_branding.ps1` ALL PASSED (yönetici, sessiz UAC, 2026-10-01)** — 11 görsel özgün boyutunda,
her biri tek bağlantı; WinSxS eşi değişmedi; logo 120×120; WinRE kaldırıldı; yazı tipi + kayıt değerleri imajda;
Wi-Fi betiği; DISM `/ScanHealth` "No component store corruption" (117 sn). Birim testleri (5 yeni), render'lar.
**Görülmeyen:** kurulan sistemde etki (VM), boot.wim'e sürücü eklemenin bu seferki çalıştırması (`tools\lab_boot.ps1 -Driver` bunu
sınıyor; bu turda çalıştırılmadı).

## D-055 — Kuyruğu WIM'in birden çok sürümüne uygulama (2026-10-01)
Bağlam: Kullanıcı (ikinci özellik turu, 15. madde) aynı kuyruğun / presetin bir WIM'deki birkaç sürüme uygulanmasını istedi.
Karar:
- **Sırayla, aynı klasörde:** Uygula özetinde "Diğer sürümlere de uygula" satırı — bağlı WIM'in diğer sürümleri
  onay kutusu olarak. Önce bağlı sürüm her zamanki gibi uygulanır ve commit edilir; **yalnız o kaydedildiyse** her
  seçili sürüm aynı bağlama klasörüne okuma-yazma bağlanır, aynı plan çalışır, commit + unmount. `optimizeWim`
  (commit artıklarını silen yeniden yazma) bir kez, en sonda. Paralel bağlama yok: disk ve DISM oturumu tek.
- **Sürüm değişikliği (`SetEdition`) diğer sürümlere gitmez** (`planForOtherEdition`): o, kuyruğa alındığı sürüme
  aittir. Diğer her işlem "atla ve raporla" ile çalışır; bir sürümde olmayan uygulama `0x80070002` → başarı (saha notu).
- **Hata:** diğer sürümün commit'i başarısızsa o sürüm discard edilir (klasör sonraki için boşalır), sonuç satırında
  "kaydedilmedi". İptal kalan sürümleri atlar. Çalışırken "Sürüm k / n · ad", bitince bilgi bandında sürüm başına sonuç.
- **CLI:** `wlcli apply <cs> <mount> --commit --also=2,3 --wim=<dosya>`.
Kanıt: birim testi (`planForOtherEdition`), render (`--demo-editions`). **Gerçek imajda kanıtlandı (kullanıcı, yönetici, 2026-10-01 11:51, `tools\lab_features.ps1`): ALL PASSED** — iki sürümlük
lab WIM'inde (Home + Pro) üç işlemlik kuyruk sürüm 1'de commit (144 sn), ardından sürüm 2'ye bağla + uygula + commit +
tek `optimizeWim`, toplam 340 sn; sürüm 2 salt okunur bağlanınca tasks.cmd, SetupComplete çağrısı, hosts bölümü ve
kopyalanan dosya oradaydı; sürüm 1'deki değişiklikler (ve önceden provision edilen Terminal) korundu; WIM 2 sürüm.
**Görülmeyen:** uygulamanın içinden (Uygula sayfası) çalıştırma, ikinci sürümde hata / iptal yolu.

## D-054 — Varsayılan uygulama ilişkilendirmeleri (2026-10-01)
Karar: Uygulamalar sayfasının ikinci sekmesi. Liste kullanıcının seçimi + içe aktarılan XML (`dism /Export-DefaultAppAssociations`
biçimi) + "Bu bilgisayardakini al" (`dism /Online /Export-…`, yönetici) + hazır tarayıcı düğmeleri (Chrome / Firefox / Brave / Edge:
http, https, .htm, .html, .pdf'nin ProgId'leri). Kuyrukta tek `SetDefaultApps` işlemi (değer = XML); Uygula XML'i
`%TEMP%\WinLove`'a yazar ve `dism /Image /Import-DefaultAppAssociations` çalıştırır. Yalnız yeni kullanıcılar için
geçerli (Windows'un kuralı); ProgId'nin sahibi uygulama kurulu değilse Windows ilk açılışta sorar — sayfada yazıyor.
Kanıt: XML ayrıştırma / yazma birim testli. **Gerçek imajda kanıtlandı (kullanıcı, yönetici, 2026-10-01 11:51, `tools\lab_features.ps1`): ALL PASSED**: 2 ilişkilendirme 4 sn'de içe aktarıldı,
imajda `Windows\System32\OEMDefaultAssociations.xml` var. **Görülmeyen:** kurulumda etkisi (yeni kullanıcının varsayılanları).

## D-053 — Dil paketleri ve bölge ayarları (2026-10-01)
Karar:
- **Yeni sayfa "Diller"** (Güncellemeler'in altında). Üstte imajın dilleri ve beş ayar (`dism /Get-Intl`): arayüz dili,
  sistem yereli, kullanıcı yereli, klavye, saat dilimi. Değişiklik tek `SetIntl` işlemi (JSON) → `dism /Set-UILang
  /Set-SysLocale /Set-UserLocale /Set-InputLocale /Set-TimeZone`. Seçenek listeleri bu bilgisayardan (yerel adları
  `EnumSystemLocalesEx`, klavye düzenleri `Keyboard Layouts`, saat dilimleri `Time Zones`), arama destekli açılır menü.
- **Dil paketleri:** "Klasör tara" Microsoft'un *Languages and Optional Features* medyasını dolaşır; dosya adından
  tür (Client-Language-Pack, LanguageFeatures-Basic/Fonts/Handwriting/OCR/Speech/TextToSpeech, LXP), dil, mimari
  (`classifyLanguageFile`). İmajın mimarisine uymayanlar gösterilmez. Kuyruğa `AddPackage` + değer `language`
  (Planner'da güncellemelerden önce: SSU 0, dil 1, LCU 2 — Microsoft'un sırası: dil paketi LCU'dan önce).
- Güncellemeler sayfası `language` değerli paketleri göstermez (iki sayfa aynı işlemi saymasın).
Kanıt: sınıflandırma ve JSON / argüman birim testli, render. **Gerçek imajda kanıtlandı (kullanıcı, yönetici, 2026-10-01 11:51, `tools\lab_features.ps1`): ALL PASSED**: `/Get-Intl` (UI tr-TR, Turkey
Standard Time, diller tr-TR) ayrıştırıldı; `/Set-InputLocale:0409:00000409 /Set-TimeZone:"GMT Standard Time"` 6 sn,
geri okumada ikisi de yeni değerde. **Görülmeyen:** gerçek dil paketi eklemesi (medya yok: `-LanguageFolder`),
`/Set-UILang` / yerel değişikliği ve kurulumda etkisi.

## D-052 — İmajdaki sürücüler: listele, kaldır; bu bilgisayarın sürücülerini al (2026-10-01)
Karar: Sürücüler sayfasına ikinci sekme "İmajdaki sürücüler": `DismGetDrivers(AllDrivers = FALSE)` → yalnız üçüncü
taraf (oemN.inf) sürücüler; sınıf, sağlayıcı, sürüm, tarih, imzalı mı, önyükleme için kritik mi. Kaldırma `RemoveDriver`
işlemi (`DismRemoveDriver`); önyükleme için kritik olan yüksek risk. `0x80070002` (zaten yok) başarı. Kutudan çıkan
sürücüler listelenmez: kaldırılmaları desteklenmiyor. "Bu bilgisayarın sürücüleri" `pnputil /export-driver * <klasör>`
(yönetici) → çalışma kökünde `host-drivers`, ardından mevcut klasör ekleme akışı.
Kanıt: `DriverPackage` düzeni ADK başlığıyla derleme anında karşılaştırıldı (bulunan hata: alan `PCWSTR ProviderName`);
birim / render. **Gerçek imajda kanıtlandı (kullanıcı, yönetici, 2026-10-01 11:51, `tools\lab_features.ps1`): ALL PASSED**: bu bilgisayardan `pnputil` 610 paket 35 sn; Home imajında üçüncü taraf sürücü yoktu,
eklenen `realtekhsa.inf` 1,4 sn'de `oem0.inf` olarak listelendi, `DismRemoveDriver` 2 sn'de kaldırdı (liste yine boş).
**Görülmeyen:** önyükleme için kritik sürücü kaldırma, uygulamanın içinden.

## D-051 — Dosyalar sayfası: bilgisayardan imaja dosya ve klasör (2026-10-01)
Karar: "Kurulum Sonrası"nın altında yeni sayfa. Sürükle-bırak veya seç → "İmajda nereye?" dialogu (hazır yerler:
kök, `Windows\Setup\Scripts`, Default kullanıcının masaüstü / Belgeler, Public masaüstü, `Windows\Fonts`, özel yol).
Her öğe tek `CopyTree` işlemi (hedef = imaj içi göreli yol, değer = kaynak). Uygula'da kopyalanır (klasör: içeriği
birleştirilir, dosyalar değiştirilir), bayt ilerlemesi, iptal edilebilir. **Yasak hedefler** (`validateTreeTarget`):
`Windows\System32\config`, `WinSxS`, `servicing`, `Program Files\WindowsApps`, `Windows\System32\drivers`, `Boot`,
`System Volume Information`, `$Recycle.Bin` ve bunların üstleri; `Windows` / `System32` altı "yüksek risk". Kaynak
Uygula anında okunur (kuyrukta yalnız yol) — sayfa bunu söyler.
Kanıt: birim testleri (hedef denetimi, kopya, boyut), render. **Gerçek imajda kanıtlandı (kullanıcı, yönetici, 2026-10-01 11:51, `tools\lab_features.ps1`): ALL PASSED**: `Tools\payload` kopyalandı, commit
sonrası iki sürümde de `Tools\payload\readme.txt` var.

## D-050 — Uygulama yükleme (.appx / .msix, bundle): çevrimdışı provision (2026-10-01)
Karar: Yeni sayfa "Uygulamalar" (Bileşenler'in altında). Paket seçilince manifest AppxPackaging COM API'siyle okunur
(ad, yayıncı, sürüm, mimariler, framework mü); dosya adı yanıltıcı olabilir (winget `.msixbundle`'ı `.msix` diye
kaydediyor) → okuyucu diğer türü de dener. **Bağımlılıklar** aynı klasörde kimlik + mimari uyumuna göre bulunur
(`planAppxInstall`), lisans `*License*.xml`; bulunamayan bağımlılık uyarı. Kuyrukta tek `AddAppx` (değer = JSON);
Uygula: `dism /Image /Add-ProvisionedAppxPackage /PackagePath /DependencyPackagePath… /LicensePath | /SkipLicense
/Region:all`. Planner'da yeni faz **Apps** (güncellemelerden sonra, temizlikten önce).
Kanıt: gerçek Windows Terminal paketi (winget, `build\lab\appx`) — manifest, bundle yedeği, VCLibs / UI.Xaml
bağımlılığı bulundu (`wlcli appx-info`, yönetici gerekmez). **Gerçek imajda kanıtlandı (kullanıcı, yönetici, 2026-10-01 11:51, `tools\lab_features.ps1`): ALL PASSED**: Terminal bundle'ı (`Dependencies\`
alt klasöründeki UI.Xaml 2.8 ile, `/SkipLicense /Region:all`) 8 sn'de provision edildi, DISM listeliyor, commit sonrası
`Program Files\WindowsApps\Microsoft.WindowsTerminal*` var. **Görülmeyen:** lisanslı (Store) paket, yeni kullanıcıda
uygulamanın açılması.

## D-049 — Hosts ve DNS (2026-10-01)
Karar:
- **Hosts:** yeni sayfa (Ayarlar / Tweaks'in altında). Hazır listeler (`resources/catalog/hosts.json`: telemetri,
  reklam, Copilot) + dosyadan / metinden özel girdiler. Her liste imajın `drivers\etc\hosts` dosyasında işaretli bir
  bölüm: `# >>> WinLove: <id>` … `# <<< WinLove: <id>`; bölümün dışına dokunulmaz, boş bölüm silinir. Kuyrukta liste
  başına `SetHosts`; imajda olan bölümler "imajda" görünür (D-045 gibi).
- **DNS:** ağ bağdaştırıcısı GUID'i imajda bilinmediği için arayüz başına değer yazılamaz → ilke değerleri
  (`HKLM\SOFTWARE\Policies\Microsoft\Windows NT\DNSClient`: `NameServer`, `DoHPolicy`). Ayarlar › Ağ'a üç ayar:
  hazır sunucular (Cloudflare, Google, Quad9, AdGuard), özel liste, DoH. `SetDns` işlem türü ayrılmıştı; kullanılmıyor.
Kanıt: bölüm ayrıştırma / yazma birim testli (satır sonu, BOM, çift bölüm), render. **Gerçek imajda kanıtlandı (kullanıcı, yönetici, 2026-10-01 11:51, `tools\lab_features.ps1`): ALL PASSED**: telemetri
bölümü iki sürümün `hosts` dosyasında. **Görülmeyen:** kurulan sistemde etkisi (hosts, DNS ilkesi).

## D-048 — Zamanlanmış görevleri kapatma: kurulumdan sonra, schtasks ile (2026-10-01)
Bağlam: Çevrimdışı imajın `Windows\System32\Tasks` klasörü neredeyse boş (görevler kurulumda kaydediliyor); görev
XML'ini veya `TaskCache`'i elle değiştirmek karma denetimini bozar.
Karar: Yeni sayfa "Görevler" (Servisler'in altında): katalog (`resources/catalog/tasks.json`, 38 görev: telemetri,
bakım, özellikler, güncelleme; "önerilen" işaretli) + özel görev yolu. Kuyrukta görev başına `SetTaskState`; Uygula
`Windows\Setup\Scripts\WinLove\tasks.cmd` betiğini yazar (`schtasks /Change /TN "…" /Disable`, çıktı
`%ProgramData%\WinLove\tasks.log`) ve `SetupComplete.cmd`'ye `call` satırı ekler (`core/postsetup/SetupScripts`,
Kurulum Sonrası ile ortak). İmajdaki betik okunarak "imajda" gösterilir.
Kanıt: betik üretimi / okuma birim testli, render. **Gerçek imajda kanıtlandı (kullanıcı, yönetici, 2026-10-01 11:51, `tools\lab_features.ps1`): ALL PASSED**: commit edilmiş imajda `tasks.cmd`
Autochk\Proxy'yi kapatıyor, `SetupComplete.cmd` onu çağırıyor. **Görülmeyen:** kurulumda gerçekten kapandıkları (VM).

## D-047 — USB'ye yazma: diskpart + bootsect + kopya, FAT32 ve .swm bölme; yalnız USB / SD diskleri (2026-09-30)
Bağlam: Kullanıcı "USB'ye yazma"yı istedi; P06'nın USB sekmesi yer tutucuydu.
Karar:
- **Yöntem Microsoft'un belgelediği yol:** `diskpart` (clean, `convert mbr` + `active` → BIOS + UEFI, ya da
  `convert gpt` → yalnız UEFI; tek birincil bölüm; `format fs=fat32 quick`; `assign`), ardından medyanın kendi
  `boot\bootsect.exe /nt60 X: /force /mbr` (BIOS önyükleme kodu), sonra dosyalar. Kendi bölümleyici / FAT32
  biçimlendiricimiz yazılmadı: yanlış bir sektör başka bir diski bozar; Windows'un araçları sınanmış. Betik
  `%TEMP%`e yazılır, çıktısı (OEM kod sayfası) loga gider, bitince silinir. Ortak süreç çalıştırıcı
  `core/system/Process` (dism.exe de buna geçti).
- **FAT32 sınırları:** 4 GB'tan büyük install.wim → `WIMSplitFile` ile `install.swm`, `install2.swm` … (3800 MB;
  Setup kendisi okur). 4 GB'tan büyük install.esd bölünemez → hata, "ISO sekmesinde WIM'e çevir". Windows'un
  FAT32 biçimlendiricisi 32 GB'ta durur → büyük bellekte 32 000 MB'lık bölüm (kalan boş). exFAT / NTFS + UEFI:NTFS
  kullanılmadı: UEFI ürün yazılımları FAT32'yi garanti okur.
- **Güvenlik:** liste yalnız USB / SD / MMC veri yolundaki diskler (IOCTL_STORAGE_QUERY_PROPERTY; yönetici
  gerekmez); Windows, sistem, önyükleme ve sayfa dosyası birimlerinin diski hiç listelenmez. Yazmadan hemen önce
  disk yeniden okunur; veri yolu + satıcı + model + seri + boyut seçilenle aynı değilse durur (takılıp çıkarılma).
  Önce plan (4 GB denetimi, yer) — disk silinmeden hata verilir. Arayüzde her zaman görünen uyarı bandı ve diski
  adıyla, boyutuyla, harfiyle söyleyen onay dialogu; "Sil ve yaz" Enter'la seçilmez. VHD(X) yalnız `wlcli
  --allow-virtual` ile (lab testi), uygulamada asla.
- **Aynı hat:** USB, ISO'nun işlem hattını kullanır (ISO kaynağı önce çalışma klasörüne açılır, isteğe bağlı yeniden
  paketleme, boot.wim kopyasına gereksinim atlamaları, kökte autounattend.xml); son adım `buildIso` yerine
  `writeUsb`. Yönetici değilse UAC ile yeniden başlatılır (`--page=iso`), disk yeniden seçilir.
Kanıt: disk listeleme bu makinede (NVMe sistem diski "system" olarak tanındı, listeye girmedi); saf parçalar birim
testli (etiket, bölüm boyutu, diskpart betiği, 4 GB planı seyrek dosyayla); hat testi (sahte yazıcıyla: kök dosya,
yamalı boot.wim, kök sürücü sonucu); render (USB sekmesi, onay). **Gerçek yazma kanıtlandı (kullanıcı, yönetici, 2026-09-30 23:33, `tools\lab_usb.ps1`, MBR): 26 / 26 PASS** — VHDX disk 1 (dosya destekli sanal) yalnız `--allow-virtual` ile listelendi; `--yes` olmadan reddedildi; diskpart 1,4 sn (clean, MBR, FAT32, active, D:); bootsect FAT32 + MBR önyükleme kodunu yazdı; install.wim (6882 MB) 2 .swm parçasına bölündü, DISM 6 sürümü okudu; önyükleme sektörü 55 AA + BOOTMGR; 7733 MB 14 sn (VHDX, önbellek). **Görülmeyen:** `-Gpt` çalıştırması, gerçek USB bellek (yazma hızı, çıkarılabilir
medya) ve ondan önyükleme.

## D-046 — Güncelleme indirme: Microsoft Update Catalog'dan en yeni LCU / .NET, doğrulamalı indirme (2026-09-30)
Bağlam: Kullanıcı "güncelleme indirme"yi istedi. P08 yalnız elle getirilen .msu / .cab dosyalarını alıyordu.
Karar:
- **Kaynak: Microsoft Update Catalog** (`www.catalog.update.microsoft.com`) — NTLite'ın ve topluluk araçlarının
  kullandığı yol; Windows Update API'si çalışan sistemi tarar, imajı değil. `Search.aspx?q=` HTML tablosu elle
  ayrıştırılır (regex yok, satır id'si GUID olmayan satır atlanır), `DownloadDialog.aspx` (POST `updateIDs`)
  dosyaları URL + **SHA-256** ile verir. Ayrıştırma saf fonksiyonlar, kaydedilmiş sayfa parçalarıyla birim testli.
- **Hedef imajdan:** derleme → "Windows 11, version 25H2" / "Windows 10 Version 22H2", mimari (x64 / arm64).
  İki arama (toplu + .NET); başlıktan tür (LCU / önizleme / .NET / dinamik), KB, "(26200.9457)" derlemesi.
  Teklif: en yeni yayımlanmış LCU ve .NET (işaretli), onlardan yeni önizleme (işaretsiz). Hotpatch, Server,
  dinamik güncellemeler (Safe OS / Setup — WinRE ve kurulum medyası içindir) sunulmaz. Aynı derlemede revizyonu
  imajınkinden büyük olmayan LCU "imaj daha yeni" (seçilemez). Windows 10'da aynı gün çıkan üç .NET paketinden
  başlığı en çok çerçeve sayan (birleşik paket) seçilir; uygulanabilirliğe DISM karar verir.
- **İndirme:** WinHTTP (`core/net/Http`, sistem proxy'si, TLS 1.2/1.3), `<work>\updates\`, önce `.part`, kesilirse
  Range ile kaldığı yerden; bitince SHA-256 denetimi (uymazsa dosya silinir). Aynı dosya doğru özetle oradaysa
  indirilmez. Yalnız Microsoft sunucuları (`*.microsoft.com` https; `*.windowsupdate.com` http de olabilir — özet
  zorunlu). Dosya adı web sayfasından geldiği için klasör / sürücü / akış içeremez.
- **24H2+ checkpoint:** LCU'nun indirme listesi dayandığı checkpoint MSU'yu (KB5043080) da içerir; ikisi aynı
  klasöre iner, **yalnız LCU kuyruğa girer** — DISM checkpoint'i aynı klasörde kendisi bulur (P08 spec'teki not).
  İmajda zaten olan checkpoint'i ayrıca kuyruklamak "uygulanamaz" diye atlanan bir adım üretirdi.
- **Arayüz:** Güncellemeler başlığında "Güncellemeleri bul" (imaj bağlı olmalı) → dialog (Güncelleme · KB · Tarih ·
  Boyut · Not; düğme sayıyı ve toplam boyutu söyler) → indirme şeridi bırakma alanının yerinde (KB, bayt, %,
  Durdur) → bitince paketler kuyruğa, bildirim klasörü gösterir. Ağ işi kendi iş parçacığında
  (`UpdateCatalogController`): 5 GB'lık indirme DISM'i ve kaynak okumayı bekletmez. Durdurmak hata değildir;
  `.part` kalır.
Kanıt: `wlcli catalog 26200.8037 | 19045.3803 | 26100.1 --arch=arm64` gerçek katalogda doğru teklifleri verdi;
.NET CU (92 MB) indirildi, SHA-256 tuttu (27 sn); ikinci çalıştırma indirmedi; 50 MB'lık `.part` Range ile
tamamlandı ve özet tuttu. Birim testleri (ayrıştırma, seçim, güvenilir sunucu, controller). Render (dialog,
şerit). **Görülmeyen:** 5 GB'lık LCU'nun indirilip Uygula'da imaja eklenmesi (checkpoint'li klasörle).

## D-045 — İmajdaki mevcut değerleri okuma: Kayıt Defteri ve Ayarlar / Tweaks imajı gösterir (2026-09-30)
Bağlam: Kullanıcı "imajdaki mevcut değeri okuma"yı istedi. P11 / P12 yalnız kuyruğa bakıyordu: bir kez uygulanmış
(ya da başka bir araçla yapılmış) imaj yeniden bağlanınca her şey "Windows varsayılanı" görünüyordu.
Karar:
- **Okuma `offreg.dll` ile** (Offline Registry Library, Windows 8'den beri System32'de, dinamik yüklenir):
  `core/image/RegistryRead` (`OfflineRegistryReader`). Hive dosyası belleğe ayrıştırılır; `RegLoadKey` yok →
  ayrıcalık açılmaz, hive boşaltılmaz, unmount'u kilitleyemez. Yalnız dosyaya okuma izni ister (bağlı imajda
  yükseltilmiş süreç zaten var). `RegLoadAppKey` SOFTWARE'de `ERROR_BADDB` verdiği için seçenek değildi; kendi regf
  ayrıştırıcımız gereksiz iş olurdu. `CurrentControlSet` → `Select\Current`. `wlcli reg-check <file.reg> <klasör>`.
- **Ne okunur:** iki katalogdaki (tweaks.json, settings.json) her yazım, ayarların metin dosyaları (`imageFileHas`),
  metin ayarlarının (OEM bilgisi) dize değerleri. Servis başlangıç türleri zaten okunan servis listesinden gelir.
  Bağlamadan hemen sonra, önyüklemeden (`PreloadController`) **önce** motor iş parçacığında (~0,4 sn);
  sonuç `AppState::imageValues()`, soru `AppState::imageHas(op)`. Uygula imaj bağlıyken biterse yeniden okunur.
- **Gösterim kuralı:** bir işlem "tutar" = kuyrukta o değerle var, ya da yuvası ve geri dönüşü kuyrukta değilken
  imajda var. Ayarın gösterilen seçeneği = bütün işlemleri tutan (en çok şey söyleyen) seçenek; yoksa varsayılan.
  **Yalnız silmelerden oluşan seçenek imajdan tanınmaz**: olmayan bir değer, silme çalışmadan önce de sonra da
  aynı görünür (bozulmamış 25H2'de ModernSharing anahtarı zaten yok → "Paylaş" menüsü kapalı görünürdü).
- **Geri alma:** imajdaki seçenekten varsayılana dönmek = değer silinir (`"-"`), oluşturulmuş anahtar silinir.
  Servis, dosya ve silme içeren seçeneklerde geri dönüş yok (öncesi bilinmiyor): denetim imajdaki konuma döner,
  ipucu "burada geri alınamaz" der. Başka bir seçeneğe geçişte, eski seçeneğin yeni seçenekle ortak olmayan yazımları
  da geri alınır.
- **Sayılar:** gezinme rozetleri artık "Uygula'nın değiştireceği" ayar / tweak sayısı (imajda olan sayılmaz);
  P11 kategori kartları ("{s} / {n} seçili") imajdakileri de sayar.
- Metin ayarında imajdaki dize kutunun yer tutucusu olur ("imajda: Contoso"); kuyruk değişmez.
Kanıt: bozulmamış 25H2 Pro hive'ları (7-Zip ile lab WIM'inden, yönetici gerekmeden) üzerinde 277 katalog yazımı:
3'ü imajda — duvar kağıdının iki varsayılan değeri ve ModernSharing silmesi; diğer her ayar doğru biçimde
varsayılan. offreg ile üretilen sentetik hive'larla birim testi; P11 / P12 mantığı birim testli; render
(`--demo-image-values`). **Görülmeyen:** uygulama içinde gerçek bağlı imajda okuma (yönetici oturumu).
Yan bulgu: "Paylaş menüsü" ayarı çevrimdışı hiçbir şey yapmıyordu (anahtar kurulumda oluşuyor) → `firstLogon`
yapıldı: silme SetupComplete'te yeniden çalışır. VM'de doğrulanmadı.

## D-044 — Hazır komutlar (güç planı, ağ); ağ ayarları; klasik Fotoğraf Görüntüleyicisi (2026-09-30)
Bağlam: Güç planı ve güvenlik duvarı kayıt defteriyle değil komutla (`powercfg`, `netsh`) ayarlanıyor.
Karar:
- Bunlar ayar kataloğuna değil Kurulum Sonrası'na **hazır komut adımları** olarak girer (`PostSetupController::
  readyCommands`, "Hazır uygulamalar" ile aynı işaretleme dialogu — `makeCatalogDialog` artık ikisine de hizmet eder).
  Plan nerede çalışıyorsa orada çalışırlar (SetupComplete / ilk oturum).
- Güç planları Windows'un kendi tanımından sabit GUID'lerle **kopyalanır** (`/duplicatescheme`), sonra etkinleştirilir:
  bazı bilgisayarlarda (Modern Standby) Yüksek performans gizli olduğundan doğrudan `/setactive` başarısız olur.
- Güvenlik duvarı grupları kaynak kimliğiyle (`@FirewallAPI.dll,-32752 / -28502 / -28752`): bu bilgisayarda Türkçe
  adlarına (Ağ Bulma, Dosya ve Yazıcı Paylaşımı, Uzak Masaüstü) karşılık geldikleri doğrulandı.
- Ağ ayarları (LLMNR, IPv6, Wi-Fi etkin noktaları, yeni ağ sorusu, konuk SMB, SMB imzalama) Sistem sekmesinde, kayıt
  defteriyle. IPv6 "kapalı" Microsoft'un önermediği seçenek: "IPv4'ü tercih et" ayrı seçenek, risk orta.
- Fotoğraf Görüntüleyicisi: iki imajda da duran `PhotoViewer.FileAssoc.Tiff` kaydının birebir kopyası Bitmap / Jpeg /
  Gif / Png için + `Capabilities\FileAssociations`; `PhotoViewer.dll` Windows 10 22H2 ve 11 25H2 imajlarında var (7-Zip ile
  bakıldı). Varsayılan yapılmaz: "Birlikte aç" ve Varsayılan uygulamalar'da seçilebilir olur.
Kanıt durumu: birim testli + render; kurulan Windows'ta görülmedi.

## D-043 — Sağ tık menüsü, bildirimler, oyun ayarları (2026-09-30)
Bağlam: Kullanıcı eksik alanlardan üçünü seçmemi istedi.
Karar: Hepsi ayar kataloğunda, kayıt defteriyle. "Sahipliği al" `HKLM\SOFTWARE\Classes\{*,Directory}\shell\runas` fiili (Windows
kendisi yükseltir); `icacls` grubu ada göre değil SID ile (`*S-1-5-32-544`: Türkçe Windows'ta "Yöneticiler");
`takeown /d` kullanılmadı (yanıt harfi dile bağlı). Menü etiketi imajın diline göre seçilir (Türkçe / İngilizce
seçenek): katalog imajın dilini bilmiyor. "Paylaş" `ModernSharing` işleyici anahtarı silinerek, "Klasöre kopyala /
taşı" iki kabuk uzantısı anahtarı oluşturularak. Bildirimler ve oyun ayarları HKCU olduğu için ilk oturumda da.
Kısayol oku kaldırma eklenmedi: güvenilir bir boş simge kaynağı yok.
Kanıt durumu: birim testli; kurulan Windows'ta görülmedi.

## D-042 — Vurgu renkleri, "Uygulama ayarları" sol menüde; ayar kataloğu büyüdü (2026-09-30)
Bağlam: Kullanıcı "uygulamaya tema özelliği, açık tema, farklı temalar" istedi. Koyu / Açık / Yüksek kontrast / Sistem
temaları zaten vardı ama ayar sayfası sol menüde olmadığı için bulunamıyordu. Tasarım (ekran 19) beş vurgu rengi
karesi gösteriyor ama yalnız bakırın token seti var (16-app-settings.md §4 bu yüzden eklememişti).
Karar (kural 3'ten bilinçli sapma):
- `ui::Accent` (Bakır · Deniz · Nar · Gök · Zeytin). Bakır = handoff tokenları, dokunulmadı. Diğer dördünün koyu tema
  tabanı tasarımdaki kare rengi; hover (+%12 beyaz), pressed (−%9), subtle (sayfa üstüne %16), yazı rengi ve açık tema
  değerleri (taban, sayfada ve beyaz yazı altında AA olana dek koyulaştırıldı) **türetildi** ve `Palette.cpp`'de
  sabit. `TokensTests` her set için token sözleşmesini (onAccent ≥ 4.5, sayfada ≥ 4.4, panelde / subtle'da ≥ 3)
  denetler. Yüksek kontrastta vurgu değişmez. Tasarım bir token seti verirse bu tablo onunla değiştirilmeli.
- "Uygulama ayarları" sol menünün son grubunda (prototipte yalnız paletteydi: sapma).
- Aynı gün ayar kataloğuna 38 ayar ve "Sistem" sekmesi daha eklendi (gizlilik, ipuçları, arama, depolama, oturum
  açma, Gezgin gezinti bölmesi, Başlat / görev çubuğu düzeni, güncelleme, UAC / SmartScreen, Num Lock, fare, AutoPlay);
  her yazımın çevrimdışı bir hive'a düştüğü test ediliyor. Kurulan Windows'taki etkileri görülmedi.

## D-041 — Görev çubuğu, Copilot / Edge / BitLocker / Windows Update ayarları, hazır uygulamalar, rapor, kişiselleştirme (2026-09-30)
Bağlam: Kullanıcı önerilen listeden 1, 2, 3, 4, 5, 7, 8'i seçti.
Karar:
- Hepsi mevcut mekanizmalarla: ayar kataloğu (`settings.json`, P12) + kuyruk işlemleri. Kayıt defteri dışında iki
  işlem türü: `WriteFile` (D-040) ve yeni **`CopyFile`** (bu bilgisayardaki bir dosya → imajda `Users\Default` /
  `ProgramData` altı, en çok 64 MB; hedef yol kuralları `WriteFile` ile aynı).
- Ayar kataloğunda iki yeni denetim: **metin** (OEM bilgisi) ve **dosya** (duvar kağıdı, kilit ekranı). Durum yine
  kuyruktan türetilir (`ImageSettingsController::valueIn`); presetler değeri taşır. Dosya ayarı yalnız .jpg/.jpeg
  kabul eder (hedef adı sabit `….jpg`; biçimi uzantıdan tahmin etmek yerine sınırlamak).
- Görev çubuğu: Microsoft'un OEM yöntemi — `LayoutXMLPath` (REG_EXPAND_SZ, `%ProgramData%\WinLove\…`) ve
  `PinListPlacement="Replace"` düzeni. Başlat düzeni (D-040) ayrı dosyada: ikisi birlikte seçilebilir.
- Kilit ekranı `PersonalizationCSP` ile (Pro'da da işleyen tek çevrimdışı yol); yan etkisi: kullanıcı Ayarlar'dan
  değiştiremez — ipucunda yazıyor. Duvar kağıdı varsayılan profildeki `Control Panel\Desktop` ile; yol `C:\` varsayar.
- Yeni "Güncelleme" sekmesi (tasarımda yok: sapma). Yeni ayarların hiçbiri önerilenlere alınmadı.
- Hazır uygulamalar: katalog kodda (`PostSetupController::popularApps`, 42 paket, kategorili), çoklu seçim dialogu.
- Rapor: `app/ApplyReport` — sayfa ile aynı sözcükler (`applySkipReason`, `applyPhaseName` artık ortak).
Kanıt durumu: hepsi birim testli + render; **hiçbiri gerçek imajda Uygula ile çalıştırılmadı ve kurulan Windows'taki
etkileri görülmedi**. Değerler Microsoft'un belgelediği / yaygın kullanılan yöntemlerden; en belirsizleri duvar kağıdı
(ilk oturumda temanın ezip ezmediği) ve `NoAutoRebootWithLoggedOnUsers` (yalnız zamanlanmış kurulumlarda etkili).

## D-040 — Başlat menüsü temizliği; yeni kuyruk işlemi `WriteFile` (2026-09-30)
Bağlam: Kullanıcı Windows 10 ve 11 için Başlat menüsü temizliği istedi (kutucuklar, reklam uygulamaları, widget).
Windows 11'in sabitlenenleri bir ilke değeriyle boşaltılabiliyor; Windows 10'un kutucukları için kayıt defteri
karşılığı yok — düzen, varsayılan profildeki `LayoutModification.xml` dosyasından okunuyor.
Karar:
- Yeni işlem **`OpKind::WriteFile`** (hedef = imaj köküne göre yol, değer = metin): `core/image/ImageFiles`. Yol
  kullanıcı girdisidir (presetle gelir): yalnız `Users\Default\…` ve `ProgramData\…` altına, bileşen yolu kurallarıyla
  (göreli, `.`/`..` yok, link üzerinden değil), en çok 1 MiB. Kök klasör imajda yoksa yazılmaz. `Settings` aşamasında.
- Ayar kataloğunda (`settings.json`) bir seçenek artık dosya da taşıyabilir (`files`). P11 tweak listesi yalnız kayıt
  defteri olduğundan "sabitlenenler" ayarı yalnız P12'de; diğer ikisi iki katalogda da aynı değerlerle.
- "Reklam uygulamaları" ayarı genişledi: `DisableWindowsConsumerFeatures` ilkesi Pro / Home'da etkisiz olduğu için
  işi varsayılan profildeki `ContentDeliveryManager` değerleri yapar; Windows bunları ilk oturumda sıfırlayabildiği
  için ayar `firstLogon` oldu (D-026).
- "Sabitlenenler" önerilenlere **alınmadı** (zevk meselesi); diğer ikisi zaten önerilendi.
Kanıt durumu: dosya yazımı ve katalog birim testli (geçici klasörde; gerçek bağlı imajda çalıştırılmadı —
`Users\Default\…\Shell` klasörünün yazılabilirliği orada görülecek). **Kurulan Windows'taki etkisi görülmedi**; değerler
yaygın kullanılan yöntemlerden (boş `pinnedList`, boş `StartLayout`) alındı, kullanıcının VM kurulumu gösterecek.

## D-039 — OOBE'de kendiliğinden kurulanlar: OneDrive (Windows 10) ve yeni Outlook (2026-09-30)
Bağlam: Kullanıcının Windows 10 22H2 kurulumunda, internet varken OneDrive ve yeni Outlook yine kuruldu. İmaj
incelendi (yönetici olmadan, 7-Zip + hive okuma): OneDrive bileşeni Windows 10'da hiç sunulmuyordu (kurulum dosyası
`SysWOW64`'te, tarif yalnız `System32`'ye bakıyordu); Outlook imajda uygulama olarak yok, bir güncelleme
zamanlayıcısı kaydıyla OOBE'de indiriliyor.
Karar:
- `onedrive` tarifine `SysWOW64\OneDriveSetup.exe` ve varsayılan profildeki `OneDrive.lnk` eklendi (olmayan yol hata değil).
- Yeni bileşen `outlook-install`. Windows 11 ve 10 farklı yoldan kurduğu için tek tarif ikisini de kapatır:
  `UScheduler_Oobe\OutlookUpdate` silinir + `UScheduler\OutlookUpdate\workCompleted = 1` (Windows 11);
  `ExpeditedAppRegistrations\MS_Outlook` klasörü silinir + Microsoft'un belgelediği `BlockedOobeUpdaters = ["MS_Outlook"]`
  + `Deprovisioned\<aile>` (kayıt dosyasında `HonorDeprovisioning: true`) (Windows 10).
- Katalogda yeni alan **`always`**: bileşen, diskte yolu olmasa da her imajda sunulur (Windows 11'de bulunacak dosya
  yok; ayrıca ileride gelecek bir güncellemenin kaydına karşı da önleyici). Boyutu 0 görünür; inspector'da kayıt
  defteri değişiklikleri de listelenir.
- İmajdaki "Outlook (yeni)" uygulaması (Windows 11) ayrı bir `RemoveAppx` seçimidir; not kullanıcıyı oraya yönlendirir.
Kanıt durumu: tarif yürütücüsü ve kullandığı kayıt işlemleri gerçek imajda kanıtlı; **bu iki tarifin OOBE'deki etkisi
henüz görülmedi** (kullanıcının VM kurulumu gösterecek). Windows 10'da OneDrive CBS paketini DISM'in kaldırıp
kaldırmadığı da görülmedi (kaldırmazsa dosya + Run kaydı yine silinir).

## D-038 — DISM'in yapmadığını kendi kodumuzla: kilitli uygulamalar ve boot.wim (2026-09-30; ikisi de gerçek imajda kanıtlandı)
Bağlam: `Microsoft.SecHealthUI` ve `Microsoft.DesktopAppInstaller` her Uygula'da `0x80073CFA` ile kalıyordu; gereksinim
atlama yalnız yanıt dosyasına bağlıydı (dosya ISO'ya girmeyince Setup durdu); Setup'ın kendi imajına sürücü eklenemiyordu.
Karar:
- **Uygulama kaldırma (yerel):** DISM'in bir uygulamayı kaldırırken imajda ne değiştirdiği, 45 uygulaması kaldırılmış
  imajla özgün imaj karşılaştırılarak çıkarıldı (ENGINE saha notu) ve aynısı bir `ComponentRecipe` olarak üretiliyor:
  ailenin `WindowsApps` klasörleri + ClipSVC lisans dosyası silinir; `AppxAllUserStore\Applications\<ad>` ve
  `\Staged\<aile>` silinir, `\Deprovisioned\<aile>` oluşturulur. Bağımlı framework paketlerine dokunulmaz.
  Yeni bir silme kodu yok: sistem bileşenlerinin tarif yürütücüsü (yol denetimleri, ACL, junction reddi) çalışır.
- **boot.wim:** `patchBootImage` önyükleme index'ini ayrı bir klasöre bağlar, `HKLM\SYSTEM\Setup\LabConfig` değerlerini
  Setup'ın kendi kayıt defterine yazar, sürücüleri ekler, commit eder. Başarısızlıkta discard.
- **Sıra (kural 6):** ikisi de önce yalnız motor + `wlcli appx-remove` / `wlcli boot-patch` + `tools\lab_appx.ps1` /
  `tools\lab_boot.ps1`.
- **Uygulamalar — kanıt sonrası (aynı gün):** `lab_appx.ps1` kullanıcının yönetici oturumunda geçti. Applier
  `RemoveAppx` adımında `0x80073CFA` alınca `removeAppxNative`'e geçer (sürüm koşulu yok: DISM'in cevabı belirler).
  Katalogdaki `lockedSince` alanı, `Item::locked`, kilit simgesi ve "Kaldırılamaz" metinleri **kaldırıldı**: iki
  uygulama diğer Yüksek riskli uygulamalar gibi seçilir; ne anlama geldiği katalog notunda ve uyarı bandında yazar.
  Kurulan sistemde bir şeyin bozulup bozulmadığı (Windows Güvenliği sayfası, winget) VM'de görülmeli — görülmedi.
- **boot.wim — kanıt sonrası (aynı gün):** `lab_boot.ps1` geçti. ISO sayfasına tek seçenek eklendi (tasarımdan
  sapma: ekran 16'da yok): "Gereksinim atlamalarını boot.wim'e de yaz". Yazılan değerler Katılımsız Kurulum'daki
  atlama seçimleridir (tek kaynak); yanıt dosyası ISO'ya girmese de yazılır. **Kurulum klasörü değiştirilmez:**
  `sources\boot.wim` `<çalışma>\boot\boot.wim`'e kopyalanır, kopya `<çalışma>\boot\mount`'a bağlanıp yamalanır, `buildIso`
  ISO'da klasördeki dosyanın yerine onu koyar (`IsoOptions::replacedFiles`), kopya silinir. Böylece sonuç her
  üretimde istekten türer (kutu kapatılınca eski yama kalmaz), kullanıcının kendi klasörü de kaynak olabilir.
  Yama başarısızsa ISO üretimi de başarısızdır: istenen atlama olmadan "hazır" denmez. Sürücü ekleme kanıtlanmadığı
  için arayüze alınmadı.

## D-036 — Yanıt dosyasında `UserData` her zaman `ProductKey` taşır (2026-09-30)
Bağlam: Kullanıcının Windows 10 22H2 (tek sürüme indirilmiş) ISO'sunda Setup "Windows unattend yanıt dosyasından
<ProductKey> ayarını okuyamıyor" diye durdu. Dosyada "Lisans sözleşmesini kabul et" yüzünden
`<UserData><AcceptEula>true</AcceptEula></UserData>` vardı, `ProductKey` yoktu (anahtar alanı boş bırakılmıştı).
Karar: `UserData` yazılıyorsa `ProductKey` de yazılır — kullanıcının anahtarı; yoksa kurulacak sürümün Microsoft'un
genel (varsayılan) anahtarı; sürüm bilinmiyorsa `00000-00000-00000-00000-00000` + `WillShowUI=Always` (Setup anahtar
sayfasını gösterir). Kurulacak sürüm açık kaynaktan türetilir: imajda tek sürüm varsa o, "Kurulacak sürüm" seçiliyse o.
16 genel anahtar kodda (`kGenericKeys`); her biri `pidgenx.dll` ile Windows 10 22H2 imajının ve Windows 11 25H2
kurulumunun `pkeyconfig.xrm-ms` dosyasına karşı denendi (hepsi beklenen sürümü verdi). Dosyadan okunan genel anahtar
ya da yer tutucu seçeneklere "kullanıcının anahtarı" diye girmez: Pro'da kaydedilmiş bir preset Home imajına Pro'nun
anahtarını dayatmasın.
Doğrulanmayan: sıfır anahtar + `Always` ile Setup'ın anahtar sayfasını gerçekten gösterdiği (çok sürümlü imajda VM testi).

## D-035 — İmajlar: çoklu seçim, yeniden adlandırma, kendi doğrulayıcımız; sürüm yükseltme kanıt bekliyor (2026-09-30)
Bağlam: Kullanıcı İmajlar sayfasına dört özellik istedi: sürüm bilgilerini düzenleme, sürüm yükseltme, çoklu seçim,
WIM bütünlük denetimi.
Karar:
- **Çoklu seçim** durumda tutulur (`AppState::selection()`, sıralı; `selectedIndex()` içlerinden birincil olan).
  Kurallar widget'tan ayrı, saf bir yapıda (`EditionSelection`): tık = tek, onay kutusu / Ctrl+tık = ekle-çıkar,
  Shift = aralık, Ctrl+A = hepsi. Sil ve dışa aktar işaretlilerin hepsini alır; bağla ve yeniden adlandır birincili.
- **Yeniden adlandırma** wimgapi ile (`WIMSetImageInformation`): ad NAME + DISPLAYNAME'e, açıklama DESCRIPTION +
  DISPLAYDESCRIPTION'a birlikte yazılır — ayrı ayrı sormak Setup'ta görünmeyen bir ad üretmenin yolu olurdu.
  FLAGS arayüzde yok (motor ve CLI alır; sürüm yükseltme kullanacak).
- **Doğrulama kendi kodumuz**: wimgapi'nin "verify" bayrakları yalnız bütünlük tablosuna bakar, Microsoft imajlarında
  o tablo yok (başlıkta boş); `WIM_FLAG_NO_APPLY` veriyi hiç okumuyor (ölçüldü: 0,2 sn). Bu yüzden lookup table +
  chunk tabloları + LZX çözücü (`core/image/wim/Lzx`, ~300 satır, yalnız çözme) + BCrypt SHA-1; XPRESS için
  `RtlDecompressBufferEx`. 8 iş parçacığı, akış başına bağımsız. Yazma tarafı wimgapi'de kalır. ESD (LZMS) kapsam
  dışı. Doğruluğun kanıtı imajın kendisi: 94.409 akışın hepsi kayıtlı SHA-1'iyle çıkıyor.
- **Sürüm yükseltme** (`dism.exe /Set-Edition`, DISM API'sinde karşılığı yok; ortak `DismExe` çalıştırıcısı): önce
  yalnız motor + `wlcli edition` + `tools\lab_edition.ps1` yazıldı (kural 6); kullanıcı betiği yönetici olarak
  çalıştırdı, 14 denetimin hepsi geçti, **sonra** UI yazıldı. Kuyruk işlemi (`OpKind::SetEdition`, tek slot, yeni
  `Phase::Edition` — her şeyden önce: DISM bekleyen işlemi olmayan imajda sürüm değiştirir ve sonraki adımlar
  kurulacak sürümün üstünde çalışır). Commit sürüm kimliğini XML'e kendisi yazar ama adı bırakır ("Windows 11 Home"
  kalır) → `ApplyJob` commit'ten sonra ad / açıklama / FLAGS'i yazar (`textAfterEditionChange`: Microsoft'un
  varsayılan adı yeni sürümünkiyle değişir, kullanıcının verdiği ad kalır). Risk "orta": özet sayfasının yüksek risk
  uyarısı kaldırma için yazılmış; tek yönlü olduğunu dialog söylüyor. İmaj zaten o sürümdeyse adım başarı sayılır.
  Hedef sürümler mount sonrası ön okumaya eklenmedi (her mount'a ~8 sn), istenince okunur ve mount başına saklanır.
