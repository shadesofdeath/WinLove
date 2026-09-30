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
