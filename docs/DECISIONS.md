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
