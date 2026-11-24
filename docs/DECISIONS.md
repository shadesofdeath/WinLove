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
→ D-135 (2026-11-10 → 11): DComp kökü uygulanıp ölçüldü, sahibinin isteğiyle tamamen kaldırıldı; bu karar geçerli.

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
Bağlam: Kullanıcı notu — bazı tweak'leri Windows ilk oturum açılışında yok sayıyor (referans araç bunu SetupComplete ile
çözüyor). OOBE gizlilik sayfası konum / reklam kimliği / kişiye özel deneyim değerlerini yeniden yazar; ilk oturum
varsayılan temayı ve görev çubuğu düzenini uygular; ContentDeliveryManager kendini yeniden tohumlar.
Karar: Katalogda `"apply": "firstLogon"` işaretli tweak'ler `SetRegistryFirstLogon` işlemi olur: değer çevrimdışı
hive'a yine yazılır **ve** imaj içindeki `Windows\Setup\Scripts\WinLove\` altında bir .reg dosyasına kaydedilir:
HKLM/HKCR → `setupcomplete.reg` (`SetupComplete.cmd` ile SYSTEM olarak, OOBE'den sonra, ilk oturumdan önce içe
aktarılır; mevcut SetupComplete.cmd korunur, satır bir kez eklenir), HKCU → `firstlogon-user.reg` (Default profilin
RunOnce değeri ile her yeni kullanıcının ilk oturumunda `reg.exe import`). Ekranda "İlk oturumda" etiketi.
Sınır: OEM ürün anahtarıyla etkinleştirilen sürümlerde Windows SetupComplete.cmd'yi çalıştırmaz; RunOnce reg.exe
kısa bir konsol penceresi gösterebilir. P14 Kurulum Sonrası aynı klasörü/mekanizmayı genişletecek.
Ek (2026-09-30, kullanıcı kararı): yol referans araçla aynı kalır — SetupComplete.cmd + ilk oturum; unattend /
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
  referans aracın yıllarca uyumluluk verisiyle yaptığı ayrı bir motor. Kapatmak için Servisler / Ayarlar sayfaları var.
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

## D-156 — Ön ayar kuyruğun üstüne mi, yerine mi: sorulur; uzun iş görev çubuğunda (2026-11-24)
Bağlam: Sahibinin önerilerden seçtiği 1. ve 2. madde. D-152'nin kullanıcısının asıl şikâyeti ön ayarın hep kuyruğun
**üstüne** eklenmesiydi (`PresetController::apply` → `queueMany`); D-152 bunu "önce Tüm değişiklikleri sıfırla" ile iki
tıka indirmişti. Uygula / ISO / indirme dakikalar sürüyor; WinLove arkadayken bittiğini gösteren bir şey yoktu.
Karar:
- **Ön ayar yükle:** kuyrukta (ya da boot.wim işaretlerinde) bir şey varsa "Kuyrukta {n} değişiklik var" sorusu:
  `Vazgeç` · `Üstüne ekle` (eskisi gibi) · **`Yerine koy`** (Enter). Yerine koy = kuyruk + boot.wim işaretleri boşalır,
  ön ayar eklenir; `AppState::batchQueue` içinde, tek geri alma adımı (Geri al ikisini birlikte geri getirir; boot.wim
  işaretleri kuyrukta olmadığı için onlar geri gelmez, D-152'deki gibi). Kuyruk boşsa, Uygula sürerken ya da ön ayarda
  işlem yoksa soru yok. Sürükle-bırak ve komut paleti aynı yoldan (`Shell::applyPreset` → `replacePreset`).
- **Görev çubuğu:** durum çubuğunun gösterdiği iş (Uygula, ISO, mount, Windows indir, güncelleme / dil / Store indirmesi)
  pencerenin görev çubuğu düğmesinde de ilerler (`ITaskbarList3`; ölçülemeyen iş belirsiz). İş bitince, 15 sn'den uzun
  sürdüyse ve WinLove önde değilse düğme yanıp söner (`FlashWindowEx`, öne gelince durur). Windows bildirimi (toast)
  yok: paketlenmemiş bir uygulamanın toast'u için kullanıcının kayıt defterine AUMID yazmak gerekir.
  - `ui::Window::setTaskbarProgress` / `flashIfInactive`; Explorer'ın "TaskbarButtonCreated" iletisi yönetici
    WinLove'a UIPI yüzünden gelmez → `ChangeWindowMessageFilterEx` ile açılır; Explorer yeniden başlarsa nesne yenilenir.
  - `Shell::updateTaskbar` (her `updateStatus` sonunda) → `Services::taskbarProgress` / `flashIfInactive` (App'in
    penceresi; render ve testlerde boş).
Kanıt: `--render --demo-preset-ask` (koyu, TR); `tests/app/RemovalGapsTests.cpp` son vaka (boşalt + ön ayar tek batch'te
= tek Geri al). Görev çubuğu: derlendi; canlı pencerede görülmedi.

## D-155 — Sağ tık menüsü: imajın kendi öğeleri, gizle / göster, kendi komutun (2026-11-24)
Bağlam: Sahibinin seçtiği 5. madde. Gezgin'in sağ tık menüsünü düzenlemek WinLove'da yalnız birkaç hazır Ayarlar
seçeneğiydi (klasik menü, "Paylaş" kaldır …). Elle tutulan bir liste istenmiyor (deferred-suggestions): öğeler imajın
kendisinden okunmalı.
Karar:
- **Sayfa:** Yapılandır › Ayarlar sekmesinde `Sağ tık menüsü` (Servisler'den sonra; ikon `MoreVertical`, `PageId::ContextMenu`).
  Tablo: Öğe · iz · Nerede · Tür (Komut / Kabuk uzantısı / Uygulama / WinLove öğesi) · Durum (göz ikonu; tık, Enter ya da
  kartın düğmesi gizler / gösterir). Şeritte `Öğe ekle…` + `Sıfırla`. Kart: kayıt yolu ya da CLSID, Nerede, komut, kaynak
  (DLL / paket), Shift ile mi; not: komut gizlenir silinmez / uzantı her yerde engellenir / Windows 11'de klasik öğeler
  "Daha fazla seçenek göster" altında.
- **Okuma (`core/image/ContextMenu`, offreg — yönetici gerekmez):** imajın SOFTWARE\Classes'ında yedi yer: `*`,
  `AllFilesystemObjects`, `Directory`, `Folder`, `Directory\Background`, `Drive`, `DesktopBackground`.
  - `shell\<fiil>`: adı MUIVerb ya da varsayılan değer, "@dll,-id" imajın dilinde (`resolveImageText`,
    `preferImageLanguage` — Services'teki çözücü paylaşıma açıldı), "&" kısayol işareti atılır; komut; Extended;
    ProgrammaticAccessOnly / LegacyDisable = zaten gizli.
  - `shellex\ContextMenuHandlers`: CLSID başına tek satır (birden çok yerdeyse yerler birleşir), adı CLSID'nin varsayılan
    değeri, kaynak InprocServer32.
  - Windows 11 uygulama öğeleri: `Program Files\WindowsApps\*\AppxManifest.xml` içindeki `FileExplorerContextMenus ›
    ItemType › Verb (Id, Clsid)`. WindowsApps yöneticiye kapalı → `SE_BACKUP_NAME` + `backupListFolders` /
    `backupReadFile` (Appx.cpp'nin yolu); paket demeti klasörleri (`_~_`) atlanır.
  - Gizli mi: fiilde değerler; uzantı / uygulama öğesinde `HKLM\…\Shell Extensions\Blocked\{CLSID}`.
- **Yazma:** gizle = fiile `ProgrammaticAccessOnly` (boş REG_SZ: menüde yok, program yine çağırabilir); uzantı / uygulama
  öğesi = `Shell Extensions\Blocked\{CLSID}` (kullanıldığı her yerde). Göster (imajın gizlediği) = o değerlerin silinmesi.
  Kendi öğe = seçilen yerlerde `…\shell\WinLove.<id>`: varsayılan değer ad, `Icon`, `Extended`, `command` (REG_EXPAND_SZ);
  arka plan / masaüstünde komuttaki `%1` → `%V`. Nerede seçenekleri: dosyalar ve klasörler, yalnız dosyalar, yalnız
  klasörler, klasör arka planı ve masaüstü, sürücüler, hepsi. id adın ASCII harf / rakamı ("open-with-vs-code"), aynı ad
  ikinciye `-2`.
- **Kuyruk:** `OpKind::SetContextMenu`, hedef `entry\<id>` (gizle / göster; imajın durumuna dönmek işlemi çıkarır) ya da
  `add\<id>`; değer JSON `{name, item?, writes}` — yazımlar kuyruğa girerken çözülür, Uygula ve ön ayar ADMX / imaj
  okumadan uygular (Ayarlar fazı, `reg().apply`). Ön ayar karşılaştırmasında öğe adıyla.
Kanıt: `build\lab\d154` (install.wim sürüm 4, Pro 26200.8037, salt okunur bağlandı, sonra çözüldü): 54 öğe 734 ms'de,
Türkçe adlarıyla; SOFTWARE kovanının kopyasına fiil gizle (`Directory\Background\shell\cmd`), uzantı gizle (Önceki
sürümler), öğe ekle → geri okumada "(hidden)" ve yeni öğe. Lab 2: uygulama öğeleri yedekleme yetkisiyle — manifestler
CLSID'yi süslü parantezsiz yazar (ilk koşu 0 öğe), düzeltmeden sonra 4 (Paint, Clipchamp, Not Defteri, Terminal); ad = fiil
kimliği + paketin kısa adı ("OpenTerminalHere (WindowsTerminal)"); dörtten çok dosya türü "N dosya türü" diye sayılır.
VM kurulumu (2026-11-24, `tools\lab_vm.ps1 -Tag d156`, install.wim sürüm 4 Pro 26200.8037, ağsız; değişiklik kümesini uygulamanın kendi denetleyicileri üretti: `tests/app/LabChangesetTests.cpp`, 49 adım `wlcli apply` ile 0 hata; ilk oturumda `tools\vm_check_policies_menu.ps1`, 38 denetim geçti): kabuğun kendi fiil listesi (Shell.Application `Verbs()`) bir dosyada "Not Defteri ile aç"ı gösterir; gizlenen
her öğe bir kez geri açılınca listeye döner ("Sık Kullanılanlara ekle", "Paylaş", "Önceki Sürümleri Geri Yükle") — gizleyen
WinLove'un yazımı; arka plan öğesinin komutu `%V`; imajın gizlediği "Keşfet" Windows 11 menüsünde görünür, "Terminalde
aç" ve "Paylaş" yok (VNC görüntüsü). Uygulama öğeleri `Verbs()` listesinde hiç görünmez (Windows 11 menüsüne özgü).
`tests/core/PolicyMenuTests.cpp` (yazımlar, %V, id, JSON; denetleyici gizle / göster / ekle / kaldır). `--render
--demo-contextmenu[=add]`.

## D-154 — Grup İlkesi: imajın kendi yönetim şablonları, gpedit'in yazdığı gibi (2026-11-24)
Bağlam: Sahibinin seçtiği 4. madde. Ayarlar sayfasının ~200 seçeneği elle tutuluyor; imajın kendisi
`Windows\PolicyDefinitions`'ta binlerce ilkeyi kendi dilinde tanımlıyor (ADMX + ADML). Hazır profil / elle liste istenmiyor.
Karar:
- **Sayfa:** Yapılandır › Ayarlar sekmesinde `Grup İlkesi` (Ayarlar'dan sonra; ikon `ScriptFile`, `PageId::Policies`).
  Süzgeçler: ara ("/"; ad, ADMX adı, kayıt yolu, kategori yolu), Kapsam (Bilgisayar ve kullanıcı / Bilgisayar /
  Kullanıcı), kök kategori, Yalnızca değişenler. Tablo: İlke · iz · Kategori (yaprak; ipucunda tam yol) · Kapsam · Durum
  (Yapılandırılmadı / Etkin / Devre dışı menüsü). "Both" sınıfı ilke gpedit'teki gibi iki satır. Etkin + seçenekli ilke →
  seçenekler penceresi (gpedit'in sunumu sırasıyla: açılır liste, onay kutusu, sayı / metin kutusu; liste ve çok satırlı
  metin `;` ile ayrılır); "Etkinleştir" değerleri denetler, hatada pencere açık kalır. Kart: açıklama metni, kapsam,
  kategori yolu, Uygula'da, desteklenen, şablon (.admx), kayıt yolu, Etkin'in seçenekleri; not: gpedit gösterir, Ayarlar'ın
  aynı değeri yazan seçeneğiyle ikisi seçiliyse sonra gelen geçerli.
- **Okuma (`core/policy/Admx`):** `<mount>\Windows\PolicyDefinitions\*.admx`, metinler `<dil>\<ad>.adml` (imajın dili:
  System32'de kernel32.dll.mui olan klasör; yoksa en-US, yoksa ilk bulunan; dosya başına). Kimlikler
  `<namespace>:<name>` (dosyalar arası `prefix:ref` → `policyNamespaces`); supportedOn metinleri bütün dosyalar
  okunduktan sonra bağlanır. Okunamayan dosya atlanır (log). pugixml UTF-8 / UTF-16 dosyaları kendisi çevirir.
- **Yazımlar (`core/policy/PolicyWrites`) — Group Policy kayıt uzantısının kuralları:** Etkin: valueName → enabledValue
  (yoksa DWORD 1), enabledList, öğeler (boolean true/falseValue, yoksa 1/0, ve listeleri; decimal DWORD ya da storeAsText
  REG_SZ; longDecimal QWORD; text REG_SZ / expandable REG_EXPAND_SZ; multiText REG_MULTI_SZ; enum seçilen öğenin değeri +
  valueList; list girdi başına değer: valuePrefix + 1, 2 … ya da girdinin kendisi, explicitValue "ad=değer"). Devre dışı:
  disabledValue (yoksa DWORD 0), disabledList, öğelerin değerleri silinir (list hariç: adları girdiler). Yapılandırılmadı =
  işlem yok. Varsayılanlar sunumdan (defaultItem, defaultChecked, defaultValue, textBox defaultValue). Aralık dışı sayı,
  sayı olmayan, geçersiz seçim, eksik zorunlu değer → hata, kuyruğa bir şey girmez. Kapsam: Bilgisayar HKLM\, Kullanıcı
  HKCU\ (imajda varsayılan profil).
- **Kuyruk:** `OpKind::SetPolicy`, hedef `machine\<id>` / `user\<id>`, değer JSON `{name, state, values, writes}` —
  yazımlar kuyruğa girerken çözülür: Uygula ve başka build'e uygulanan ön ayar ADMX'e bakmaz. Uygula (Ayarlar fazı):
  yazımlar kovanlara, sonra **yerel ilke nesnesine**: `Windows\System32\GroupPolicy\{Machine,User}\Registry.pol` (PReg v1;
  aynı değerin eski girdisi ve "**del." ikizi değiştirilir, silme "**del.<ad>" olur) + `gpt.ini` (Version = kullanıcı << 16 |
  makine, yazılan tarafın sayacı +1; kayıt CSE'si `{35378EAC…}{D02B1F72/73…}` uzantı listesinin başına; başka araçların
  satırları kalır). Böylece kurulan Windows'ta gpedit.msc ilkeyi ayarlanmış gösterir ve Group Policy istemcisi her
  açılışta / oturumda yeniden uygular. Ön ayar karşılaştırmasında ilke adı + Etkin / Devre dışı.
Kanıt: bu PC'nin şablonları (tr-TR): 3560 ilke, 0,5 sn (Debug). `build\lab\d154`: imajın kendi şablonları 3532 ilke /
429 kategori, 662 ms, Türkçe; kovan kopyasına AutoUpdateCfg (makine, 11 değer) + NoControlPanel (kullanıcı) Etkin →
`reg-check` 3/3, Registry.pol makine 11 / kullanıcı 1 girdi geri okundu, gpt.ini `Version=65537`. `tests/core/PolicyMenuTests.cpp`
(kendi küçük ADMX'imiz: isimler, dosyalar arası kategori, sunum; her öğe türünün yazımı; aralık / seçim / zorunlu
hataları; JSON; Registry.pol biçim / ayrıştırma / birleştirme; gpt.ini başka aracın satırıyla; denetleyici). `--render
--demo-policies[=options]`.
VM kurulumu (2026-11-24, `tools\lab_vm.ps1 -Tag d156`, install.wim sürüm 4 Pro 26200.8037, ağsız; değişiklik kümesini uygulamanın kendi denetleyicileri üretti: `tests/app/LabChangesetTests.cpp`, 49 adım `wlcli apply` ile 0 hata; ilk oturumda `tools\vm_check_policies_menu.ps1`, 38 denetim geçti): dört ilke (AutoUpdateCfg Etkin + seçenek "2", AllowTelemetry Etkin, TS_DISABLE_CONNECTIONS Devre dışı,
kullanıcıda DisableSearchBoxSuggestions Etkin) kayıtta; elle silinen değerleri `gpupdate /force` Registry.pol'den geri yazdı
(makine ve kullanıcı); `gpresult /h` dördünü de adıyla ve durumuyla, kaynak "Yerel Grup İlkesi" diye listeler; Group Policy
günlüğünde uyarı / hata 0. gpedit.msc aynı Registry.pol'ü ve şablonları okur.
Doğrulanmayan: gpedit.msc penceresinin kendisi (ekranda açılmadı); Home sürümünde Registry.pol'ün işlenmesi (kayıt
değerleri zaten yazılı: ilke yine geçerli).

## D-153 — Kaldırılan uygulamanın bıraktığı boşluk aynı kuyruk düzenlemesinde kapanır (2026-11-24)
Bağlam: Sahibinin seçtiği 3. madde (2026-10-07'den beri "Removing an app fixes what pointed at it" diye bekliyordu).
Windows 11'de Print Screen Ekran Alıntısı Aracı'nı açar: araç kaldırılınca tuş hiçbir şey yapmaz. Fotoğraflar
kaldırılınca resimler açılacak uygulama sormadan açılmaz; Not Defteri (Store) kaldırılınca .txt'nin varsayılanı gider.
Düzeltmeler Ayarlar'da zaten vardı ama kaldırmaya bağlı değildi.
Karar:
- **`QueueCompanion` (AppState):** `queue` / `queueMany` / `unqueue` / `unqueueMany` / `unqueueIf` düzenlemeyi
  `ChangeSet` grubunun içinde yapar, sonra eşlikçi `reconcile(önceki, şimdiki)` aynı grupta kuyruğu düzenler → kaldırma
  ve düzeltmesi tek Geri al adımı. Geri al / Yinele / geri yüklenen kuyruk eşlikçiyi çalıştırmaz (bütün eski kuyruğu,
  düzeltmesiyle getirirler). Shell kurar (`RemovalGaps`), yıkılırken bırakır.
- **`RemovalGaps`:** RemoveAppx hedefinin kimlik adına göre (`Microsoft.ScreenSketch_…` → `Microsoft.ScreenSketch`):
  - Ekran Alıntısı Aracı → Ayarlar `print-screen` = "Ekranı panoya kopyala";
  - Fotoğraflar → Ayarlar `photo-viewer` açık + varsayılan uygulamalar .bmp .dib .gif .jfif .jpe .jpeg .jpg .png →
    `PhotoViewer.FileAssoc.*` (Uygulamalar › Varsayılanlar'ın `SetDefaultApps` XML'ine birleştirilir);
  - Not Defteri → .txt → `txtfile` (klasik Not Defteri), yalnız imajda `Windows\System32\notepad.exe` varsa.
  Yalnız boş yuvalar doldurulur: kullanıcının aynı ayar için seçtiği kalır. Kaldırma kuyruktan çıkınca düzeltme de çıkar
  (değeri hâlâ düzeltmeninkiyse; ilişkilendirmelerde yalnız düzeltmenin girdileri, kullanıcınınkiler kalır). Düzeltmeler
  sıradan kuyruk işlemleri: Ayarlar / Uygulamalar'da görünür, değiştirilebilir, ön ayara girer. Terminal kaldırılınca
  düzeltme yok (Windows kendisi konsola döner); Medya Oynatıcı / Filmler ve TV → Windows Media Player şimdilik yok
  (özelliğin durumuna bağlı).
- Bileşenler kartında not: "Kaldırılınca WinLove bunu da yapar (Ayarlar / Uygulamalar'da değiştirilebilir):" + satırlar.
Kanıt: `tests/app/RemovalGapsTests.cpp` (4 vaka: tek Geri al / Yinele, kaldırma çıkınca düzeltme çıkar; kullanıcının
seçimi kalır; Fotoğraflar 8 ilişkilendirme, kullanıcının .pdf'i kalır; Not Defteri dosyasız / dosyalı; boşluksuz uygulama).
VM kurulumu (2026-11-24, `tools\lab_vm.ps1 -Tag d156`, install.wim sürüm 4 Pro 26200.8037, ağsız; değişiklik kümesini uygulamanın kendi denetleyicileri üretti: `tests/app/LabChangesetTests.cpp`, 49 adım `wlcli apply` ile 0 hata; ilk oturumda `tools\vm_check_policies_menu.ps1`, 38 denetim geçti): üç uygulama kaldırıldı, düzeltmeleri kuyruğun kendisi ekledi; kurulan Windows'ta
PrintScreenKeyForSnippingEnabled = 0, .jpg / .png / .gif / .bmp Fotoğraf Görüntüleyicisi ile açılır (AssocQueryString),
.txt klasik Not Defteri ile (Windows ProgId'i `txtfilelegacy` olarak çözer, komut `system32\NOTEPAD.EXE %1`).

## D-152 — Her sayfada Sıfırla; Ayarlar'da sekme başına; Ön Ayar'da "Tüm değişiklikleri sıfırla" (2026-11-24)
Bağlam: Bir kullanıcı (sahibinin aktardığı): ayarları yaptıktan sonra daha önce kaydedilmiş başka bir ön ayarı uygulamak
istiyor, ama ön ayar kuyruğun **üstüne** ekleniyor (`PresetController::apply` → `queueMany`); önce her şeyi tek tek
kaldırmak gerekiyor. Sahibi: Özellikler ve Bileşenler'deki Sıfırla gibi Ayarlar'a (Sistem, Güvenlik … her biri için ayrı),
Zamanlanmış Görevler, Kurulum Sonrası, Sürücüler, Kayıt Defteri ve Güncellemeler'e de Sıfırla. Ayrıca bulunan:
`AppState::unqueueIf` işlemleri tek tek siliyordu → her silme ayrı geri alma adımıydı; Bileşenler / Özellikler /
Servisler'in Sıfırla'sından sonra Geri al yalnız son satırı geri getiriyordu.
Karar:
- **Kural:** bir sayfanın Sıfırla'sı yalnız o sayfanın kuyruktaki değişikliklerini çıkarır, tek geri alma adımıyla;
  listeler (taranan sürücü klasörleri, içe aktarılan .reg girdileri) yerinde kalır, işaretsiz. Şeritte en sağda, ayraçtan
  sonra `Sıfırla` (`Shell::addResetAction`, `common.resetAction`, `Icon::Refresh`).
  - Zamanlanmış Görevler: `SetTaskState` + `CreateTask` (imajın önceki koşudan gelenleri yine görünür).
  - Kurulum Sonrası: planın adımları; Programlar sayfasının seçimleri ve planın seçenekleri kalır (plan boşalırsa işlem
    kuyruktan çıkar).
  - Sürücüler: `AddDriver` + `RemoveDriver`; boot.wim işaretleri de kalkar (o liste kuyrukta değil: Geri al onu getirmez).
  - Kayıt Defteri: her girdi kapatılır (değerleri kuyruktan çıkar), girdiler listede kalır → Geri al hepsini yeniden açar.
  - Güncellemeler: `AddPackage` (dil paketleri hariç: onlar Diller'in).
- **Ayarlar:** şeritteki `Sıfırla` → `Tümünü sıfırla`; süzgeç satırının sağında `Bu sekmeyi sıfırla` (yalnız gösterilen
  sekmenin ayarları; arama / "Yalnızca değişenler" / şablon süzgeci açıkken `Gösterilenleri sıfırla`: süzgecin bıraktığı
  satırlar). Sıfırlanacak bir şey yoksa pasif. "N ayar değişecek" düğmenin soluna kayar, sığmazsa çizilmez; 1100 px'lik en
  dar pencerede şablon süzgeci 140'a kadar daralır. Motor: `ImageSettingsController::resetSettings(liste)`,
  `settingsOfTab(tab)`; `resetChanges()` artık hepsiyle `resetSettings`.
- **Ön Ayar:** şeritte `Tüm değişiklikleri sıfırla`: bütün kuyruk + boot.wim işaretleri, "Geri al"lı bildirim ("{n}
  değişiklik kuyruktan çıkarıldı"); kuyruk boşsa "Kuyrukta değişiklik yok"; Uygula sürerken hiçbir şey yapmaz. Yanıt
  dosyası (Katılımsız) dokunulmaz. Ön ayarın kendisi yine üstüne ekler: sıfırla + uygula iki tık, soru yok.
- **`unqueueIf`:** çıkarılacakları toplayıp `ChangeSet::removeAll` ile tek adımda (bütün mevcut Sıfırla'lar da düzeldi).
Kanıt: `tests/app/ResetTests.cpp` (6 vaka: her sayfa yalnız kendininkini çıkarır, başka sayfanın işlemi kalır; Görevler
ve Kayıt Defteri'nde tek Geri al hepsini getirir; dil paketi kalır; bir Ayarlar sekmesi yalnız kendini sıfırlar, sekmeler
bütün ayarları kapsar). `--render` (koyu, TR / EN): altı şeridin ucu; Ayarlar normal / süzgeçli ("Gösterilenleri
sıfırla") / düğmeye tıklanmış (Gizlilik rozeti kalkar, satırlar Windows varsayılanı, düğme pasif) / 1100 px.
Doğrulanmayan: canlı pencerede tıklama.

## D-150 — Kullanıcı raporu 2026-11-13 (Windows 10 Home 19045.7807): pending.xml'de reddedilen WinSxS küçültme kaydı bekletmez; günlükte yapı damgası (2026-11-24)
Bağlam: Sahibinin aktardığı rapor (`WinLove-apply.log`, `WinLove-rapor-20261113-1024.html`, ön ayarı; 871 adım, Windows 10
Home 22H2 19045.7807 tr-TR, kullanıcının Windows 10'undaki DISM 10.0.19041.3636). Sonuç "İmaj kaydedilmedi": 3 bekleten
hata (+ 1 bekletmeyen):
- `media-streaming` (adım 28) ve `help` (51): her birinde `…-merged-Package` kalktıktan hemen sonra kardeş `…-Package`
  `0x800703EE`. D-127'nin deseni. Günlükte D-127'nin "written out of the WIM; once more" satırı **yok**; yayındaki
  `v1.3.0-beta` ekinde (2026-11-09 11:37Z yüklenen, SHA-256 `32a1e589…`) dize var, ilk ekte (08:15Z, D-127'den önce) yok
  → kullanıcı ilk eki kullanmış. Ek aynı etiketle değiştirildiği için ne sürüm adı ne güncelleme denetimi bunu ayırt ediyor.
- ResetBase `0x800F0806` → atlandı, bekletmedi (1.2.3'ten beri).
- `ShrinkStore` (871): `pending.xml` var → `Unsupported`, kodsuz → `unchangedByFailure` false → **bekletti**. Adım hiçbir
  klasöre dokunmadan dönüyordu; D-127 olsa da imaj kaydedilmeyecekti. Hatanın kendisi güncel kodda da vardı.
Karar:
- `shrinkComponentStore`: `pending.xml` reddi ilk iş (ölçümden önce; günlükte ~6 sn idi) ve CBS_E_PENDING `0x800F0806`
  taşır. `unchangedByFailure`: `ShrinkStore` + `0x800F0806` bekletmez (ResetBase'in kuralı). Yarıda kalan küçültme ve
  "servis yığını bulunamadı" bekletmeye devam eder. Raporda nedeni `apply.reason.pending` (metin: "açılan ya da kapatılan
  bir özellik"). ApplyJob'un "saved all the same" satırı küçültmeyi de sayar.
- Uygula günlüğünün ilk satırı: `(WinLove <etiket>, build <PE bağlama damgası>)` (`ApplyController`): aynı etiketle
  yeniden yüklenen bir ek kullanıcı günlüğünden ayırt edilir.
Kanıt:
- Birim testleri: `a WinSxS shrink refused for pending operations is skipped …`, `store shrink: pending servicing is
  refused first …` (klasör yerinde kalır, kod 0x800F0806).
- Yönetici lab'ı `build\lab\w10e3ee` (Windows 10 Home 19041.3803 kopyası, `win10_22h2_tr_consumer.esd` index 4; kullanıcının
  kendi ön ayarı, yeni `wlcli`):
  - A: ön ayarın 9 özellik kapatması tek başına → 9/9 ok, `pending.xml` **oluşmadı** (kullanıcınınki başka bir adımdan).
  - B: 871 adım `--commit` → 0 hata, WinSxS küçültüldü (13.160 klasör, 1,66 GB), kaydedildi; 7 dk. `0x800703EE` çıkmadı.
    DISM imajın kendi servis yığınını yüklüyor ("Offline servicing, using stack version from …mnt\Windows\WinSxS…",
    v10.0.19041.3745).
  - C: önce SSU 19041.7714 (`build\lab\uup\19045.7727-tr-pro`), sonra aynı ön ayar → yeni yığın yüklendi, 872 adım
    0 hata, kaydedildi. `0x800703EE` yine çıkmadı.
**Doğrulanmayan:** `0x800703EE` iki yığında da üretilemedi; D-127'nin yeniden denemesi hâlâ gerçek imajda kanıtlanmadı.
Kullanıcının imajıyla kalan farklar: 7807 toplu güncellemesi entegre medya (aynı paketin birden çok sürümü: `Package
~7802` + `merged ~4355` + `~.1` artıkları; lab'da hepsi 3636), kullanıcının Windows 10'undaki DISM 19041.3636 (lab'da
26100.9818), kendi bilgisayarındaki güvenlik yazılımı. Kesin kanıt: aynı medya (19045.7807 resmi ESD, D-149'un
indiricisiyle ~3,7 GB) ya da kullanıcının güncel ekle yeniden çalıştırması.

## D-151 — Kaynağın çalışma klasörü adı, kurulum medyasının en derin dosyası 260 karakteri aşmayacak kadar kısa (2026-11-24)
Bağlam: Sahibinin aktardığı kullanıcı raporu: UUP'den indirilen ISO açılmıyor, "işlem başarısız (0x00000000) cannot
create destination"; dosya adını kısaltan kullanıcıda düzelmiş. ISO açılırken medya `<workRoot>\work\<ISO adı>\` altına
çıkarılır (`workDirectoryFor`); medyanın en derin dosyası kökün 147 karakter altında
(`sources\replacementmanifests\microsoft-windows-appx-deployment-server\microsoft.windows.secondarytileexperience_…xml`;
25H2, 26H2 ve Windows 10 ISO'larında aynı), kopya önce `<ad>.partial` yazar. UUP / resmi ISO adları ~70–75 karakter
(`26200.9818.261025-0959.25H2_GE_RELEASE_SVC_PROD3_CLIENTMULTI_X64FRE_TR-TR`): `C:\Users\<ad>\AppData\Local\WinLove\work`
altında ~270 karakter. Manifest `longPathAware` diyor, ama Windows bunu yalnız `LongPathsEnabled = 1` iken geçirir
(varsayılan kapalı; geliştirme makinesinde açık, bu yüzden burada hiç görülmedi). Hata kodu yoktu, ekranda 0x00000000.
Karar:
- `AppSettings::workFolderFor(root, source)` (saf): ad + en derin dosya (147 + `.partial` + 16 pay) 259'a sığıyorsa ad
  aynen; sığmıyorsa ad kesilir (sondaki `.`, boşluk, `-`, `_` atılır) ve `~` + tam adın küçük harfle FNV-1a'sı (8 onaltılı)
  eklenir: aynı ad hep aynı klasör, kesimi aynı iki ad ayrı. Örnek: `19045.7807.261013-0444.22H2_RELEASE~xxxxxxxx`.
- `workDirectoryFor`: uzun yollar bu süreçte açıksa (`RtlAreLongPathsEnabled`) ve tam adla bir klasör zaten varsa o
  kullanılır (o bilgisayarda çıkarılmış kopya yeniden çıkarılmaz); yoksa `workFolderFor`. Uzun yolsuz bir bilgisayarda
  önceki başarısız denemenin bıraktığı tam adlı klasör kullanılmaz (içindeki kısmi kopya işe yaramaz).
- `UdfImage::copyNode`: hedef açılamazsa Windows'un kendi kodu (`HRESULT_FROM_WIN32(GetLastError())`) ve yol 259'u
  aşıyorsa "the path is N characters long" yazılır; artık 0x00000000 yok.
Kanıt: birim testi (`settings: a source's work folder leaves the setup media's deepest file under 260 characters`):
UUP adı kesilir, en derin dosya 259'a sığar, büyük/küçük harf aynı etiketi verir, başka ad başka klasör, kısaltılmış
klasör hâlâ `isWorkCopy`. **Doğrulanmayan:** hata bu makinede üretilemedi (`LongPathsEnabled = 1`, wlcli de aynı
manifestle); `LongPathsEnabled = 0` bir bilgisayarda (ya da VM'de) UUP adlı bir ISO'nun açılması denenmedi.

## D-148 — Sahibinin bildirdiği dört hata: başka araçla bağlanmış imajı devralma, işlem kelimeleri, Kuyruk çipleri, durdurulan Uygula'nın kapanışı (2026-11-11; D-064'ü tamamlar)
Bağlam: Sahibinin 2026-11-11 tarihli 9 maddelik listesinin 1, 2, 3 ve 8. maddeleri (kendi günlüğü
`WinLove-20261111-091520.log` ve ekran görüntüleriyle). Dört bağımsız hata; kural 6 gereği 1. madde önce wlcli + yönetici
lab'ında. (Listenin 4 → D-142, 9 → D-143, 6 → D-144, 7 → D-145, 5 → D-146; başlık yazısı D-147.)
Karar:
1. **Bağlı imajı devralma** (Kaynak seç › "Bu bilgisayarda bağlı imajlar": başka bir aracın bağladığı imaj açılmıyor,
   şerit "%0"da kalıyordu).
   - Neden: DISM ve wimgapi `\\?\` ile bağlanmış bir imajı yine `\\?\` yollarıyla bildirir. `sourceForMountedImage` üst
     klasörün adı "sources" diye büyükbabayı kurulum klasörü saydı (kurulum dosyasına bakmadan); `openSource(klasör)`
     adayı `klasör / "sources/install.wim"` diye kurdu ve `\\?\` önekli yolda `/` ayırıcıya çevrilmez → "folder has no
     sources\install.wim|esd|swm". Devralma ilerlemesiz bir iş olarak "Bağlanıyor %0" gösterdi, DISM'in istediği
     `DismRemountImage` (~4 sn) da ilerlemesizdi; sonra kaynak okuyucu kuyruğunda 12 sn bekledi: her Kaynak ziyaretinde
     yenilenen bağlı imaj listesi sağlıksız her bağlama için `blockersOf` (klasörü gezip Restart Manager'a sorma) çalıştırıyordu.
   - `base/Path.h` `withoutLongPathPrefix`: `\\?\` / `\\?\UNC\` öneki sınırda atılır (düz yol 247 karakteri aşarsa kalır):
     `Dism::mounts()`, `wimMountedImages()` ve `devicePath` (kovan listesi). Klasöre yol eklemek `nativePath` ile.
   - `core::sourceForMountedImage` (Source.h): kurulum klasörü yalnız imaj gerçekten kurulum ortamının install imajıysa
     (`<k>\sources\install.wim|esd|swm` + `<k>\setup.exe` ya da `sources\boot.wim`); değilse WIM dosyasının kendisi.
     `openMountedImageSource` klasör okunamazsa WIM'e düşer.
   - Devralma tek motor işi (`ImageController::adoptMount`, `EngineOperation::Kind::Adopting`): DISM kaydı → saf
     `core::adoptionStep` (Ok → kullan; NeedsRemount → `DismRemountImage`, sonra kullan; Invalid / imaj yok / kalıntı /
     boş → **hiçbir şeye dokunmadan** red) → kaynağı oku → `Events::adopted` (kaynak + bağlı imaj; okuyucuya ikinci okuma
     yok). Yeniden bağlarken iş `Remounting`. İlerlemesi olmayan iki iş durum çubuğunda yüzdesiz (`StatusBar::setTask` < 0),
     satırın Durum hücresi "Devralınıyor / Yeniden bağlanıyor". Her hata işi bitirir; gerekçe Kaynak'taki hata çubuğunda ve
     bildirimde (`Failure::Adopt`; geçersiz / imaj yok / bağlı değil ayrı metin), liste yeniden okunur.
   - Bağlı imaj listeleri (`inspectMounts`, `inspectMountsFast`) artık `blockersOf` çalıştırmaz; tarama yalnız tek
     klasörün karar anında (ör. WinLove'un kendi bağlama klasörü).
   - **Başka araca ait bağlama** (`AppState::mountedElsewhere()`: WinLove'un bağlama / eski / VHD klasörü değil):
     Sürümler'de bilgi çubuğu ("Başka bir araçla bağlanmış imaj … Kaydet ve çöz yalnızca bu bağlamaya işler"), Özet'te
     "Başka bir araçla bağlı · <klasör>", çözme sorusunda ek paragraf. Kaydet / çöz yalnız o klasörü işler; o aracın WIM'i
     kayıttan sonra yeniden yazılmaz (`optimizeWim` atlanır, Uygula'da da); diğer sürümlere uygulama (D-055) o klasörde
     sunulmaz.
   - wlcli: `adopt <klasör> [--json]` ve `mounts --fast [--json]` (wimgapi listesi + ham `WIM_MOUNT_FLAG_*`).
   - Referans aracın ikilisine bakıldı: paketlenmiş, dizgilerde DISM / wimgapi bağlama adları yok; çalışan sürecine
     bağlanmak sahibinin bağlamasına dokunma riski taşıdığı için yapılmadı. Bu yüzden belgelenmiş DISM sırası izlendi
     (ENGINE saha notu); Invalid için yıkıcı bir şey önerilmez.
2. **Sürümler'in Durum sütunu** (silinen sürüm "Bağlanıyor…" diyordu): ISO kaynakta silme önce çalışma kopyasını hazırlar
   (`Preparing`) ve sayfa `Mounting || Preparing` için "Bağlanıyor" yazıyordu; silmenin kendisi "—" gösteriyordu.
   `EngineOperation` → `purpose` (kopya adımı neyin için) ve `indexes` (işin sürümleri); yeni türler `Duplicating`,
   `Moving` (önceki "Exporting"), `Adopting`, `Remounting`. `operationRowWord`: Bağlanıyor · Siliniyor · Dışa aktarılıyor ·
   Adlandırılıyor · Çözülüyor · Kopyalanıyor · Taşınıyor · Hazırlanıyor · Okunuyor · Denetleniyor · Doğrulanıyor.
   `EditionTable::setWorkingRows` işin bütün satırlarına yazar; iş bitince satırlar eski hâline döner (Bağlı / Bağlanamadı /
   —); "Bağlanamadı" yalnız bir bağlama (ya da bağlama için kopya) başarısızsa. Durum sütunu 76 → 116 px.
3. **Özet › Kuyruk** ("2 değişiklik" ama sol taraf boş, tam vurgu çubuk, bozuk risk simgesi): kart gezinti rozetlerinin
   sayfa birimindeki sayılarını gösteriyordu; karşılama açıkken her bağlamada giren iki `WriteFile` (D-084) ve sürüm
   yükseltmesi (`SetEdition`) hiçbir grupta sayılmıyordu. Boyut değişmeyince çubuk tamamen vurguyla doluyordu; "Yüksek
   riskli işlem yok" tek çubuklu `RiskLow` glifini üçüncül renkte çiziyordu. Şimdi `pages/overview/QueueChips`: her işlem
   onu kuyruğa koyan sayfaya (`queuePageOf`: türe göre; paylaşılan türlerde sayfaların sahiplendiği yuvalar — Kayıt
   Defteri'nin kendi girdileri, karşılamanın dosyaları, küçültmenin güncelleme ilkesi; sahipsiz kayıt / dosya Ayarlar'ın)
   ve o sayfanın sekmesine sayılır; Görüntü (Başlat) ve yeni "Kurulum › Katılımsız" grubu dahil, hiçbir sekmede olmayan
   "Diğer"e. Çiplerin toplamı = "N değişiklik". Sol tarafta bir şey yoksa sağ sütun kartın tamamını alır. Boyut
   değişmiyorsa çubuk yalnız nötr iz (`paintSizeBar`; Uygula'nın plan kartında da). Risk satırı: yoksa başarı renginde
   `SuccessCircle` + ikincil metin, varsa `RiskHigh` hata renginde. Gezinti ve kutucuk rozetleri eskisi gibi sayfanın
   kendi biriminde (dil, ayar…); kartın çipleri işlem sayısı.
4. **Durdurulan Uygula'dan sonra kaydet / çöz** (sayfa çözüldükten sonra da "Durduruldu" diyordu): sayfanın modu yalnız
   `ApplyRun`'dan ve bağlı imajdan türüyordu; çözme ayrı bir motor işi (`Unmounting`) ve `ApplyRun`'a bir şey yazmıyordu.
   `ApplyRun::closing` {commit, Running / Done / Failed, hata} ve `ApplyRun::mountSerial`; `AppState::runLeftImageMounted /
   beginRunClose / endRunClose` (yalnız bağlı imaj çalıştırmanın bıraktığıysa — kaydetmiş çalıştırmada ya da sonradan
   bağlanan imajda devreye girmez). `ImageController::unmount` (WIM ve VHD) başında `beginRunClose`, sonunda `endRunClose`
   (hata → Failed + gerekçe; iptal → unutulur); kayıtta kaynak yeniden okunur (`sizeAfter`). `RunSummary::Close` (Saving /
   Discarding / Saved / Discarded / SaveFailed / DiscardFailed); `ApplyPage::modeKey` kapanışı da içerir → Shell sayfayı
   yeniden kurar. Sonuç kartı: "İmaj kaydediliyor… / Değişiklikler atılıyor…" + motorun yüzdesiyle ölçer → "Kaydedildi ve
   çözüldü" (sıradaki: ISO) / "İmaj çözüldü, değişiklikler atıldı" (sıradaki: Görüntü'ye git) / "Kaydedilemedi /
   Çözülemedi" + gerekçe ("İmaj hâlâ bağlı; yeniden deneyebilirsin", Kaydet…). Özet'in çalışma kartı da aynısını söyler;
   kapanış sürerken sıradaki adım "Bekle".
- Render: `--demo-mounts=adopting|remounting|failed|adopted|overview|unmount`,
  `--demo-apply=stopped|discarding|discarded|saving|saved|closefailed`,
  `--operation=delete|export|rename|duplicate|move|unmount|prepare-delete [--select=…]` (kaynakla),
  `--demo-unattended=welcome --demo-overview=unread --page=overview`; hepsi render_diff envanterinde.
Sonuç:
- Doğrulanan (birim): `tests/app/MountAdoptionTests.cpp` (Ok / NeedsRemount / Invalid / imaj yok / kalıntı; `\\?\` ve UNC
  öneki; "Sources" büyük harf; kurulum klasörü dışındaki WIM; setup.exe / boot.wim ile kurulum klasörü; `\\?\` altındaki
  bozuk install.wim'in artık "folder has no sources" vermemesi; WinLove'un kendi klasörü / başkasınınki),
  `tests/app/WorkStateTests.cpp` ("editions: each long work…"), Özet çipleri (her `OpKind`'dan bir işlemle toplam = kuyruk;
  karşılamanın iki dosyası → Kurulum › Katılımsız; paylaşılan türler), Uygula kapanışı (durduruldu → atılıyor → atıldı;
  → kaydediliyor → kaydedildi + sıradaki ISO; hata gerekçesi; iptal; kaydetmiş çalıştırmada ve sonradan bağlanan imajda
  devreye girmemesi; sayfa başlıkları her adımda).
- Doğrulanan (yönetici lab'ı, iki kez; `build\lab\fixes\lab-adopt.ps1`, günlükler `lab-adopt-run1.log`, `lab-adopt.log`):
  lab WIM'inin kopyası çevresinde kurulum dosyası olmayan bir `…\Sources\install.wim`; dism.exe ile `\\?\` yollarıyla index
  4 bağlandı → `wlcli mounts --json` düz yollar, `--fast` bayraklar 257 (0x101 MOUNTED | READWRITE) → `wlcli adopt
  \\?\…\mnt`: "ok -> use", kaynak WIM'in kendisi (6 sürüm), "Windows 11 Pro", DISM oturumu ~0,7–2 sn → boş klasör
  reddedildi ("not an image", dokunulmadı) → özellikler 137, provisioned appx 47 okundu → tek kayıt değeri uygulandı ve
  `reg-check` ile imajda okundu → `wlcli unmount <düz yol> --discard` → bağlama tablosu başlangıçtakiyle aynı. Bu, D-064'ün
  "görülmeyen"ini ve `inspectMountsFast`'ı (D-136 eki) gerçek bir bağlamada motor yolundan kanıtlar. Sahibinin bağlama
  tablosu oturum boyunca değişmedi (yalnız referans aracın kendi bağlaması: index 2, RW, Ok).
- Render önce / sonra `build\lab\fixes\final\` (b1 devralma: liste, devralınıyor, yeniden bağlanıyor, reddedildi,
  Sürümler, Özet, çözme sorusu; b2 Durum sütunu "Bağlanıyor" → "Siliniyor" + kopya, iki sürüm silme, dışa aktarma,
  yeniden adlandırma; b3 Kuyruk: "Kurulum · Katılımsız 2", nötr çubuk, ✓ simgesi, zengin kuyruk; b4 kapanış: durduruldu,
  atılıyor %40, atıldı, kaydediliyor, kaydedildi, çözülemedi, Özet).
- Doğrulanmayan: NeedsRemount'un gerçek imajda devralınması (üretmek WIM filtresini koparmayı — `fltmc unload wimmount`,
  wimserv'i durdurmak — gerektirir, sahibinin diğer aracının bağlamasını da etkilerdi; yalnız birim testi); devralmanın,
  ISO kaynakta silmenin ve gerçek bir Uygula'yı durdurup çözmenin pencereli uygulamada izlenmesi (pencere açılmadı; motor
  yolu wlcli ile, durum geçişleri birim testinde, gösterim render ile; çözme ve silme motoru değişmedi).

## D-147 — Başlık çubuğu yalnız "WinLove" der, markanın yazı tipiyle (2026-11-11; D-133'ün "başlıkta imajın adı"nı geçersiz kılar)
Bağlam: Sahibi: başlık "daha efektif, güzel, farklı fontta" olsun; ISO / WIM adı gibi bilgiler orada görünmesin.
Karar:
- Ortadaki başlık her zaman uygulamanın adı (`Str::AppName`); açık imajın adı / dosyası (D-133, "Windows 11 Pro ·
  install.wim") kaldırıldı — imaj durum çubuğunda ve sayfalarda zaten adlanıyor. `windowTitle()`, `TitleBar::setTitle()`
  ve `title.image` metni (tr / en) silindi. İşletim sisteminin pencere başlığı (görev çubuğu / Alt-Tab) zaten sabit
  "WinLove" idi, değişmedi.
- Yazı: markanın kendi yazı tipi (`WinLove-UI-Handoff/00_brand/brand-guidelines.md`: "Wordmark: IBM Plex Sans"), yalnız
  SemiBold, OFL 1.1: `resources/fonts/IBMPlexSans-SemiBold.ttf` + `IBMPlexSans-OFL.txt`, exe'ye RCDATA olarak gömülü
  (diğer fontlar gibi). Arayüz yazı tipi ayarı (Inter / Geist / Segoe UI Variable) markayı etkilemez. Hakkında'nın fontlar
  ve lisans metni ile README'nin lisans satırı IBM Plex Sans'ı da sayar.
- Tokenlar (tokens.json 5.0.0, sürüm artırılmadı): `typography.family.brand` / `brandFallback` (Segoe UI),
  `typography.styles.wordmark` (15 / 20, 600, harf aralığı +0,2); `FontRole::Brand` (gen_tokens.py), `FontFamilies.brand`.
- Renk: "Win" metin rengi, "Love" vurgunun okunur tonu (`accent.text`: koyu #74A3EA, açık #2462C7; özel vurguda onun tonu,
  D-143). Pencere etkin değilken tek renk, soluk (text.tertiary, eskisi gibi). Yüksek karşıtlıkta (uygulama ya da Windows)
  yalnız metin rengi. Sığmayan dar pencerede tek renk + "…" (eskisi gibi). Ayrım noktası koddaki sabit "Love" değil,
  adın ikinci büyük harfi.
- D-109 / D-136'dan sapma: başlıkta arayüz yazı tipi dışında bir yazı tipi (yalnız marka için).
Sonuç:
- Doğrulanan: Debug `build.ps1 -Test` geçti; Release `-Test` ikinci koşuda geçti (ilk koşu 0xC0000374 ile düştü — STATUS
  "Bilinen"; bu değişiklikle ilişkisi ne kanıtlandı ne dışlandı: temel sürümle karşılaştırma yapılmadı).
  `tests/app/ShellPolishTests.cpp`, `tests/support/TestGraphics.cpp` güncellendi. Render `build\lab\wordmark\final\`
  (Özet koyu / açık / saf siyah / yüksek karşıtlık, bağlı demo imajla; Kaynak koyu; 2× başlık ve büyütülmüş marka
  kırpımları): bağlı imajda başlık yalnız "WinLove".
- Doğrulanmayan: canlı pencerede etkin / etkin değil geçişi; Windows'un yüksek karşıtlık temaları.

## D-146 — Ayarlar'a hazır şablonlar: Şablon ▾ (2026-11-11; "Önerilenleri uygula"yı ve ayar başına `recommended`'ı kaldırır)
Bağlam: Sahibinin listesi madde 5: Ayarlar (imaj) sayfasına Bileşenler'deki gibi hazır şablonlar. Bileşenler'in
şablonları (`Tier`: Gizlilik ⊂ Genel ⊂ Hafif; `ComponentController::applyTemplate`) yalnız RemoveAppx / RemoveComponent
ekler, kuyruktakini ve uyumluluk korumasının tuttuğunu atlar, hiçbir şeyi çıkarmaz; geri alınabilir toast, satırda şablon
rengi, "Tümü ▾" süzgeci. Ayar, servis, görev kuyruğa girmez. Ayarlar'da tek düğme vardı: "Önerilenleri uygula" (49 ayarın
`recommended` işareti); kullanıcının seçimini eziyordu ve küme herkes için güvenli değildi (workplace-join,
removable-twice, wpbt, game-dvr ilkesi…).
Karar:
- **Veri:** `resources/catalog/settings.json` sonunda `"templates"`: `{id, tr, en, desc_tr, desc_en, settings: {ayar id:
  seçenek id}}` (aç / kapa için "on" / "off"). Bilinmeyen ayar / seçenek, Windows varsayılanı ya da metin / dosya ayarı
  içeren seçim yüklenirken atlanıp loglanır; seçimi kalmayan ya da kimliği tekrarlanan şablon atlanır. Ayar başına
  `recommended` işaretleri (49 satır) kaldırıldı.
- **Model:** `ImageSettingTemplate` (`ImageSettingsCatalog.h`), `ImageSettingsController::applyTemplate / previewTemplate /
  templateOption / resetChanges`; `applyRecommended` kaldırıldı.
- **Etkileşim (Bileşenler gibi "ekler, sıfırlamaz"):** şablon, imajdaki hâlinde duran her ayarını kendi seçeneğine alır;
  kuyrukta zaten değiştirilmiş ayara (kullanıcının ya da önceki bir şablonun) dokunmaz. İmajın kendi değeri (D-045) seçim
  sayılmaz, şablon onu değiştirir. Bir şablon = tek kuyruk düzenlemesi = tek geri alma adımı. Şablonlar birbiriyle
  çelişmez (ortak ayara aynı seçenek; birim testi) → hangi sırayla uygulanırsa uygulansın birleşimleri.
- **Arayüz (Bileşenler'in düzeni):** şerit `Bu bilgisayardan al · Şablon ▾ | Geri al · Yinele | Sıfırla`. Toast: "{n} ayar
  şablona göre ayarlandı (Gizlilik)" + şablonun açıklaması + "{k} ayar senin seçtiğin gibi bırakıldı." + "Orta riskli: …"
  (değişen orta riskli ayarların adları) + "Geri al". Süzgeç satırında "Tümü ▾ / Şablon (…)": şablonun ayarları bütün
  sekmelerden ("Sekme › Bölüm"), satırda "şablonda: Kapalı", formun üstünde bilgi çubuğu (ad, kime göre, "{n} ayar · {k}
  tanesi değişecek", **Şablonu uygula**, kapat = Tümü). Uygulanınca satırlar her zamanki "değiştirilecek" vurgusu, sekme ve
  gezinti rozetleri. **Sıfırla:** formun bütün ayarlarını imajdaki hâline döndürür (yazılan metin / resim de), tek adım, onay
  yok (Bileşenler'inki gibi); durum kuyruktan türediği için Servisler'de / Kayıt Defteri'nde aynı değeri yazan işlem de
  çıkar (P12'nin bilinen modeli).
- **Altı şablon** (ayar sayısı): **Dengeli** 24 — herkes için: reklam, öneri, gereksiz veri paylaşımı kapanır, hiçbir
  Windows özelliği bozulmaz (yalnız düşük riskli kayıt değerleri; servis / dosya yok); **Gizlilik** 59 — Dengeli'nin veri /
  reklam kısmı + telemetri servisleri, yazma / konuşma verisi, eşitleme, uygulama izinleri, aramada web / geçmiş, Copilot,
  Recall ve uygulamalardaki yapay zekâ, uygulama telemetrileri (tek orta riskli: `diagtrack-service`); **Sade ve sessiz**
  29 — reklamsız, önerisiz, ipucusuz Başlat / görev çubuğu / kilit ekranı (tema, hizalama gibi zevk ayarlarına dokunmaz);
  **Performans** 12 — başlangıç gecikmesi, menü gecikmesi, NTFS son erişim, bakım uyandırma, görsel efektler…; **Oyun** 12
  — ön plan önceliği, GPU zamanlaması, pencereli oyun iyileştirmeleri, MMCSS, ham fare, oyunu bölen kısayollar; **İş
  bilgisayarı** 26 — tüketici içeriği kapanır, ağ ve oturum sıkılaşır (restrict-anonymous, LLMNR), etki alanı / yönetim /
  yazdırma çalışır, telemetri kuruluşa kalır. **Hiçbir şablonda:** yüksek riskli ayar, geri dönüşü olmayan servis / kayıt
  silme seçeneği, Windows Update / Mağaza güncellemesi / Defender / SmartScreen / UAC / ağ / yazdırma / erişilebilirlik /
  oturum / kamera-mikrofon / sayfa dosyası (testteki `never` listesi). "Herkes için bozmaz" kuralına uymadığı için bilerek
  dışarıda kalanlar: Dengeli'de web-search, widgets, onedrive-kfm, workplace-join, removable-twice, folder-type-discovery,
  game-dvr, wpbt / vendor-apps; Gizlilik'te location, find-my-device, camera / microphone, tarayıcı telemetrisi; Oyun'da
  memory-integrity / vbs, xbox-services, game-bar / game-mode. Ayar listeleri katalogda; ayar ayar gerekçe tablosu
  `build\lab\tweakpresets\NOTES.md` §3'te (depoda değil).
- `tools/lab_settings_d067.ps1` 93. satır `recommended`'a bakıyordu; artık açılır listede hep son seçeneği dener (lab yine
  her ayarı varsayılan dışı hâliyle uygular).
- Render: `--demo-tweaks --demo-tweak-template=balanced,privacy` (sırayla uygular, son toast kalır),
  `--demo-tweak-filter=<id>` (süzgeç o şablonda); menü açık kare `--click-at` ile.
- Yapılmayan öneriler (sayfa dışı): Gizlilik için telemetri görevleri, WerSvc / lfsvc / MapsBroker / RetailDemo servisleri,
  Hosts'un telemetri listesi, Bileşenler'in Gizlilik şablonu; Performans / Oyun için güç planı; İş için RemoteRegistry.
  `templates` yapısı ileride görev / servis / hosts kimliklerine genişletilebilir (çapraz sayfa şablonu).
Sonuç:
- Doğrulanan: Debug + Release `build.ps1 -Test`: 644 vaka / 22.613 denetim (10 atlanan); D-146'nın 8 vakası 2.813 denetim
  (çelişmezlik, `never` listesi, ekler / sıfırlamaz, geri alma, Sıfırla, yükleme redleri). Render
  `build\lab\tweakpresets\final\` (koyu / açık × tr / en: temel, Dengeli + Gizlilik, Oyun, süzgeç; menü açık koyu tr ve açık
  en). render_diff (önce = `build\lab\autoiso-opts\before\WinLove.exe`): 336 aynı / 14 farklı; bu işin farkları Ayarlar'ın
  şeridi ve süzgeç satırı, diğerleri bilinen gürültü (`--demo-icons`, log saati, Uygula çalışıyor) ve D-144 ekinin "ISO
  seçenekleri…" satırı.
- Doğrulanmayan: canlı pencerede menü / toast / Geri al / Sıfırla; şablonların kurulan Windows'taki etkileri (VM yok;
  değerlerin imaja yazılması D-067 lab'ında ayar ayar kanıtlı).
- Bilinen (önceden vardı): alt sekme etiketi "Ayarlar · 6" sayfa açılırken yazılıyor, kuyruk değişince güncellenmiyor
  (Bileşenler'de de aynı).

## D-145 — Güncellemeyi uygulamanın içinden yükle + Markdown'lı Değişiklikler penceresi (2026-11-11; D-140'ın "hiçbir şey indirmez" kuralını yalnız "Yükle"ye basınca gevşetir)
Bağlam: Sahibinin listesi madde 7: "güncelleme uyarısında Değişiklik günlüğünü de releases'den bulsun md düzgün formatta
inceleyebilsin … Yükle ve Değişiklikleri gör butonu olacak; basınca ayrı pencerede sade ve düzenli biçimde listeleyecek …
Markdown destekler şekilde göstersin." Gerçek yayın düzeni (GitHub API, 2026-11-11 11:50, tek GET, belirteçsiz; yanıt
`build\lab\autoupd\releases.json`): 9 yayın, hepsi ön sürüm, taslak yok; **her birinde tek ek** `WinLove.exe`
(`application/x-msdownload`, `state: uploaded`, 6,99 MB … 11,21 MB, `…/releases/download/<etiket>/WinLove.exe`) ve her
ekte GitHub'ın kendi `digest` alanı (`sha256:<64 hex>`); zip / `.sha256` eki / başka mimari yok. Gövdeler Markdown (kalın,
`##` / `###`, iç içe madde, `>` alıntı + emoji, `[Türkçe](#türkçe)` çapaları, satır içi kod).
Karar:
- **Ek seçimi** (`core::pickUpdateAsset`): adı tam `WinLove.exe` (harf duyarsız); yoksa adı `winlove` ile başlayan, `x64` /
  `amd64` içeren, `arm64` içermeyen `.exe`. Ayrıca `state: uploaded`, 1 bayt – 512 MB, adres projenin yayın indirmesi.
  Yoksa yükleme yapılmaz, "İndirme sayfasını aç".
- **İndirme** (`core::downloadUpdate`): `<exe>.new` (exe'nin yanında: aynı birim → takas iki yeniden adlandırma), `.part` +
  kaldığı yerden devam (var olan `httpDownload`, isteğe bağlı `HttpOptions`: `User-Agent: WinLove/1.3.0`, çerez yok).
  Yönlendirmenin vardığı adres `*.githubusercontent.com` (https) ya da projenin indirme yolu değilse bir bayt yazılmadan
  red (`trustedAssetUrl`). Gönderilen: yalnız o GET (+ GitHub'ın gördüğü IP).
- **Denetim** (`core::verifyUpdateFile`, sırayla): boyut = API'nin boyutu; SHA-256 = API'nin `digest`'i (varsa — bugün
  hepsinde var); PE ("MZ" … "PE\0\0", çalıştırılabilir, DLL değil); makine 0x8664 + PE32+; alt sistem 2 (GUI);
  ProductVersion = etiketin sürümü (`parseAppVersion`; "1.3.0 Beta" = "v1.3.0-beta"). Ayrı bir `.sha256` eki indirilmez.
- **Takas** (`core::swapInUpdate`): önceki güncellemeden kalan `<exe>.old` silinir — silinemiyorsa (hâlâ kullanımda) hiçbir
  şey değişmeden durur; `<exe>` → `<exe>.old` (çalışırken izinli); `<exe>.new` → `<exe>`; ikinci adım başarısızsa birincisi
  geri alınır. `core::rollbackUpdate`: yeni exe `<exe>.new`'e, `.old` geri `<exe>`'ye.
- **Akış** (`UpdateController`, kendi iş parçacığında; arayüz ve DISM motoru beklemez): Yükle → redler (bağlı imaj;
  Uygula / ISO / motor işi / indirme sürerken; klasör yazılamıyor) → indiriliyor (kartta ölçer + %, durum çubuğunda
  "Güncelleme indiriliyor · %42", **Vazgeç**) → denetleniyor → takas → **hazır** (bildirim + kartta **Yeniden başlat**;
  durum çubuğu da). Yeniden başlat: yeni exe `CreateProcess` ile (aynı çalışma klasörü, "runas" yok) `--relaunched
  "--updated-from=1.3.0 Beta"` (+ bu süreç yönetici değilse `--no-elevate`: güncelleme yüzünden UAC istenmez), sonra pencere
  `confirmClose` yolundan kapanır. Başlatılamazsa takas geri alınır, kart nedenini söyler. Yeni örnek: tek bildirim "WinLove
  güncellendi: 1.3.0 Beta → x", Hakkında kartı aynı; `<exe>.old` silinir (eski süreç çıkana dek 2 sn arayla 10 deneme; her
  açılışta bir kez daha).
- **Hatalar** kartta ve bildirimde düz sözlerle (`Install::Why`): ek yok / bağlı imaj / iş sürüyor / klasör yazılamıyor /
  indirilemedi (yarım dosya kalır, yeniden deneme kaldığı yerden) / dosya bu sürümün değil (silindi) / yerine konamadı
  (eski exe yerinde) / yeni sürüm başlatılamadı (eski geri kondu) / durduruldu (yarım dosya silindi). Ek yok, klasör,
  denetim, takas, başlatma hatalarında **İndirme sayfasını aç** (yalnız `html_url`, D-140'ın güvenli adres kuralı);
  diğerlerinde **Yeniden dene**.
- **Bildirim:** D-140'ın tek tostu iki düğmeli: **Yükle** (dolu) · **Değişiklikleri gör** (tostun altında kendi satırında;
  tek düğmeli tostlar eskisi gibi). Durum çubuğu bölümüne tıklama bir menü: Yükle · Değişiklikleri gör · Hakkında'da göster
  (indirme sürerken Hakkında, hazırsa yeniden başlatma). **Hakkında kartı:** yeni sürüm varken [Yükle] [Değişiklikleri gör]
  [Şimdi denetle]; "Sürüm sayfasını aç" artık yalnız yüklenemeyen durumda "İndirme sayfasını aç"; güncelken de
  [Değişiklikleri gör].
- **Değişiklikler penceresi** (`App` → `ChangelogWindow`, içerik `app/shell/Changelog` `ChangelogView`): ayrı üst düzey
  pencere, aynı `ui::Window` (kendi sınıfı `WinLove.Changelog` — tek örnek denetimi ana pencereyi sınıfıyla bulur; ana
  pencereye ait: üstünde durur, onunla küçülür; kapanınca ileti döngüsü bitmez — `WindowAppearance::className / owner /
  quitOnClose`, `WindowCallbacks::destroyed`), aynı grafik / tema / vurgu / DPI / yakınlaştırma, kendi başlık çubuğu
  ("WinLove — Değişiklikler" + küçült / büyüt / kapat), yeniden boyutlanır, Esc kapatır, tek pencere (ikinci "Değişiklikleri
  gör" öne getirir), dil / yazı tipi değişince kapanır, tema değişince çerçevesi güncellenir. İçerik: "Değişiklikler", "Bu
  bilgisayarda 1.3.0 Beta · 2 yeni sürüm"; sağda [Sürüm sayfasını aç] [Yükle]; çalışan sürümden yeni her yayın (en yenisi
  önce), sonra "Önceki sürümler" altında son 3 eski yayın (çalışan sürüm "Bu sürüm" etiketiyle); her biri bir kartta: ad,
  "v1.4.0-beta · 1 gün önce yayımlandı", etiketler (Yeni / Bu sürüm / Ön sürüm), gövde Markdown. **Ek istek yok:** gövdeler
  D-140'ın tek yanıtında (`Release::body` ≤ 128 K karakter; `ReleaseCheck::releases` sürüme göre sıralı; `changelogParts`).
- **Markdown:** ayrıştırıcı `base/Markdown` (saf metin; ui ve core kullanabilir), görünüm `ui/widgets/MarkdownView`. ATX ve
  setext başlıklar; yumuşak satır sonu birleşir, iki boşluk / `\` / `<br>` kırar; kalın, italik, ikisi, üstü çizili, `kod`;
  çitli ve girintili kod blokları; iç içe alıntı; iç içe madde / numaralı listeler (gevşek / sıkı, tembel devam); yatay
  çizgi; GitHub tabloları (sığmazsa sütunlar daralır, satırlar sarar); bağlantılar (tıklanınca yalnız `http(s)://`
  tarayıcıda; `#çapa` aynı yayındaki başlığa kayar), `<otomatik>` ve çıplak `https://` / `www.` adresleri; görsel yalnız alt
  metni (hiçbir şey indirilmez); HTML yorumu gizli, diğer HTML metin; yaygın HTML varlıkları. Düşmanca girdide çökmez,
  doğrusal kalır: iç içelik 12'de metne döner, etiket araması 2000 karakter, ters tırnak araması önbellekli, girdi 512 K'da
  kesilir. Metin seçilebilir (sürükle, çift tık paragraf, Ctrl+A / Ctrl+C), tekerlek / klavye ile kayar; düzen `layout()`'ta,
  `paint()` yalnız çizer; renk aralıkları için `Canvas::drawTextLayout` (renkli yazı tipleri renkli).
- **wlcli:** `update-install --exe=<kopya> (--asset=<dosya|https> | --releases=<kayıtlı yanıt>) [--tag=] [--expect=<sha256>]
  [--bytes=] [--fail-at=verify|swap] [--json]` (çıkış 0 yüklendi, 2 indirme / ek yok, 3 dosya bu sürümün değil, 4 takas geri
  alındı) ve `update-rollback --exe=`. Exe'yi asla başlatmaz.
- Render: `--demo-update=downloading|verifying|ready|install-failed|updated`, `--demo-changelog[=<yanıt.json>]` (varsayılan
  `tests\integration\fixtures\changelog-sample.json`: gerçek yanıttan üç yayın + her Markdown yapısını kullanan iki
  uydurma yeni yayın + bir taslak); `--keys=` artık `pgdn,pgup,home,end` da alır; render_diff envanterinde.
- 65 yeni metin anahtarı (`appUpdate.self.*` 30, `changelog.*` 12 ve D-144'ünkiler; tr + en). Yeni token yok, yeni üçüncü
  parti yok. `Shell.cpp` Debug'da COFF bölüm sınırını aştı: yalnız o dosyaya `/bigobj`.
- **Hakkında'daki söz** ("kimseye bir şey göndermez") değiştirilmedi (sahibinin kararı açık, STATUS). Yeni ağ trafiği yalnız
  kullanıcının "Yükle"ye basmasıyla ve yalnız o dosyanın GET'i; telemetri yok.
Sonuç:
- Doğrulanan (birim): `tests/base/MarkdownTests.cpp` (10 vaka: her yapı, vurgu kenar durumları, bozuk bağlantılar,
  varlıklar, CRLF / CR, BOM, sekme, 20 000 sözcüklük satır, 5000 iç içe alıntı, 400 seviye liste, 20 000 `[`, girdi sınırı,
  çapa anahtarları), `tests/core/SelfUpdateTests.cpp` (6: gerçek yanıttaki ekler ve `digest`, gövdeler, ek seçimi ve 10
  kötü adres, derlenen WinLove.exe'nin PE / sürüm bilgisi, boyut / SHA-256 / sürüm / PE değil / ARM64 / konsol programı
  redleri, takas, iki ad arası hata → geri alma, kullanımdaki `.old` → hiçbir şey değişmez, yabancı adrese indirme reddi),
  `tests/app/SelfUpdateAppTests.cpp` (8: akış sahte yükleyiciyle, yeniden başlatma komut satırı, redler, her hata, iptal,
  başlatma hatasında geri alma, hazırken imaj bağlanınca bekleme, Hakkında kartının her hâli, durum çubuğu, pencerenin
  bölümleri + Esc, iki düğmeli tost), `tests/ui/MarkdownViewTests.cpp` (1). Debug + Release `build.ps1 -Test`: 633 vaka /
  20.085 denetim (10 atlanan).
- Doğrulanan (lab, `build\lab\update\selfupdate\lab.ps1` → `lab.log`; yalnız kopyalar, yönetici yok, `dist\` ve çalışan
  WinLove'a dokunulmadı): yerel dosyayla takas (çıkış 0; exe yeni, `.old` eski), `update-rollback` (0; exe yine eski),
  `--fail-at=verify` (3; exe değişmedi, `.new` silindi), etiket / dosya sürüm uyuşmazlığı (3), `--fail-at=swap` (4; exe
  değişmedi, `.new` duruyor), exe **çalışırken** takas ve geri alma (ikisi 0, süreç canlı kaldı), **gerçek ek**: GitHub'dan
  v1.3.0-beta `WinLove.exe`, 11.211.264 bayt, SHA-256 = API'nin `digest`'i, x64 GUI, ProductVersion "1.3.0 Beta"; takas edildi.
- render_diff (`build\lab\autoupd\diff.log`): 286 aynı, 34 farklı, 30 "başarısız" (eski exe'nin tanımadığı yeni bayraklar);
  34 farkın hepsi açıklandı (D-144'ün satırı / bandı, Hakkında, iki düğmeli tost, durum çubuğu menüsü, "Boş alan").
  Görüntüler `build\lab\autoupd\final\` (toast-*, status-*, about-*, changelog-*).
- Doğrulanmayan: canlı pencere hiç açılmadı — Değişiklikler penceresinin gerçek açılışı / taşıma / boyutlandırma / DPI
  değişimi / Esc / tek örnek / sahip pencereyle küçülme / tema değişimi; bağlantının tarayıcıda açılması; farede metin
  seçimi (yalnız simülasyon); iki düğmeli tostun ve durum çubuğu menüsünün tıklanması; uygulamanın içinden gerçek "Yükle" →
  "Yeniden başlat" → `--updated-from` ile açılan yeni örnek ve `.old`'un silinmesi (çekirdek adımlar wlcli lab'ında gerçek
  dosyalarla; `CreateProcess` + pencere kapatma zinciri yalnız sahte yükleyiciyle birim testinde); `Program Files`'ta
  yönetici olarak çalışan WinLove'un güncellenmesi; yazılamayan klasör (yalnız birim testi); proxy / kesilen bağlantı;
  başka sürümden kalmış `.new.part` (bilinen: ilk deneme boyut / SHA-256'da düşer, dosya silinir, ikinci deneme temiz
  başlar); gerçekten daha yeni bir yayın (yok; demo ve fixture ile); ekran okuyucu.

## D-144 — Uygula bitince ISO oluştur; ISO seçenekleri Uygula'dan da seçilir (2026-11-11)
Bağlam: Sahibinin listesi madde 6: "Uygula sayfasında kullanıcı isterse uygula işlemi bitince durum başarılıysa otomatik iso
oluşturmayı da seçebilmeli. Şuan bitmesini bekliyor sonra kullanıcı iso sayfasına girip tekrar iso oluştur demek zorunda
kalıyor." Aynı gün ek: "Uygula sayfasına ISO oluştur ekledin ama kullanıcı ISO oluşturma seçeneklerini seçemiyor." (ilk
hâlde otomatik ISO hep ISO Oluştur'un ilk değerleriyle yapılıyordu; sayfadaki seçimler sayfanın widget'larındaydı ve sayfa
yeniden kurulunca kayboluyordu).
Karar:
- **Yer:** Uygula › özet, "Kaydetmeden önce" ve "Kurulumdan sonra" satırlarının altında üçüncü satır **Bitince**: [ ] **ISO
  oluştur** · yazılacak dosya (`D:\ISO\x64-9448_WinLove.iso`) · **Değiştir…** (dosya kaydetme diyaloğu) · **ISO
  seçenekleri…** (`ButtonKind::Subtle`, `TweaksSliders` simgesi; ikisi seçenek açık ve kullanılabilirken görünür). Altında
  tek satır özet (Caption, text.secondary): "UEFI + BIOS · etiket CPRA_X64FRE_TR-TR_DV9 · SHA-256"; değiştirilmişse ör.
  "Yalnızca UEFI · etiket WINLOVE_TR · ESD · tuş istemi yok · UEFI CA 2023". Yapılamayacak kaynakta seçenek pasif, dosyanın
  yerinde kısa neden (turuncu): "kaynak açık değil", "tek bir WIM / ESD dosyasından ISO yapılmaz", "sanal diskten ISO
  yapılmaz", "yanıt dosyasında Kurulum'un reddedeceği bir değer var". Kural ISO Oluştur'unki: `IsoController::sourceBlocker`
  (yeni; `blocker()` bundan türedi, sırası ve sonucu aynı) — bağlı imaj ve süren Uygula sayılmaz.
- **Hatırlanan:** `AppSettings::isoAfterApply` (settings.json `"isoAfterApply"`, varsayılan kapalı) — aynı satırlardaki
  `pauseBeforeSave` ve `updateGuard` gibi. Ön ayara değil: ön ayar imajın içeriğini taşır; ISO'nun yazılıp yazılmayacağı ve
  nereye yazılacağı bu bilgisayarın iş akışı (yol makineye özgü).
- **Dosya:** klasör ISO Oluştur'unki (`AppSettings::isoFolder`, boşsa Masaüstü), ad ISO Oluştur'un varsayılanı `"<kaynak
  adı>_WinLove.iso"`. "Değiştir…" klasörü `isoFolder`'a yazar (ISO sayfası da onu açar), adı yalnız bu kaynak için bellekte
  tutar. Hiçbir klasör oluşturulmaz (motor seçilen klasöre `.part` → yerine taşıma ile yazar).
- **Ne zaman başlar:** Uygula'nın bitişinde (`ApplyController::Events::finished` — imaj kaydedilip çözüldükten, kaynak
  yeniden okunduktan sonra) **çalıştırma imajı kaydettiyse**: bütün adımlar tamam ya da atlananlarla kaydedilmiş.
  Başlamaz ve nedeni kartta yazar: başlatılamadı / kaydedilemedi ("uygulama tamamlanamadı"), Durdur ya da DISM oturumu
  kaybı ("durduruldu"), bir adım başarısız olup imaj kaydedilmedi (held) ya da duraklatmada Durdur (kept) ("imaj
  kaydedilmedi"), D-055'te diğer sürümlerden biri kaydedilemedi, kayıttan sonra ISO Oluştur reddetti (ör. yanıt dosyası
  bozuldu), D-084 karşılama betiği kurulacak sürümde yok (ISO sayfasının `startIso` denetimi). "Dur: bağlı klasörü elle
  düzenleyeceğim" (D-106) seçiliyken **Kaydet ve devam et** imajı kaydeder → ISO başlar (kullanıcı elle düzenlemeyi bitirip
  kaydetmeyi seçti); duraklatmada **Durdur** → ISO yok, nedeni yazılı. Bu bir yorum; "duraklatma seçiliyse hiç ISO yapma"
  istenirse `autoIsoSkip`'e tek satır (sahibine soru, STATUS).
- **Motor:** tek ISO motoru `IsoController::start` (yeni motor yok). İstek `IsoController::defaultRequest`, aşağıdaki ortak
  seçeneklerden kurulur; otomatik ISO'ya özgü olanlar: `workCopy = Keep` (ISO kaynağında az önce kaydedilen çalışma
  kopyası), önceki kurulum kullanıcı seçmediyse D-074 kuralı motor iş parçacığında, yapımdan hemen önce (`legacySetupAuto`;
  `editionsWithoutWinre`, `mediaOpensPreviousSetup`), `openFolder = false` (uzun bir Uygula'nın sonunda Gezgin
  kendiliğinden açılmaz; kartta **Klasörde göster**), `afterApply = true` (`AppState::IsoRun::afterApply`). Sıkıştırma
  `canRepack` değilse motor "Olduğu gibi"ye düşürür.
- **ISO seçenekleri tek kaynak:** `AppState::IsoChoices` (`isoChoices()` / `setIsoChoices()`, `Change::Iso`): birim etiketi
  (`optional`; yoksa kaynağınki, `IsoController::chosenLabel`), önyükleme modu, install.wim sıkıştırma, "tuşa basın" istemi
  yok, gereksinim atlamaları boot.wim'e, önceki kurulum (`optional`; yoksa D-074 kuralı), Secure Boot 2023, SHA-256,
  "Bitince klasörü aç" (yalnız ISO sayfası). Ortam güncellemesi kutusu paylaşılan `MediaUpdate::enabled`'da kaldı; dosya adı
  / klasör burada değil. **Ömür:** uygulamanın oturumu (sayfa yeniden kurulunca, dil değişince korunur; settings.json'a
  yazılmaz — önceden de yazılmıyordu). Etiket ve önceki kurulum kaynağa aittir: Uygula sonrası aynı kaynağın yeniden
  okunması korur, başka kaynak açılınca ikisi sıfırlanır (`AppState::setSource`); mod, sıkıştırma, kutular kalır.
- **Ortak bileşen** `app/pages/iso/IsoOptions.{h,cpp}`: `IsoOptionControls` denetimleri bir sahibin çocuğu olarak kurar, her
  değişiklik hemen `setIsoChoices`'a, `sync(running)` durumdan geri okur (değer, etkin / pasif, ipuçları; D-074 / D-077 /
  D-080 / D-094 kuralları sayfadan buraya taşındı); ipuçları, ISO sayfası özet kartının üç satırı ve WinRE okuması
  (`editionsWithoutWinre`, okuyucu iş parçacığı) da burada. **ISO sayfası** bu bileşeni kullanır (yerleşim, sıra, klavye
  sırası, çizim aynı); `IsoPage::request()` yine denetimlerden okur. **ISO seçenekleri iletişim kutusu**
  (`makeIsoOptionsDialog`, `Shell::showIsoOptions`): 760 geniş, başlık + bir cümle ("ISO Oluştur sayfasıyla aynı
  ayarlardır… Dosyanın yeri Uygula'daki satırda."), ISO sayfasının satırları (etiket 190 px · denetim · ipucu; D-136
  ölçüleri), değişiklik anında geçerli (taslak yok), Tamam / Esc / dışarı tıklama kapatır, ISO yapılırken pasif. Özet satırı
  saf `isoChoicesSummary(state, strings)`: yalnız varsayılandan farklı ya da hep bilinmesi gerekenler (mod, etiket,
  yeniden paketlenebiliyorsa sıkıştırma, istem yok, atlamaların boot.wim'e yazılmaması — yalnız yanıt dosyası bir denetimi
  atlıyorsa, elle seçilmiş önceki kurulum, ortam güncellemesi, UEFI CA 2023, SHA-256); `Change::Iso`'yu dinler.
  `IsoController::canRepack(const AppState&)` statik sürüm eklendi.
- **Durum** `AppState::ApplyRun::autoIso` (Waiting / Running / Done / Failed / Cancelled / NotStarted + neden): koşuyla
  birlikte yaşar, dil değişince yeniden kurulan kabuk kaybetmez. İlerleme ISO koşusundan (`isoRun`), bitişi koşuya
  kopyalanır; sonradan ISO sayfasında yapılan başka bir ISO bu kartı değiştirmez. Mantık `app/controllers/AutoIsoController`
  (+ saf `autoIsoSkip`).
- **Görünüm:** çalışırken adım listesinin son satırı "ISO oluştur · imaj kaydedilince · <dosya>"; sonuç kartında ISO bandı
  (`ResultCard::Texts::iso`): halka (D-142) + "ISO oluşturuluyor · ISO yazılıyor" + ölçer + %42 + **ISO'yu durdur** → ✓ "ISO
  hazır: <dosya> · 5,96 GB" + **Klasörde göster** (kartın dolu düğmesi; "ISO Oluştur" ikincil) / ⛔ "ISO oluşturulamadı:
  <neden>" / iptal / ⓘ "ISO oluşturulmadı: <neden>"; bant değişince sayfa yeniden kurulur (`modeKey`). Özet'in çalışma
  kartı: "ISO oluşturuluyor · %42" (halka, ISO'nun ölçeri) → "Tamamlandı · ISO hazır"; sıradaki adım "İlerlemeyi gör" / yeni
  `Next::IsoReady` ("ISO hazır: … Önce bir sanal makinede dene, sonra USB'ye yaz." · **Klasörde göster**). Durum çubuğu:
  "Uygulanıyor · ardından ISO" / "Uygulandı · ISO oluşturuluyor".
- Render: `--auto-iso` (seçenek açık, ISO klasörü sabit `D:\ISO` — her makinede aynı kare),
  `--demo-overview=iso-running|iso-done|iso-failed|iso-cancelled|iso-skipped` (+ `--page=apply`), `--iso-choices`
  (değiştirilmiş seçenekler: Yalnızca UEFI, WINLOVE_TR, ESD, istem yok, UEFI CA 2023), `--iso-options` (iletişim kutusu
  açık); render_diff envanterinde. Metinler (tr + en): `apply.autoIso.*` (25 + `options`, `optionsHint`),
  `overview.runIso / runIsoDone / nextIsoReady`, `status.applyingThenIso / buildingAfterApply`, `isoChoices.*` (14). Yeni token yok.
Sonuç:
- Doğrulanan (birim, `tests/app/AutoIsoTests.cpp`, 11 vaka / 149 doğrulama): `autoIsoSkip` her sonuçla (başarılı, atlamalı
  ama kaydedilmiş, başlatılamadı, kaydedilemedi, durduruldu, held, kept, diğer sürüm kaydedilemedi); seçenek / dosya / ISO
  sayfasıyla aynı ilk değerler / "Değiştir…" / kaynak değişince ad; VHD / WIM / kaynaksız → kullanılamaz; başarı → ISO
  bir kez başlar, ilerleme koşuyu değiştirmez, bitişi kopyalanır, sonraki ISO kartı değiştirmez; ISO hatası / iptal;
  kayıttan sonra ISO Oluştur reddi ve karşılama eksikliği; motor kapıda reddederse "sürüyor"da kalmaz; Uygula'da seçilen
  seçeneklerin (etiket, mod, ESD, istem, SHA kapalı, Secure Boot, önceki kurulum, ortam güncellemesi kapalı) koşu
  bitince `IsoController::start`'a giden istekte olması; dokunulmamış önceki kurulum → kural; özet metni tr / en; ISO
  sayfası ↔ iletişim kutusu iki yönlü; yeniden kurulan sayfa seçenekleri korur; aynı kaynak etiketi korur, başka kaynak
  sıfırlar; Uygula sayfası her aşamada penceresiz kurulur; Özet'in sıradaki adımı (`IsoReady`, `WatchRun`, `BuildIso`).
  Debug + Release `build.ps1 -Test` geçti (Release'te 13 koşudan birinde bilinen 0xC0000374, STATUS "Bilinen").
- Render: `build\lab\autoupd\final\` (apply-option-*, apply-running-iso-waits, apply-iso-*, overview-iso-*) ve
  `build\lab\autoiso-opts\final\` (apply-option-off/on/changed, apply-options-dialog(-changed), iso-page(-changed)); koyu +
  açık, tr + en. render_diff: ISO sayfasının bütün durumları (USB, WinRE yok, ortam güncellemesi, AIO, Secure Boot…) bileşene
  taşınmadan sonra birebir aynı; farklar açıklandı (yeni satır, ISO bandı, Özet'in ISO hâli, log saati).
- Doğrulanmayan: gerçek imajla Uygula + otomatik ISO (yönetici lab'ı yapılmadı; mantık ISO motorunun lab'da kanıtlı yolunu
  çağırıyor, istekteki alanlar ISO sayfasının zaten kullandıkları); canlı pencerede seçenek / "Değiştir…" diyaloğu / "ISO
  seçenekleri…" / "ISO'yu durdur" / "Klasörde göster"; ISO kaynağında `editionsWithoutWinre`'nin çalışma kopyasını mı özgün
  ISO'yu mu okuduğu (ISO sayfasıyla aynı davranış; WinRE'yi kaldıran bir ön ayardan sonra ölçülmedi); D-055 ile gerçek koşu.

## D-143 — Serbest vurgu rengi (2026-11-11; D-137'nin beş sabit vurgusunu genişletir)
Bağlam: Sahibinin listesi madde 9: "Ayarlarda sabit renkler yerine color palette koy özgür seçebilsin kullanıcı." D-137'den
beri beş sabit vurgu vardı (Mavi, Bakır, Deniz, Nar, Zeytin), tabloları `build\lab\identity\derive.py` ile türetilmişti.
Karar:
- Vurgu artık herhangi bir renk (0xRRGGBB). `ui/theme/Palette`: `setAccentColor(rgb)`, `accentColor()`, `deriveAccent(rgb)`
  (kurallar), `accentTokens(rgb)` (hazır renk → D-137 tablosu, başka renk → kurallar), `parseHexColor` / `hexColor`,
  seçicinin `toHsv` / `fromHsv`'si, `presetColor` / `presetOf`. `setAccent(Accent)` (beş hazır renk) duruyor. Mavi
  (#3070D9, varsayılan) tokenların kendisi; yüksek karşıtlık kendi renklerinde.
- Kurallar (hesap HLS + OKLCH; `Palette.cpp` başındaki yorum):
  - **Dolgu** (`accent.base`): seçilen renk. Üstünde beyaz yazı >= 4,6:1 olmalı: daha açık bir renk OKLab açıklığı
    düşürülerek koyulaştırılır (ton ve kroma korunur; kroma yalnız sRGB'ye sığmak için azalır). Koyu temalarda dolgu koyu
    kartta işaret olarak (sekme alt çizgisi, anahtar, onay kutusu) >= 3:1 görünmeli: daha koyu bir renk koyu ve saf siyah
    temalar için aynı yolla açılır. İki sınır arasında her zaman yer var (bağıl parlaklık ~0,163–0,178). Örnek: #4B0082
    açıkta aynı, koyuda #8B55CC; #F5E663 → #827600 (her temada); #0F766E açıkta aynı, koyuda #1D7E75.
  - **Üzerine gelme / basılı:** dolgu %8 / %16 siyaha doğru (D-137 gibi).
  - **Seçim dolgusu** (`accent.subtle`): dolgu kartın üstünde %20 (koyu) / %10 (açık) / %24 (saf siyah); üzerine gelme
    dolgusuna 6 seviyeden yakınsa (griler) %2'lik adımlarla artar (en çok %50).
  - **Okunur ton** (`accent.text`; koyu temalarda odak halkası da): dolgu HLS açıklığında 0,01'lik adımlarla açılır (açık
    temada koyulaşır), sayfa, çerçeve, kart, grup, menü, giriş, kartın üzerine gelme dolgusu ve seçim dolgusunun hepsinde
    >= 4,6:1 olana dek (derive.py'nin yöntemi, yuvarlaması birebir). Seçilen renkten daha renkli çıkarsa (OKLCH kroma)
    kroması seçilen rengin kromasına indirilir ve adımlama sürer — D-137'nin Deniz ile Zeytin'e elle yaptığı yumuşatmanın
    kuralı. Açık temada odak = dolgu. Saf siyahın okunur tonu koyununki. Vurgu üstü yazı beyaz.
- **Beş hazır renk:** kurallar D-137 tablolarının 30 değerinin 26'sını birebir verir; Deniz'in ve Zeytin'in koyu / saf siyah
  okunur tonu + odağı (#52AFA8 / #8DAA5E; tabloda #4FB1AA / #93AD5F) elle yumuşatılmıştı, fark OKLab'da 0,006 / 0,012
  (fark edilme eşiği ~0,02'nin altında). Hazır renk seçilince tablo kullanılır → mevcut kullanıcı için hiçbir şey değişmez.
- **Kalıcılık:** settings.json `"accent": "#RRGGBB"`. Eski dosyalar: `"blue"`, `"copper"`, `"sea"`, `"pomegranate"`,
  `"olive"` kendi renklerine; bilinmeyen / bozuk / yanlış tipte (`"sky"`, `"#12345"`, sayı) → Mavi; `"design" < 2` (D-107
  öncesi) → Mavi. `AppSettings::accent` artık `std::uint32_t` (`accentFromText()`).
- **Ayarlar › Vurgu rengi:** beş hazır renk tek tıkla (←/→ ile de), altıncı yuvarlak "Özel renk" (seçili değilken ton
  yelpazesi, seçiliyken o renk), yanında ad ya da "Özel renk · #RRGGBB". Tıklama / Enter / Boşluk → satırın altında açılan
  panel (`ui::ColorPickerPopup`; menü gibi gölgeli, köşe `radius.menu`, iç boşluk `size.panelPad`): doygunluk × parlaklık
  alanı (232 × 120), ton çubuğu, dolgunun bu temadaki hâli + beyaz "Aa" örneği, `#RRGGBB` kutusu, gerekirse not ("Beyaz
  yazı okunur kalsın diye dolgu koyulaştırıldı." / "Koyu temada seçimler görünsün diye dolgu açıldı."). Sürüklerken bütün
  arayüz canlı boyanır (yalnız `ui::setAccentColor`, kaydedilmez); bırakma / Enter / ok tuşu / hazır renk kaydeder; Esc
  açıldığı renge döner; dışarı tıklamak olduğu gibi kapatır. Kutu yalnız tam 6 hane yazılınca önizler (#RGB yalnız Enter
  ile). Pencerenin 1 px vurgu çerçevesi (`App::applyTheme`) kaydedilince güncellenir.
- Canlı uygulama: bütün vurgu fırçaları çizim anında çözülür (`colorArgb`), önbellekte vurgu rengi yok. İkon tonları
  (`ui/render/IconTone.cpp`, D-114) kendi anlamsal paletinden; vurgu mürekkebiyle çizilen ikonlar yeni rengi kendiliğinden
  alır. "Varsayılanlara dön" vurguyu eskisi gibi sıfırlamaz (değiştirilmedi).
- Render: `--accent=` artık `#RRGGBB` de alır; `--demo-accent-picker` (Ayarlar, seçici açık). Canvas'a `fillLinearGradient`.
Sonuç:
- Doğrulanan: `tests/ui/AccentTests.cpp` — hazır renk eşitliği (26 + 4), Mavi = tokenlar; 960'tan fazla renk (saf sarı,
  beyaz, siyah, griler, açık sarı, koyu mor, camgöbeği, sayfa renkleri, 24 ton × 3 doygunluk × 4 parlaklık, 600 sabit
  tohumlu rastgele) üç temada PaletteTests'in bütün kuralları (metin 1 / 2 seçimde >= 4,5, metin 3 >= 3; vurgu metni her
  yüzeyde >= 4,5; beyaz yazı dolgu / üzerine gelme / basılıda >= 4,5; dolgu sayfa / çerçeve / kartta >= 3; odak >= 3;
  seçim karttan >= 8, üzerine gelmeden >= 6 seviye; yüksek karşıtlık değişmiyor); ton korunması (sarı < 4°, mor < 6°); hex
  ayrıştırma; HSV gidiş-dönüş (<= 1 seviye); seçici etkileşimi (sürükleme önizler, bırakma kaydeder, ton çubuğu, kutuya
  yazma, Esc, dışarı tıklama, oklar). `tests/app/SettingsTests.cpp`: `#RRGGBB` kaydı, eski adlar, bozuk değerler.
  `deriveAccent` ~48 µs / renk (Release). Testler D-142 ile birlikte: Debug + Release 599 vaka / 19.407 denetim.
- Render `build\lab\ring\final\accent-*` (seçici açık: koyu tr #4B0082, açık tr #F5E663, koyu en #F5E663, açık en #0F766E;
  üç özel renk × Özellikler / Uygula / Ayarlar × koyu / açık, + Uygula saf siyah). render_diff'te varsayılan Mavi'de bütün
  renkler piksel piksel aynı (hazır renk eşitliğinin render düzeyindeki kanıtı); fark yalnız Ayarlar'ın vurgu satırı ve galeri.
- Doğrulanmayan: gerçek pencerede sürükleme akıcılığı, fare yakalama ve pencere çerçevesinin renk değişimi; ekran okuyucu
  (UIA) adları; Windows yüksek karşıtlık temasında seçicinin görünümü.

## D-142 — WinLove'un ilerleme halkası (2026-11-11; D-135'in iptalinden sonra sahibinin istediği tek hareket)
Bağlam: Sahibinin listesi madde 4: "Uygulamamızın bütün sayfalarında yer alan bütün progressringleri daha güzel animasyonlu
farklı bir şekilde yap." Her "çalışıyor" göstergesi `Icon::Spinner` ikonuydu: 8 dilimlik ikon her 100 ms'de 45° (kademeli;
İndir sayfasında 250 ms) döndürülüyordu; ISO Oluştur, Özet'in çalışma kartı, Uygula'nın baş kartı ve adım listesinde hiç
dönmüyordu. Genel hareket sistemi (D-135) iptal olduğu için yalnız bu halka yazıldı; DirectComposition / hareket
altyapısı yok.
Karar:
- **Tek bileşen** `ui/widgets/ProgressRing.{h,cpp}` (ui katmanı): `paintRing(canvas, kutu, poz, mürekkep)` (herhangi bir
  widget'ın `paint`'inde), `RingClock` (kendi tick'i olan şeritler / kartlar için: başlangıç zamanı + kare aralığı),
  `ProgressRing` widget'ı (kendi tick'i; yalnız çiziliyorken döner).
- **Belirsiz kip:** yay kafası yarım çevrimde 250° öne koşar, kuyruk ikinci yarıda yetişir (`motion.easings.ring`
  cubic-bezier(0.4, 0, 0.2, 1)); yay 20°'den (nokta değil, kısa çizgi) 270°'ye uzar, kısalır; halka ayrıca her
  `motion.ringTurn` (2000 ms) bir tur döner. Bir çevrim `motion.ringCycle` 1500 ms; her çevrim öncekinden 160° ileride
  başlar, poz 13,5 sn'de bir tam tekrarlar. Poz zamanın fonksiyonu (`ringPose(geçen ms)`): geç gelen kare sıçramaz, kare
  sayısına bağlı değil. **Belirli kip** (`ringValuePose`: yüzde, saat 12'den saat yönünde; widget'ta değer `motion.base` ile
  kayar) galeride ve API'de hazır; uygulamada yüzde her yerde zaten yanındaki çubukta ve sayıda yazdığı için halka
  belirsiz kaldı (aynı sayıyı iki kez göstermemek, uzun adımda donmuş görünmemek için).
- **Görünüm:** `accent.text` (her temada >= 4,5:1; özel vurguda da) yuvarlak uçlu yay, altında aynı mürekkebin
  `opacity.ringTrack` (%20) iz halkası. Yüksek karşıtlık: 1 fiziksel piksel `line.strong` iz + sistemin vurgu rengindeki yay.
  Kutunun köşesi fiziksel piksel ızgarasına, çap ve çizgi kalınlığı tam piksele yuvarlanır (`size.ringStroke` 2 → 1×: 2 px,
  1,25×: 3, 1,5×: 3, 2×: 4; `ringStrokeLarge` 2,5 → 3 / 3 / 4 / 5 px).
- **Boyutlar:** `size.ring` 16 (satır içi: Uygula adım listesi, ISO Oluştur ilerleme satırı, Özet çalışma kartı, Windows
  indir iş satırı), `size.ringLarge` 20 (şeritler ve baş kart: Sürümler işlem şeridi, Diller / Güncellemeler / ortak indirme
  şeridi, Uygula baş kartı); 20'likler eski 16'lık ikonun merkezine oturtuldu (yerleşim kaymadı).
- **Kare temposu:** pencere her karede **bütünüyle** yeniden çiziliyor (D-011: `InvalidateRect(hwnd, nullptr)` →
  `WM_PAINT` → `App::paint` → `Present(1,0)`, kirli dikdörtgen yok; ARCHITECTURE §3, AUDIT D4 / D5 açık). Yalnız halkanın
  alanını çizmek flip-model'de `Present1` + kirli dikdörtgen + önceki karenin korunmasını ister; doğruluğu pencere açmadan
  doğrulanamadığı için yapılmadı (ayrı iş önerisi, AUDIT D4 / D5). Mevcut `tickIntervalMs` kullanıldı: halka dönerken sahibi
  widget `motion.ringFrame` 25 ms ister (Windows'un varsayılan 15,6 ms zamanlayıcı çözünürlüğünde ~31–40 ms → ≈ 25–32
  kare/sn). Halka yokken / görünmezken kare istenmez: widget son tick'ten beri çizilmediyse (gizli sayfa, kırpılmış kaydırma
  alanı) tick'i bırakır, çizilince uyanır; şerit iş bitince eski temposuna (100 / 250 ms) döner. Hareketi azalt (Windows'un
  "animasyonları göster"i kapalı ya da uygulamanın "Hareketi azalt"ı): duran yay (saat 12'den 270°), halka için kare yok.
- `--render`: varsayılan karede duran yay (`forceInstantMotion`, deterministik); `--anim-time=<ms>` her halkayı o anki
  pozuyla çizer → pencere açmadan kare dizisi. Yeni `--demo-boot-drivers=iso-running` (ISO yazılıyor %42; ISO sayfasındaki
  halka).
- `Icon::Spinner`'ın bütün kullanımları halkaya geçti (`grep Icon::Spinner src` boş; ikon Icons.g.h'de kalıyor):
  `pages/images/OperationStrip`, `pages/FetchStrip`, `LanguagesPage::FetchStrip`, `UpdatesPage::FetchStrip`, `DownloadPage`
  (halka artık başlığın satırında — eski ikon çubuğun başına biniyordu), `IsoPage` (artık dönüyor), `ApplyPage` (adım
  listesindeki çalışan satır), `apply/ApplyCards` `RunHeader` (`Texts::icon` artık `optional`: boş = halka), `OverviewPage`.
  Kurulum sihirbazı (D-126) ve `programs.ps1`'in kendi WPF halkaları dokunulmadı. Galeri: 16 / 20 belirsiz, %0 / 25 / 60 /
  100 belirli.
- Tokenlar (tokens.json 5.0.0, sürüm artırılmadı): `size.ring` 16, `size.ringLarge` 20, `size.ringStroke` 2,
  `size.ringStrokeLarge` 2,5, `motion.durations.ringCycle` 1500, `ringTurn` 2000, `ringFrame` 25, `motion.easings.ring`
  [0.4, 0, 0.2, 1], `opacity.ringTrack` 0,2. Açılar (250° atlama, 20° en kısa, 270° duran yay) hareketin geometrisi:
  `ProgressRing.cpp`'de adlı sabitler. Canvas'a `strokeCircle`, `strokeArc` (tek yay segmentli path geometry, her çizimde).
Sonuç:
- Doğrulanan: `tests/ui/ProgressRingTests.cpp` — 20 sn boyunca 1 ms adımlarla poz sıçramasız (1 ms'de < 1,5°), yay hep
  20–270°, 10 saat sonra da sonlu; belirli / duran poz; `RingClock` (hareketi azalt → duran poz ve kendi tempo,
  `--anim-time`, ikinci iş fazı korur); widget (dönerken 25 ms'lik zamanlayıcı, gizlenince bir kare sonra kare istemiyor,
  yeniden çizilince uyanıyor, durunca ve hareketi azaltta kare yok); 1 / 1,25 / 1,5 / 2× ölçekte sol kenar pikseli >= %85
  dolu. Bir halkanın kaydı + çizimi ~5 µs (Release, offscreen, `wl_tests -tc="perf: ring*" --no-skip`). Debug + Release
  `build.ps1 -Test`: 599 vaka / 19.407 denetim (10 atlanan; D-143 ile birlikte).
- render_diff (önce `build\lab\ring\before\`, sonra `after\`, aynı derinlikte; 300 kare): 275 aynı, 23 farklı, 2 atlanan
  (`--demo-store=dialog`). Farklar: Sürümler işlem şeridi (10), Özet çalışma kartı (4), Uygula çalışıyor (2), Ayarlar'ın
  vurgu satırı (4, D-143), galeri (2), "Boş alan" (1). Envanterde görünmeyen halka yerleri ayrıca render edildi (Diller
  şeridi, Windows indir iş satırı, ISO ilerleme satırı). Bütün halka yerleri koyu / açık / yüksek karşıtlık / 1–2× bakıldı;
  kare şeritleri ve GIF'ler `build\lab\ring\final\ring-*`.
- Doğrulanmayan: gerçek pencerede CPU / GPU maliyeti ve gerçek kare temposu (pencere açılmadı). Halka dönerken pencere 25
  ms'de bir baştan çiziliyor (eski şeritlerde 100 ms'de bir, ISO / Uygula / Özet'te hiç): uzun bir Uygula'da (30+ dk) eski
  şeritlere göre ~3–4 kat daha çok tam pencere çizimi; tam pencere çiziminin süresi ölçülmedi. Önerilen ölçüm: Uygula
  sürerken Görev Yöneticisi'nde WinLove.exe CPU'su, `ringFrame` 25 → 33 / 40 ile karşılaştırma. Windows'un yüksek karşıtlık
  temalarında (sistem renkleriyle) gerçek görünüm yalnız tokenlarla render edildi.

## D-141 — VHD / VHDX kaynağı: fark diskiyle bağla, kaydet = birleştir, kaydetmeden çöz = dosya değişmez (2026-11-11)
Bağlam: CLAUDE.md ürünü "ISO/WIM/ESD/VHD" diye tanıtıyordu ama VHD hiç yoktu: `ImageFormat::Vhd / Vhdx` ve uzantı
tanıma vardı (birim testiyle), `openSource` "VHD/VHDX sources arrive with a later step" ile reddediyordu; arayüzde,
metinlerde, wlcli'de hiçbir şey yoktu (yalnız `usb-write --allow-virtual`'ın laboratuvar VHDX'i). İstenen: kurulu (ya da
uygulanmış, hiç açılmamış) bir Windows taşıyan .vhd / .vhdx'i kaynak olarak açmak, bağlamak, Uygula ile değiştirmek —
D-021'e uygun: yalnız çevrimdışı disk, çalışan sistem değil. Sahibinin seçtiği üç özellikten biri (koordinatörün
sırasıyla 6. iş). Kural 6: önce motor + wlcli + gerçek diskte laboratuvar, sonra arayüz. Zorluk: VHD'de "kaydetmeden
çöz"ün ucuz bir karşılığı yok (değişiklikler yerinde yazılır).
Karar:
- **Kaydetme / atma anlamı (en önemli karar): fark diski.** "Bağla", kullanıcının dosyasının üstüne WinLove'un kendi
  fark diskini oluşturur (`<çalışma klasörü>\vhd\<ad>.winlove-changes.vhdx`; VHD'nin çocuğu VHD) ve **çocuğu** bağlar
  (virtdisk.dll, `PERMANENT_LIFETIME | NO_DRIVE_LETTER`). Her yazma çocuğa gider; kullanıcının dosyası yalnız okunur.
  **Kaydet** = kovanlar boşaltılır, birim kilitlenip ayrılır, çocuk ayrılır, ayrıldığı **doğrulanınca** çevrimdışı
  `MergeVirtualDisk` (v1, derinlik 1; olmazsa v2) ile kullanıcının dosyasına işlenir, çocuk silinir. **Kaydetmeden çöz** =
  ayır + çocuğu sil: dosya bir bayt bile değişmez (laboratuvarda SHA-256 ile kanıtlı). Birleştirme olmazsa hiçbir şey
  kaybolmaz: çocuk ve kayıt kalır ("bağlı değil" → yeniden bağlanabilir ya da atılabilir). "Yerinde, geri alınamaz"
  tasarımı gerekmedi.
- **Görüntü kökü:** bağlı diskin birimleri `IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS` ile disk numarasına göre bulunur (yalnız
  o diskin birimlerine bakılır), Windows birimi = `Windows\System32\config\SYSTEM` taşıyan en büyük NTFS birim
  (`chooseWindowsVolume`, saf). Birim boş bir klasöre **birim bağlama noktası** olarak konur (`SetVolumeMountPointW`); o
  klasör DISM'in açtığı imaj köküdür (`DismOpenSession`, `dism.exe /Image:`), bağlı bir WIM'in klasörü gibi — sayfaların,
  kovanların, Uygula'nın kodu değişmedi. Windows yine de harf verirse yalnız o birimin harfi (GUID'iyle) kaldırılır.
  Uygulamada klasör `<çalışma kökü>\vhd\mount` (WIM'in `mount` klasörü **değil**: onun onarımı içi dolu, DISM kaydı
  olmayan klasörü "artık" sayıp temizlerdi), wlcli'de verilen klasör.
- **Kaynak bilgisi:** disk yalnız okumalık ve kalıcı olmayan biçimde bağlanır (süreç ölürse kendiliğinden ayrılır),
  kovanlar offreg ile okunur (sisteme kovan yüklenmez), ayrılır: `CurrentVersion` (EditionID, CurrentBuildNumber, UBR,
  DisplayVersion, InstallationType), `Session Manager\Environment\PROCESSOR_ARCHITECTURE`, `Control\MUI\UILanguages`,
  `Nls\Language\InstallLanguage`; birimin dolu / boş alanı. Ad EditionID'den (`editionDisplayName`; laboratuvar:
  uygulanmış Pro imajının ProductName'i "Windows 10 Enterprise"), sunucu / bilinmeyen kimlikte ProductName (22000+ "10"
  → "11"). Tek sürüm (index 1), `SourceInfo::disk` (sanal boyut, dosya boyutu, dinamik / sabit / fark, birim).
  WinLove'un kendi bağladığı disk klasöründen okunur.
- **Kayıt ve çökme güvenliği:** her bağlama, disk oluşturulmadan **önce** `%LOCALAPPDATA%\WinLove\vhd-mounts.json`'a
  yazılır (diskin yolu, çocuk, klasör, birim GUID'i, zaman, kim), ayrılınca silinir; son kayıt gidince dosya da gider.
  Sonraki açılış (uygulama `adoptExistingMount`, wlcli `mounts / repair / unmount`) **yalnız kaydın adlandırdığı**
  çocuğa, klasöre, birime bakar — hiçbir diski tahminle ayırmaz, hiçbir şeyi listeleyip temizlemez. Durumlar (saf
  `classifyVhdMount`): Ok; NotAttached (yeniden başlatma bütün sanal diskleri ayırır) ve NoMountPoint → **Remount**
  (değişiklikler korunur, WIM'in "needs remount"u gibi); ChildMissing → kaydı unut (boş klasörü sil; içinde dosya olan
  klasöre dokunma); DiskMissing (takılı olmayan sürücü) → hiçbir şey silinmez. Bağlılık sorusu üç durumlu: yanıt yoksa
  hiçbir şey ayrılmaz, birleştirilmez, silinmez. Kaydı olmayan bir çocuk dosyası yoldaysa bağlama reddedilir (silinmez,
  yeniden kullanılmaz).
- **Ne olmaz (yetenekler, saf `sourceCapabilities`):** sürüm listesinin WIM'e ait işleri (sil, yeniden adlandır,
  çoğalt, sırala, ekle, dışa aktar), doğrulama (WIM akışları), Araçlar (sıkıştır / böl / birleştir / ESD), ISO / USB
  (kurulum dosyası yok) → düğmeler pasif, ipucu nedenini söyler (`images.vhdNoWim`, `iso.blockVirtualDisk`). Uygula: WIM
  yeniden yazımı (`optimizeWim`), sürüm metinleri, `lang.ini`, `sources\sxs` yok; kaydetme `ApplyJobOptions::saveImage`
  ile (birleştir + ayır). Diğer sürümlere uygulama (D-055) yok.
- **Motor:** `core/image/VirtualDisk.{h,cpp}` (virtdisk.lib bağlandı; katman: yalnız core), `Source.cpp` VHD dalı,
  `MountHealth` (`hivesLoadedFromDevice`, `unloadHives`: kovanlar bağlama noktası klasörüyle değil birimin aygıt yoluyla
  görünür; başka bir aracın DISM üzerinden bağladığı VHD'nin adı için `openSource` çağrılmaz — okumak onu bağlamaya
  çalışırdı), `ApplyJob::saveImage`.
- **wlcli:** `info <vhd|vhdx> [--json]` (disk + Windows), `mount <vhd> 1 <klasör> [--changes-dir=<klasör>]`,
  `unmount <klasör> --commit|--discard` (birleştir / sil), `mounts` (VHD kayıtları da: `kind: "vhd"`, durum, eylem,
  değişiklik baytı), `repair <klasör>` (yalnız kayıt: remount / unut), `apply … [--commit]` (kaydetme = birleştirme),
  `--vhd-records=<dosya>` (laboratuvar kendi kayıt dosyasını kullanır).
- **Arayüz** (yeni token yok, yeni ikon yok, şerit tasarımı aynı, hareket yok): Kaynak sayfası filtresi / bırakma kuralı
  / ipucu .vhd · .vhdx; son kullanılanlarda disk ikonu (`DiskPartition`); "Bağla" önce **sorar**: "… doğrudan yazılmaz:
  WinLove değişiklikleri kendi değişiklik diskinde tutar (<yol>). Kaydet ve çöz … işler; Kaydetmeden çöz … siler ve dosya
  hiç değişmeden kalır. Disk sürücü harfi almadan bağlanır." (Vazgeç · Bağla); Sürümler'in yan kartında WIM satırları
  (dal, sysprep, tarihler, dosya / klasör sayısı, sıkıştırma, bölüm) yerine **Sanal disk** bölümü (tür · dinamik, disk
  boyutu, dosya boyutu, Windows bölümü · boş, "kaydedince diske işlenir"); Özet'te "VHDX · dinamik · 40 GB · … ·
  kaydedilene dek dosya değişmez", "Sürüm i / n" yok; çöz diyaloğu ve bekleyen / başarısız kuyruklu hâlleri "sanal diske
  işler / değişiklik diski silinir" der; Uygula bitince "Değişiklikler sanal diske işlendi ve disk ayrıldı", işlem satırı
  "Sanal diske işle ve ayır", sonuç kartında ISO yerine Rapor (ana) · Preset · Log; Özet'in sıradaki adımı "… disk
  ayrıldı. Yeni değişiklikler için diski yeniden bağla." + Bağla; ISO Oluştur "Sanal diskten ISO yapılmaz: içinde kurulum
  dosyaları (boot.wim, setup) yok."; bildirimler "Değişiklikler sanal diske işlendi, disk ayrıldı" / "Değişiklikler
  silindi, disk ayrıldı: dosyası değişmedi". Açılışta kayıttaki disk geri yüklenir (bağlı değilse yeniden bağlanır).
  Render: `--demo-vhd[=closed|confirm|mounted|unmount|done|iso]` (uydurma kaynak, hiçbir şey bağlanmaz; render'ın son
  kullanılanlar listesine girmez), `tools/render_diff.py` envanterinde.
- 24 yeni metin anahtarı (tr + en, aynı yer tutucular) + 3 değişen değer (`source.desc`, `source.dropHint`,
  `images.addFile`: VHD / VHDX eklendi).
Sonuç:
- Doğrulanan (laboratuvar `tools\lab_vhd.ps1`, yönetici, `build\lab\vhd`, 2026-11-11 08:40–09:05): 40 GB dinamik
  `golden.vhdx` (GPT + NTFS, `dism /Apply-Image` lab WIM'i index 4 = Pro 26200.8037, 87 sn; disk yalnız kendi dosya
  yolundan, numarası / türü / boşluğu / boyutu / konumu denetlenerek biçimlendi). Taze kopyada: `info` (Windows 11 Pro ·
  26200.8037 · x64 · tr-TR, 2,3 sn, sonra bağlı değil); `mount` 0,1–0,7 sn (çocuk bağlı, klasör bağlama noktası, harf
  yok); 7 okuma komutu (özellik 550, paket 195, yetenek 1774, appx 505 satır, sürücü `[]`, servis 264, isteğe bağlı
  özellik 1562); 10 adımlı karışık küme (özellik, CBS bileşeni, 2 appx, 2 depo tarifi, HKLM + HKCU değeri, servis, işaret
  değeri) 10 / 10, 38 sn; discard 0,5 sn → yeniden bağlayınca işaret yok, dosya yerinde; `apply` 4. adımda öldürüldü →
  taze `wlcli mounts` kendi diskini kayıttan buldu, `unmount --discard` öldürülenin bıraktığı 2 DISM kovanını aygıt
  yolundan boşaltıp ayırdı; yeniden başlatma benzetimi (çocuk kendi yolundan ayrıldı) → `not attached / remount` →
  `repair` yeniden bağladı, okundu; bunların hepsinden sonra kullanıcının dosyasının **SHA-256'sı aynı**. `apply --commit`
  41 sn (çevrimdışı birleştirme 364 ms, 522 MB) → yeniden bağlayınca 10 değişikliğin hepsi diskte, **ScanHealth temiz**
  (54 sn). `.vhd` biçimi: 6 GB WinPE diski, `.vhd` çocuk, `apply --commit` (birleştirme 127 ms) → kalıcı. Her aşamanın
  başında ve sonunda `dism /Get-MountedImageInfo` aynı (yalnız başka bir aracın "needs remount" bağlaması, dokunulmadı),
  hiçbir temizlik komutu çalışmadı; sonunda hiçbir sanal disk bağlı değil, kayıt / klasör / çocuk yok.
- Birim testleri: `tests/core/VirtualDiskTests.cpp` (10 vaka / 111 denetim: uzantı, birim seçimi, disk numarası, çocuk
  yolu, ad ve mimari, yetenekler, durum / eylem tablosu, kayıt dosyası gidiş-dönüş + bozuk / şüpheli kayıtlar, klasör /
  diskle bulma, çocuğu olmayan kaydı unutma — içinde dosya olan klasöre dokunmadan —, kaydı olmayan klasörün
  çözülmemesi, olmayan diskin hiçbir şey yazmadan reddi), `tests/app/VhdSourceTests.cpp` (4 vaka / 36 denetim: Görüntü
  denetleyicisinin redleri ve nedenleri, dışa aktarmanın motora gitmeden reddi, ISO engeli, Özet'in disk bilgisi ve
  "kaydedildi" sonrası adımı, klasörlerin WIM klasöründen ayrı olması). Debug + Release `build.ps1 -Test` geçiyor (her
  ikisinde 570 vaka / 18.068 denetim, 9 atlanan). Görüntüler `build\lab\vhd\final\` (tr) ve `final-en\` (koyu + açık;
  ara kareler `r1..r3`).
- Regresyon (`tools/render_diff.py`, önce = bu işten önceki Debug exe `build\lab\vhd\before\`, sonra = yenisi,
  x64-9448.iso ile; 264 kare, iki tema): 188 aynı, 60 farklı, 16 "başarısız" (+ 2 kare iki exe'de de açılmayan
  `--demo-store=dialog`). 16 başarısızın hepsi eski exe'nin tanımadığı `--demo-vhd*` (8 durum × 2 tema; eski exe 1 ile
  çıkıyor). 60 farkın 12'si Kaynak sayfası (`--page=source` ve onun 5 demosu × 2 tema): sayfa açıklaması ve bırakma
  ipucu artık VHD / VHDX'i söylüyor (bilerek; her karede aynı 10.222 piksel, 649–1464 × 173–329; ikisinde ayrıca "Boş
  alan"); 40'ı yalnız durum çubuğundaki "Boş alan" rakamı; 4'ü log satırlarının saati (`--demo-logs`, Uygula çalışıyor /
  duraklatıldı); 4'ü `--demo-icons` (eski exe başka derinlikte durduğu için demo marka ikonu bulunmuyor; D-137'de
  açıklanan). Başka hiçbir sayfa / demo / diyalog değişmedi. Kareler `build\lab\vhd\diff\`, liste
  `build\lab\vhd\diff_log.txt`.
- Bulunup düzeltilen (laboratuvarın ilk koşusu): bağlı bir diskte yalnız GET_INFO isteyen v1 açılış 0x80070005 ile
  reddediliyor; motor bunu "bağlı değil" sanıp discard'da ayırmadı, kaydette v2 birleştirmeyi **bağlı diskte** çalıştırdı
  (çevrimiçi birleştirme başarılı oldu, kullanıcının dosyası bağlı kaldı; betiğin güvenlik ağı kendi dosya yolundan
  ayırdı, kopya silindi). Şimdi: kendi çocuğu ATTACH_RW | GET_INFO | DETACH ile açılır (bağlıyken de yanıt verir),
  bağlılık üç durumlu, birleştirme yalnız ayırma doğrulandıktan sonra. İkinci koşu baştan sona geçti (ENGINE saha notu).
- Doğrulanmayan: açılmış / kullanılmış bir Windows'un diski (yalnız uygulanmış, hiç açılmamış imaj); BitLocker; birden çok
  Windows birimi, MBR, sabit boyutlu disk; kullanıcının diski zaten fark diskiyse; gerçek yeniden başlatma ve birleştirme
  sırasında güç kesilmesi; NTFS olmayan çalışma kökü; uygulamanın kendisi pencerede (bağla / kaydet / çöz / açılışta geri
  yükleme yalnız birim testiyle ve `--render` ile — pencerede tıklanmadı); render'da pasif şerit düğmesinin ipucu karesi
  (render yolu pasif düğmenin ipucunu çizmiyor — var olan Doğrula'nınki de; metinler birim testinde); bağlıyken WinPE'de
  yalnız kayıt adımı olan plan (DISM SOFTWARE'i tutuyor, 0x80070020 — WIM'de de aynı, sanala özgü değil; ENGINE saha notu).

## D-140 — Güncelleme denetimi: yalnız haber verir, tek HTTPS GET, günde en çok bir kez (2026-11-11)
Bağlam: WinLove GitHub'da yayımlanıyor (`shadesofdeath/WinLove`); bugüne kadarki dokuz yayının hepsi ön sürüm (etiketler
`v1.0.0-alpha` … `v1.3.0-beta`, adları "WinLove 1.3.0 Beta"; ilkinin adı "WinLove 1.0 Alpha"). Uygulamanın kendi sürümü
CMake'te `WL_VERSION_LABEL` = "1.3.0 Beta" (`WL_VERSION_STRING` = "1.3.0"). Kullanıcı yeni bir sürümü ancak GitHub'a
bakarak öğreniyordu. Sahibinin seçtiği üç özellikten biri (koordinatörün sırasıyla 5. iş). İstenen: yalnız haber vermek
— hiçbir şey indirilmez, kurulmaz; çevrimdışı olmak olağan bir durum; asla diyalog yok, aynı sürüm için asla ikinci kez
haber yok.
Karar:
- **Ağa giden (tamamı):** tek bir HTTPS GET — `https://api.github.com/repos/shadesofdeath/WinLove/releases?per_page=20`.
  İsteğin başlıkları (WinHTTP'nin kendi gönderdiği hâliyle, `wlcli update-check --verbose` ile okundu): `User-Agent:
  WinLove/1.3.0`, `Accept: application/vnd.github+json`, `X-GitHub-Api-Version: 2022-11-28`, `Connection: Keep-Alive`
  (+ HTTP/1.1'in `Host: api.github.com`'u). Gövde yok, çerez yok (`WINHTTP_DISABLE_COOKIES`), hesap / belirteç yok;
  makine adı, kullanıcı adı, Windows sürümü, imaj, ayar, kuyruk — hiçbiri gönderilmez. GitHub, her HTTPS isteğinde olduğu
  gibi bağlantının IP adresini görür; Windows'un sistem proxy'si (var olan paylaşılan WinHTTP oturumu) kullanılır.
  Yanıttan yalnız etiket, ad, taslak / ön sürüm bayrağı, yayım tarihi, `html_url` ve gövdenin ilk satırları okunur;
  hiçbir şey diske yazılmaz (yalnız ayarlara: son denetim zamanı ve iki etiket, aşağıda). Hiçbir dosya indirilmez.
- **Motor** (`core/net/ReleaseCheck`; `core/net/Http`'ye istek başına seçenekler: User-Agent, ek başlıklar, zaman aşımı,
  çerezsiz, gönderilen başlıkları okuma, gövde sınırı — eski çağrılar değişmedi): aşama başına 6 sn zaman aşımı, 4 MB
  gövde sınırı, iptal belirteci (WinHTTP isteği kapatılır). Sürüm karşılaştırması saf fonksiyon (`parseAppVersion`,
  `compareAppVersions`): `v1.3.0-beta`, `1.3.0 Beta`, `WinLove 1.3.0 Beta`, `v1.0.0-alpha`, `1.4.0`, `v1.4.0-rc.2`,
  `1.4.0 RC 2`, `1.3` (= 1.3.0); alpha < beta < rc < sürüm; aşamanın sayısı ("beta.2"). Başka her biçim
  (`v1.3.0-preview`, `2.0.0.1`, bir tarih, "latest") "karşılaştırılamaz": asla yanlış "yeni sürüm var" denmez. Yanıt:
  taslaklar atlanır, ön sürümler sayılır (hepsi ön sürüm); en yeni = en yüksek sürüm (eşitse sonra yayımlanan); sürüm
  önce etiketten, olmazsa addan. Sonuç: `Current` / `Available` / `Unknown` (bir sürüm okunamadı — en son listelenen
  yalnız adıyla gösterilir) / `None` (hiç yayın yok). Tarayıcıda açılacak adres yalnız
  `https://github.com/shadesofdeath/WinLove/releases/…` altındaysa yanıttan alınır, değilse projenin sürüm listesi
  (`releasePageUrl`). Notlar: gövdenin ilk ≤ 4 satırı düz metin (başlıklar, çizgiler, yalnız bağlantıdan oluşan satır,
  kod, tablo, HTML çıkar; bağlantının metni kalır; madde "• "), ≤ 600 karakter. Hata sınıfları (`releaseFailure`):
  bağlantı (çevrimdışı, DNS, zaman aşımı, TLS) · GitHub'ın saatlik sınırı (403 / 429) · başka HTTP durumu · okunamayan
  yanıt · iptal.
- **wlcli:** `wlcli update-check [--json] [--verbose] [--save=<dosya>] [--current=<sürüm>]` — çıkış 0 (denetlendi, ne
  bulduysa), 2 (yanıt yok), 130 (iptal). `--verbose` gönderilen başlıkları basar, `--save` yanıtı saklar (birim
  testlerinin kaydı buradan), `--current` başka bir sürümü karşılaştırır.
- **Ayar:** Ayarlar › yeni bölüm "WinLove güncellemeleri" › "Güncellemeleri denetle" (`AppSettings::checkUpdates`,
  **varsayılan açık**; ipucu: "Günde en çok bir kez GitHub'a bakar; hiçbir veri gönderilmez, hiçbir şey indirilmez";
  "Varsayılanlara dön" onu da açar). settings.json'a ayrıca `updateCheckedAt` (yanıt alınan son denetim, UTC saniye),
  `updateLatestTag` (o denetimin bulduğu en yeni etiket), `updateNotifiedTag` (haber verilen sürüm).
- **Sıklık:** `UpdateController` (`app/controllers`) yalnız `App::runWindowed`'da kurulur; kendi `TaskRunner`'ı var (UI
  iş parçacığı değil, DISM motorunun ya da okuyucunun arkasında beklemez, açılışı geciktirmez). Pencere açıldıktan 4 sn
  sonra bir kez (pencere zamanlayıcısı 9): ayar açıksa ve son yanıt ≥ 24 sa önceyse — ya da hiç yoksa, ya da
  "gelecekten"se (saat 1 sa'ten fazla geri alınmış) — denetler (`updateCheckDue`). Hata sessiz: bildirim yok, Hakkında
  nedenini yazar; son yanıt zamanı değişmez, bir sonraki açılış yeniden dener. "Şimdi denetle" her zaman denetler (ayar
  kapalıyken de).
- **Bildirim:** açılış denetimi yeni bir sürüm bulursa ve etiketi `updateNotifiedTag` değilse (`updateNoticeDue`): tek bir
  bildirim (Toast, Bilgi; "Yeni WinLove sürümü: WinLove 1.4.0 Beta" · "2 gün önce GitHub'da yayımlandı. Hiçbir şey
  indirilmedi." · **Göster** → Hakkında) ve durum çubuğunda vurgu renginde, ikonlu bir bölüm ("Yeni sürüm: …", tıklanınca
  Hakkında). Etiket o anda `updateNotifiedTag`'e yazılır: aynı sürüm bir daha asla duyurulmaz (uygulama hemen kapansa
  da). Hakkında sayfası görülünce durum çubuğu bölümü kalkar. Bildirim AppState'te (`AppUpdate::notice` / `toastShown`):
  dil değişince yeniden kurulan kabuk ikinci bir bildirim göstermez. Diyalog yok. "Şimdi denetle" ile bulunan sürüm
  sayfada zaten görüldüğü için duyurulmaz (etiketi yine yazılır).
- **Hakkında:** kahraman kartının altında "Güncellemeler" kartı (D-114'ün kart dili): durumun ikonu, başlık, tek satır
  olgu; sağda "Şimdi denetle" (Secondary, denetlenirken pasif) ve yeni sürüm varken sayfanın tek dolu düğmesi **Sürüm
  sayfasını aç** (tarayıcıda, Shell'in var olan `ShellExecuteW` yardımcısıyla; yalnız `https://`); altında sürüm
  notlarının ilk satırları. Durumlar: "Güncellemeler henüz denetlenmedi" / "Son denetim: …" (son bilinen sürüm · açılışta
  günde en fazla bir kez / açılışta denetleme kapalı) / "Denetleniyor…" / "WinLove güncel" (en son sürüm · yayım · son
  denetim) / "Yeni sürüm var: …" / "Sürümler karşılaştırılamadı" / "GitHub'da yayımlanmış sürüm yok" / "Denetlenemedi" +
  neden (bağlantı · saatlik sınır · HTTP n · okunamayan yanıt). Metinler AppState bildiriminde kurulur (`updateCardView`,
  saf fonksiyon), notların yüksekliği layout'ta ölçülür (genişliğe göre önbellekli), `paint()` yalnız çizer.
- **Render ve testler ağa çıkamaz:** denetleyiciyi yalnız pencereli çalışma kurar; render'da
  `Shell::Services::checkForUpdates` boş (düğme bir şey yapmaz); testler kendi fetch / saat fonksiyonlarını verir.
  Render durumları: `--demo-update[=available|current|failed|checking]` (`available` ayrıca Hakkında dışındaki sayfada
  bildirimi ve durum çubuğunu gösterir); `tools/render_diff.py` envanterinde.
- 58 yeni metin anahtarı (tr + en, aynı yer tutucular; mevcut hiçbir değer değişmedi): `appUpdate.*` 26,
  `settings.updates / checkUpdates / checkUpdatesHint`, ve D-139 ekinin 29'u. Yeni token yok, yeni üçüncü parti yok
  (nlohmann/json zaten vardı). Yeni dosyalar: `src/core/net/ReleaseCheck.{h,cpp}`,
  `src/app/controllers/UpdateController.{h,cpp}`, `tests/core/ReleaseCheckTests.cpp`, `tests/app/UpdateCheckTests.cpp`.
- **Aynı turdaki küçük işler** (gece listesinin 7. maddesi):
  - `tests/app/SourceTests.cpp` "formatRecentTime" saatten bağımsız: `now()` yerine bu makinenin saat diliminde sabit bir
    an (2026-06-15 12:00, `TzSpecificLocalTimeToSystemTime`) ve tam beklentiler ("bugün 11:59", "3 gün önce", aynı yıl
    yılsız, 400 gün önce yıllı); gece yarısını geçen durum ayrıca: 00:00:30'da bir dakika önce = "1 gün önce", 10 sn önce
    = "bugün 00:00". Zayıflatılmadı, atlanmadı (önce gece yarısını geçen koşuda düşebiliyordu).
  - `wlcli apply`: `ApplyReport::sessionLost` (D-131) varsa özet satırından sonra "stopped: DISM's session could not be
    opened again - <hata>; the later steps did not run, the image is still mounted and not saved". Özet satırı ve
    "(stopped)" aynı (lab betikleri `commit: held` vb. okuyor); çıkış kodu, durdurulan her çalışmada olduğu gibi 3
    (`apply`'ın JSON çıktısı yok). Sınıflandırma core'da: `core::ops::runStop(report)` → None / SessionLost / Stopped
    (birim testi). Gerçek bir oturum kaybıyla çalıştırılmadı (imaj bağlanmadı).
  - Hosts, Dosyalar, Kurulum Sonrası'nın ayrıntı kartı ve işaretleri: D-139'un eki.
  - Takas zinciri tampon sayısı (2 → 3): **yapılmadı** — ölçmek canlı pencere ister (bu işte pencere açılmadı).
Sonuç:
- Doğrulanan: `wlcli update-check --verbose --save=build\lab\update\releases.json` gerçek GitHub'a (2026-11-11 04:25):
  9 yayın, en yeni `v1.3.0-beta` "WinLove 1.3.0 Beta" (ön sürüm, 2026-11-09), çalışan 1.3.0 Beta → "up to date", sayfa
  `…/releases/tag/v1.3.0-beta`, saatlik kalan istek 59; gönderilen başlıklar yukarıdaki dört satır. `wlcli update-check
  --json --current=1.2.3-beta` → `"verdict": "available"` (gerçek veriyle "yeni sürüm" yolu). Birim testleri:
  `tests/core/ReleaseCheckTests.cpp` (8 vaka: gerçek ve tuhaf 35 etiket, 13 sıralama çifti, kaydedilmiş gerçek yanıt —
  tarih, notlar, sayfa, beş sürüm için hüküm —, taslaklar / sürümle en yeni / eşitlik / okunamayan etiketler, boş liste
  ve 6 bozuk yanıt + yanlış tipli girdiler, notlar, tarih ayrıştırma, güvenli adres (8 kötü adres), hata sınıfları;
  `runStop`), `tests/app/UpdateCheckTests.cpp` (6 vaka: 24 sa kuralı enjekte saatle — hiç / 23 sa / 24 sa / saat geri /
  kapalı —, aynı etikete ikinci bildirim yok, ayarların gidiş-dönüşü ve bozuk ayarlar, denetleyici sahte fetch ile —
  açılış denetimi, aynı gün yok, ertesi gün aynı sürüm bildirimsiz, daha yeni sürüm bildirimli, çevrimdışı sessiz ve zaman
  değişmez, ayar kapalıyken açılış yok / "Şimdi denetle" var —, Hakkında'nın 11 durumu ve düğmeleri, Ayarlar anahtarı,
  kabukta tek bildirim + durum çubuğu + Hakkında'da kalkması, kabuğun kendi başına ağa çıkamaması). Debug + Release
  `build.ps1 -Test` geçiyor (her ikisinde 556 vaka / 17.921 denetim). Görüntüler: Hakkında'nın beş durumu (koyu, açıkta
  "yeni sürüm"), bildirim, Ayarlar, D-139 ekinin üç sayfası (`build\lab\update\final\`; ara kareler `r1..r4`; 1100x700
  @1,0 ve İngilizce de bakıldı).
- Regresyon (`tools/render_diff.py`, önce = bu işten önceki Debug exe, sonra = yenisi, x64-9448.iso ile; 248 kare, iki
  tema): 177 aynı, 61 farklı, 10 "başarısız" (+ 2 kare iki exe'de de açılmayan `--demo-store=dialog`). 10 başarısızın
  hepsi eski exe'nin tanımadığı `--demo-update*` (5 durum × 2 tema; eski exe 1 ile çıkıyor). 61 farkın 41'i yalnız durum
  çubuğundaki "Boş alan" rakamı (bilinen gürültü; render sırasında derleme diske yazıyordu). Kalan 20: Ayarlar ve Ayarlar
  + ≡ menüsü (4; yalnız formun kaydırma çubuğu — yeni bölümle içerik uzadı, x 2045–2053), Hakkında (2; kahraman kartının
  altı: yeni kart, gerisi aşağı kaydı), Hosts / Dosyalar / Kurulum Sonrası demoları ve satırı seçili hâlleri (12; işaret
  sütunu, ayrıntı kartı, kartın yanında daralan / gizlenen sütunlar), Kurulum Sonrası + Wi-Fi diyaloğu (2; fark yalnız
  perdenin arkasındaki sayfada — diyaloğun içi aynı, `diffbox` ile denetlendi). Başka hiçbir sayfa / demo / diyalog
  değişmedi. Kareler `build\lab\update\diff\`, liste `build\lab\update\diff_log.txt`. Son exe ile bu karşılaştırmadaki
  "sonra" exe'si arasında yalnız kullanılmayan bir metnin silinmesi var (iki karede piksel farkı yalnız "Boş alan").
- Doğrulanmayan: canlı pencere (yalnız `--render`): açılıştan 4 sn sonraki gerçek denetim, gerçek pencerede bildirimin
  zamanlayıcısı, "Sürüm sayfasını aç"ın tarayıcıyı açması, "Şimdi denetle"nin pencerede tıklanması; gerçekten çevrimdışı
  / proxy arkasında / TLS hatası / GitHub saatlik sınırı (bunlar yalnız enjekte hatalarla test edildi; sınıflandırma
  `releaseFailure`'ın birim testinde); gerçekten daha yeni bir yayın (henüz yok — `--current` ve demo ile); GitHub
  yanıtının 20'den fazla yayına çıktığı gün (ilk sayfa yeter: en yeni her zaman ilk sayfada).
- **Açık, sahibinin kararı:** Hakkında'nın "Hiçbir kullanım verisi toplamaz, kimseye bir şey göndermez" cümlesi
  (`about.freeBody`, D-072'nin sözü) değiştirilmedi. Denetim veri göndermez, yalnız sürüm listesini okur; ama artık
  varsayılan olarak bir istek yapılıyor ve bu sahibine söylenmeli. Seçenekler: cümleyi yumuşatmak (denetimi ve
  kapatılabildiğini söylesin) ya da denetimi varsayılan kapalı yapmak (STATUS).
→ D-145 (2026-11-11): "hiçbir şey indirilmez" kuralı yalnız kullanıcı "Yükle"ye basınca gevşedi (ek indirilir, SHA-256 +
PE + sürümle denetlenir, takas edilir); tost iki düğmeli, Hakkında'da "Sürüm sayfasını aç" yerine Yükle · Değişiklikleri
gör; gövdeler Markdown penceresinde. Söz cümlesi hâlâ değişmedi; karar sahibinde (STATUS).

## D-139 — Çalışma sayfaları: ortak başlık, ayrıntı kartı, durum dili (2026-11-11; D-110 / D-111'in sütun başlığı yerini geçersiz kılar)
Bağlam: Özet ve Uygula ekranları (D-138) yeni görsel dili kurmuştu; kullanıcının asıl vakit geçirdiği yoğun sayfalar
(Bileşenler ~1.300 satır, Servisler, Kayıt Defteri, Özellikler, Sürücüler, Görevler, Güncellemeler, Uygulamalar,
Programlar, Diller …) hâlâ çıplak tablolardı. Her sayfanın özeti başka yerde ve başka biçimdeydi (Servisler'de araç
çubuğunun sağında, Kayıt Defteri'nde bölüm başlığının sağında, Sürücüler › İmajdaki'nde solda Body yazıyla, Bileşenler'de
hiç yok); birkaçı özeti her karede `paint()` içinde biçimlendiriyordu (Servisler, Sürücüler, Programlar), Güncellemeler
bir paketin dosyasını ilk çiziminde okuyordu. Satırın ayrıntısı yalnız ipucunda ya da tablonun altındaki tek satırdaydı;
yan kart yalnız Programlar'da vardı (Bileşenler'inki D-110'dan beri kullanılmıyordu). Durum işaretleri sayfadan sayfaya
değişiyordu: "kaldırılacak", "önerilen", "önyükleme için kritik" adların sonuna eklenmiş sözcüklerdi; risk bir sayfada
sözcüklü bir sütun, ötekinde yoktu. Sahibi: "yarışır kıvama gelsin", hareket işi yok, şeridin tasarımı aynı, soru yok
(koordinatörün sırasıyla 4. iş).
Karar (tek desen, bütün çalışma sayfalarında):
- **Başlık bölgesi** (`pages/PageHeader` — `SummaryLine`): sayfa kartının içinde, en üstteki kontrol satırı; solda
  sayfanın süzgeç / arama kontrolleri (süzgeci olmayan sayfada listenin başlığı: "Özel kayıtlar", "Kuyruktaki paketler",
  "Diller", "Dosyalar"), **sağda canlı özet**. Biçim: önce listenin ne olduğu sessizce (text.tertiary: "63 bileşen · 5,11
  GB", "214 servis", "9 / 10 açık", "18 / 38 görev kapalı"), sonra Uygula'nın ne yapacağı vurgunun okunur tonunda
  (accent.text: "3 kaldırılacak · 650 MB", "1 devre dışı bırakılacak", "2 değişecek"), sorun uyarı renginde ("1 uyumsuz",
  "1 bağımlılık eksik"); parçalar " · " ile. Metinler sayfanın zaten dinlediği AppState bildirimlerinde kurulur,
  genişlikler `layout()`'ta ölçülür, `paint()` yalnız çizer. Satır kısa kalınca önce sessiz parça düşer, sonra parça kısa
  biçimine geçer ("2 değişecek"), sonra "…" ile kesilir. Bileşenler ve Özellikler'in sütun başlıkları süzgeç satırından
  tablonun kendi başlık satırına indi (özet o satırın sağında; D-110 / D-111'in referans araç görünümü burada bırakıldı).
- **Ayrıntı kartı** (`pages/DetailCard` — `DetailModel`, `DetailCard`, `DetailSource`): yan panellerin genişliğinde
  (size::inspector), sayfa kartının yanında, sayfanın seçili satırı için. Sayfa yalnız modeli verir (zaten bildiği
  veriden; yeni okuma yok, katalog içeriği yazılmadı): 32'lik kutucuk (satırın ikonu / programın kendi ikonu / baş harf),
  ad (iki satıra kadar), sessiz alt satır, kimlik (mono: paket adı, servis anahtarı, yol); **durum satırı** (işaretlerin
  sözcüklü hâli: "Uygula'da kaldırılacak" vurguda, "Uyumluluk koruyor", "Kaldırılamaz", "İmajda kalır"); bir iki cümle
  açıklama (katalog notu, imajın servis açıklaması, özelliğin açıklaması, programın manifest metni); "Ayrıntılar" altında
  etiket / değer satırları (boyut, imajda, Uygula'da, risk noktası ve sözcüğü, şablon, hesap, yol …); notlar (koruyan
  bekçinin `CompatController::explain` sözleri + "Bileşenler › Uyumluluk'tan değiştirilebilir", kalıcı kilidin nedeni,
  risk / uyumsuzluk uyarısı); bölümler (İçerik, Bağlı olduğu, Bunu kullananlar, Cihazlar, Bağımlılıklar, .reg'in
  değerleri) kalan yere sığdığı kadar + "+n daha"; altta satırın eylemleri sessiz düğmeler olarak (Secondary; satırın
  tıklama / Space / Delete / menü eylemlerinin aynısı — aynı fonksiyonu çağırır). Seçim yoksa sayfanın ikonu + "Bir satır
  seç". Sayfa listesi boşken (bağlı imaj yok, okunuyor, okunamadı) kart çizilmez. Shell kartı `DetailSource` olan her
  sayfaya kendisi ekler (`Shell::attachDetailCard`); Programlar'ın ProgramInspector'ı bu karta taşındı (aynı içerik + "Bir
  satır seç"; `ProgramInspector::paintLetter` satırların küçük harf kutucuğu için duruyor). Seçim, klavye, çift tık ve sağ
  tık davranışı aynı.
- **Kırılma noktası** (`detailCardFits`): kart, sayfa kartı yanında en az `kDetailMinPageCard` = 600 DIP kalıyorsa
  görünür → gezinti açıkken pencere >= 1180 DIP (1207: sayfa kartı 627 kalır, kart var; en küçük pencere 1100: 519
  kalırdı, kart yok — tablolar sıkışmasın; gezinti daraltılınca 1100'de de görünür). Kart yanındayken tablo, kartın zaten
  söylediği sütunları sırayla bırakır (`fitColumns`; yalnız sığmayınca: Servisler'de Ad, Kayıt Defteri'nde Değer / Kapsam,
  Özellikler'de Açıklama, Sürücüler'de Sağlayıcı / Sürüm / Tarih, Görevler'de Kategori, Güncellemeler'de KB / Tür / Boyut,
  Uygulamalar'da Yayıncı / Mimari / Sürüm, Programlar'da Kimlik, Diller'de Bileşen dilleri / Boyut), ad sütunu 200'e kadar
  daralabilir; kart yokken her sütun eskisi gibi. Tablonun altındaki tek satırlık ayrıntı (Görevler, Uygulamalar,
  Sürücüler › İmajdaki) kart yanındayken çizilmez, yokken yerinde. `ui::TableView`: `TableColumn::hidden`,
  `setColumnHidden`, `minimumWidth`, hücreye özgü ipucu `cellTooltip` (sayfadan bağımsız; ui imaj bilmez).
- **Durum dili** (`PageBits.h` — `RowMarks`, `paintMarks`, `marksTooltip`): adın hemen sağında, kendi dar sütununda, her
  işaret kendi yuvasında (alt alta hizalı):

  | İşaret | Çizim | Anlamı | İpucu / kart |
  |---|---|---|---|
  | Kuyrukta | QueueClock, accent.text | Uygula'da değişecek | sayfanın sözü: "Uygula'da kaldırılacak", "Uygula'da: Devre dışı" |
  | Korunuyor | ShieldCheck, text.tertiary | bir uyumluluk bekçisi tutuyor (Uyumluluk'tan kaldırılabilir) | "Uyumluluk koruyor: " + bekçinin sözü |
  | Kilitli | Lock, text.tertiary | burada değiştirilemez (kalıcı yetenek, zorunlu bileşen) | "Değiştirilemez: " + neden |
  | Risk | 6 px nokta: status.warning (Dikkat) / status.error (Riskli); güvenlide yok | değiştirmenin riski | "Risk: Riskli — not" |
  | Önerilen | PresetBookmark, text.tertiary | şablon / katalog öneriyor (Görevler) | "Önerilen" |

  Onay kutusunun kendisinin "kuyrukta" demek olduğu listelerde (Kayıt Defteri, Sürücüler › Eklenecek / Kurulum ortamı)
  saat işareti tekrarlanmaz. Riskin sözcüklü sütunu (Servisler, Görevler) noktaya indi; sözcük ipucunda ve kartta.
  Bileşenler'de risk noktası yalnız kataloğun bildiği öğede (envanterden gelen sürücü / yazı tipi "orta" varsayılanıyla
  1.300 satırı noktalamasın). Grup satırları (Bileşenler / Özellikler kategorileri, Sürücüler sınıfları, Programlar
  grupları) yarı kalın (BodySemi; eskiden BodyStrong = 400 olduğundan aslında kalın değildi), "n/m" ve boyut sağda; ağaç
  girintisi her sayfada 16 (`kTreeIndent`). Bir ad ya da kategori kesilince tamamı ipucunda.
- **Boş / okunuyor / hata:** okunurken EmptyState'te dönmeyen Spinner glifi yerine sayfanın kendi ikonu ve sessiz satır
  (Bileşenler, Servisler, Özellikler, Sürücüler › İmajdaki, Programlar); hata durumları zaten nedenini ve "Yeniden
  dene"yi veriyordu (dokunulmadı).
- Kodda ayrıca: Kayıt Defteri ve Güncellemeler satır metinlerini `refresh()`'te önbelleğe alır (paint'te biçimlendirme,
  ölçüm ve Güncellemeler'de dosya okuma kalktı); Kayıt Defteri'nin satır silme ✕'i kendi dar sütununda; `SearchBox` dar
  kutuda yer tutucuyu tuş ipucunun altına değil önüne keser; Diller'in bölge alanları dar kartta taşmaz. **Hata
  düzeltmesi:** Bileşenler'de bir bileşen seçiliyken imaj ayrılınca (ya da gruplar küçülünce) `rebuildRows()` eski
  satırın indisleriyle yeni grup listesini okuyordu (Debug'da "vector subscript out of range", Release'te tanımsız
  davranış) — seçimin anahtarı artık gruplar değişmeden önce okunuyor (yeni birim testi yakaladı). Render:
  `--toast-timeout` (demo bildirimi kartın altını örtmesin), `--click-after-load=x,y` (Programlar'ın dizini okunduktan
  sonra satır seçmek); ikisi ve seçili satırlı sayfa durumları `tools/render_diff.py` envanterinde.
- Yeni token yok. 106 yeni metin anahtarı (tr + en, aynı yer tutucular; `detail.*`, `marks.*` ve sayfa grupları altında);
  mevcut hiçbir değer değişmedi (eski exe'nin gömülü metinleriyle karşılaştırıldı — çalışma sırasında üç anahtar
  çakışması yakalanıp geri alındı).

| Sayfa | 1 başlık / özet | 2 ayrıntı kartı | 3 gruplama + durum dili | 4 durum sütunu, hizalı sayılar, kesme | 5 boş / okunuyor / hata |
|---|---|---|---|---|---|
| Bileşenler | ✓ | ✓ (bileşen + kategori / düğüm) | ✓ | ✓ | ✓ |
| Servisler | ✓ (paint'ten çıktı) | ✓ (açıklama, hesap, dosya, bağlı olduğu / kullananlar, bekçi) | ✓ (düz liste: işaretler) | ✓ (Risk sütunu → nokta) | ✓ |
| Kayıt Defteri | ✓ (başlık + özet) | ✓ (değer / .reg'in değerleri; Düzenle · Aç-Kapat · Kaldır) | kısmen (düz liste, işaret gerekmiyor; ✕ kendi sütununda) | ✓ (".reg · n değer" addan çıktı; metinler önbellekte) | aynı (zaten tutarlı) |
| Özellikler | ✓ | ✓ (öğe + kategori / dil grubu) | ✓ | ✓ (başlıklar tabloya, açıklama kartta) | ✓ |
| Sürücüler (3 sekme) | ✓ (üç sekmede aynı yer; paint'ten çıktı) | ✓ (INF, sınıf, boot.wim'dekiler, imajdaki sürücü) | ✓ İmajdaki; Eklenecek / Kurulum ortamı kısmen (rol sözcüğü hücrenin sağında hizalı kaldı) | ✓ İmajdaki ("kaldırılacak" / "kritik" / oemN addan çıktı) | ✓ |
| Görevler | ✓ | ✓ | ✓ ("önerilen" işaret oldu, risk noktası) | ✓ | aynı |
| Güncellemeler | ✓ (başlık + özet + uyumsuz uyarısı) | ✓ | — (sıralı düz liste; Durum sütunu zaten ikon + söz) | ✓ (paint'te dosya okuma kalktı) | aynı |
| Uygulamalar | ✓ (sekme satırının sağında) | ✓ (Eklenecek; Varsayılan sekmesinde kart yok) | — | ✓ (paket dosyası addan çıktı) | aynı |
| Programlar | ✓ (paint'ten çıktı) | ✓ (ProgramInspector → ortak kart) | ✓ (grup satırı yarı kalın) | kısmen | ✓ |
| Diller | ✓ (başlık + özet) | ✓ | — | kısmen | aynı (okunuyor / hata satırı zaten vardı) |
| Hosts | ✓ (SummaryLine) | ✗ → ek'te ✓ | ✗ → ek'te işaret sütunu | ✗ → ek'te Risk sütunu → nokta | aynı |
| Dosyalar | ✓ (başlık + özet; alttaki satır kalktı) | ✗ → ek'te ✓ | ✗ | ✗ → ek'te risk yuvası (Risk sözcük sütunu kalktı) | aynı |
| Kurulum Sonrası | ✓ (SummaryLine, paint'ten çıktı) | ✗ → ek'te ✓ | ✗ (adımların riski / kendi durumu yok) | ✗ | aynı |

Ek (2026-11-11, D-140 ile aynı turda): eksik üç sayfa (davranış aynı; kart düğmeleri satırın kendi tıklama / tuş
eylemini çağırır):
- **Hosts:** işaret sütunu (kuyrukta saat · risk noktası · önerilen; Risk sözcük sütunu kalktı, sözcüğü ipucunda ve
  kartta); ayrıntı kartı: açıklama, durum ("Uygula'da hosts dosyasına yazılacak" / "…kaldırılacak" / "İmajın hosts
  dosyasında var" / "Seçili değil"), Girdi, Durum, Risk, Önerilen ve **engellenen alan adları** (tablo yalnız sayıyı
  veriyordu; kalan yere sığdığı kadar + "+n daha"); düğme Engelle / Engeli kaldır, içe aktarılan satırda İçe aktar… /
  Girdileri temizle. Satır durumları `refresh()`'te önbelleğe alındı (paint kuyruğa sormaz). Kart yanındayken tablonun
  altındaki satır çizilmez.
- **Dosyalar:** Risk sözcük sütunu → Kaynak'tan sonra yalnız risk yuvalı işaret sütunu (`paintRiskMark`; liste kuyruğun
  kendisi, her satıra saat koymak bir şey söylemezdi), ipucu "Risk: Riskli — Windows ya da Program Files altına
  yazılıyor…"; kart: ad, Klasör / Dosya · boyut, "Uygula'da imaja kopyalanacak", kaynağın tam yolu ve kurulan Windows'taki
  yeri (sarılı metin; tablo yolu kesiyor), İmajdaki yer / Boyut / Tür / Risk, riskliyse uyarı notu; düğme "Kuyruktan
  çıkar" (= Delete). Kart yanındayken gerekirse Boyut sütunu kartın.
- **Kurulum Sonrası:** kart: adım adı ve türü, "İlk oturumda n. adım olarak çalışacak" / "Kurulum sonunda …", komutun
  tamamı (tablo kesiyor) / kopyalamanın kaynak → hedefi / winget kimliği, Tür / Sıra / Çalıştırma / Bekle / Hata olursa,
  Wi-Fi'de yalnız ağ adı (profil anahtar içerir, gösterilmez), geçersiz adımda hata notu; düğmeler Düzenle (= Enter) ·
  Bitmesini bekle / Beklemeden geç (yalnız komut; = "Bekle" etiketinin tıklaması) · Sil (= Delete). Adımların riski ve
  kendi durumu yok (hepsi plan): işaret sütunu eklenmedi. Kart yanındayken Kaynak, gerekirse Tür sütunu kartın.
- Boş durumlar diğer sayfalarla aynı (kart, tablo görünürken "Bir satır seç"; bağlı imaj yokken kart yok). Testler:
  `DetailCardTests.cpp`'ye üç vaka. 29 yeni metin (hosts 11, files 6, postsetup 12). Regresyonu D-140'ın Sonuç'unda.
Sonuç:
- Doğrulanan: Debug + Release `build.ps1 -Test` geçiyor; yeni `tests/app/DetailCardTests.cpp` (9 vaka): kırılma noktası
  (1207 → kart, 1100 → yok, sınırın iki yanı), `fitColumns` (sırayla bırakma, gizli sütunun genişliği, kartsız hepsi
  geri), `SummaryLine` (sessiz parçanın düşmesi, kısa biçim, boş parça), Bileşenler / Servisler / Kayıt Defteri /
  Özellikler / Görevler'in kart modeli (seçim yok · düz satır · kuyruktaki satır · bekçinin tuttuğu satır / kapalı değer;
  kart düğmesinin satırın kendi eylemini yapması; birleşik özetlerin metni; imaj ayrılınca kartın düşmesi), işaret
  sözlüğü. `PagesPolishTests`'te iki denetim yeni yerlere çekildi (başlıklar artık tablonun başlık satırında, işaret
  sütunu sütun indislerini bir kaydırdı). Görüntüler: her sayfa koyu + açık 1207x700 @1,75, en küçük pencere 1100x700
  @1,0 (kart yok, tablolar eski düzende), saf siyah render edilip bakıldı (`build\lab\dense\r1..r6`, son kareler
  `build\lab\dense\final\`).
- Regresyon (`tools/render_diff.py`, önce = bu işten önceki Debug exe, sonra = yenisi, x64-9448.iso ile): 232 kare (iki
  tema): 153 aynı, 75 farklı, 4 "başarısız" (+ 2 kare iki exe'de de açılmayan `--demo-store=dialog`). 4 başarısızın hepsi
  eski exe'nin tanımadığı yeni durumlar (`--toast-timeout`, `--click-after-load`; eski exe 1 ile çıkıyor). 75 farkın 64'ü
  bu işin sayfaları (yan kart, başlık satırı, işaret sütunu, grup satırları; diyaloglu durumlarda — Uyumluluk, Değeri
  düzenle, Görev oluştur, Dil ekle, güncelleme kataloğu — farklar yalnız diyaloğun arkasındaki sayfada, diyalog aynı), 4'ü
  `--demo-apply=running|paused` (sayfa verilmeyince Özellikler'i gösterir), 5'i durum çubuğundaki "Boş alan" rakamı, 2'si
  Uygula demosunun log satırlarındaki saat (ikisi de bilinen gürültü). Başka hiçbir sayfa / demo / diyalog değişmedi.
  Kareler `build\lab\dense\diff\`, liste `build\lab\dense\diff_log.txt`.
- Doğrulanmayan: canlı pencere (yalnız `--render`): kart düğmelerine gerçek tıklama, klavyeyle gezinirken kartın
  güncellenmesi, pencere boyutu değişirken kartın açılıp kapanması; gerçek bağlı imajda ~1.300 satırlı Bileşenler (özet
  her kuyruk bildiriminde bütün bileşenleri mevcut bayrak önbelleğiyle bir kez gezer — ölçülmedi); yüksek karşıtlık
  temaları; Programlar'da ayrıntıların ağdan gelmesi (render'da "Ayrıntılar yükleniyor…" kaldı).

## D-138 — İmaj özeti ve Uygula ekranı (2026-11-11)
Bağlam: Bir imaj açılınca kullanıcı sürüm tablosuna, oradan da yoğun tablolara düşüyordu; hiçbir ekran "neredeyim"
demiyordu (bu imaj ne, içinde ne var, kuyrukta ne var, ne kadar küçülecek, sırada ne var). Uygula ekranı ise ürünün en
görünür anı olduğu hâlde özetinde çoğu "0 –" olan 7 sayaç, ayrı bir süre InfoBar'ı; çalışırken 2 px bir çizgi ve iki
satırlık liste; bitince bir InfoBar ve 6 sayaçtan ibaretti. Sahibi: "arayüz yarışır kıvama gelsin", hareket işi yok,
şeridin tasarımı aynı; soru sorulmadan, render'a bakılarak (koordinatörün sırasıyla 2. ve 3. iş). Sayfa ayrıntıları
`docs/pages/02-images.md` §2b ve `05-apply.md`.
Karar:
- **Yer:** Görüntü kartına üçüncü sekme **Özet** (`PageId::Overview`, `--page=overview`): Kaynak seç · Sürümler · Özet.
  İmaj açılınca yine Sürümler gelir (alışılan tablo yerinde, kendiliğinden sayfa değişmez); Özet, Görüntü'nün son açılan
  alt sayfası olarak hatırlanır; durum çubuğunun imaj bölümü (kaynak açıkken) ve komut paleti oraya götürür. Şerit
  Sürümler'inkiyle aynı (`Shell::addImageActions`), yan kart yok.
- **Ne gösterir** (`pages/overview/OverviewModel` — `buildOverview(AppState, OverviewInputs)` saf fonksiyon;
  `OverviewInputs` = Shell'in denetleyicilerinin zaten bildiği: nav sayıları, `CompatController::impact()` / `onCount()`,
  `ApplyController::highRisk()` + plan tahmini, `ComponentController::groups()`, `canMount` / `isPackedSource`): kimlik
  kartı (ad, rozet, sürüm · build · sürüm kimliği · mimari · dil · boyut; dosya · sıkıştırma · dosya boyutu · sürüm i / n
  · kaynak; bağlama klasörü) + **Sıradaki adım** cümlesi ve sayfanın tek dolu düğmesi (Kaynak seç / Bağla / ESD → WIM /
  Bileşenler / Uygula'ya geç / İlerlemeyi gör / Sonuca bak / ISO Oluştur; iş sürerken düğme yok); uygulama kartı
  (çalışırken ya da son çalışma hâlâ haberken); **İmajda ne var** — 7 kutucuk (bileşenler kategorileriyle, sağlanan
  uygulamalar, özellikler, üçüncü parti sürücüler, diller, servisler, kuyruktaki güncellemeler), her biri sayfasına;
  **Kuyruk** — gezintinin gruplarıyla çipler, boyut önce → ≈ sonra + çubuk, süre, yüksek riskli adımlar, uyumluluk
  raporu, bağlanınca kuyruğa girecek indirmeler.
- **Okunmamış liste:** "henüz okunmadı" / "okunuyor…" / "okunamadı" / "bağlanınca okunur"; dönen gösterge, uydurma sayı
  ve özet için liste okuması yok (yeni motor çağrısı yok). Diller kaynak açıkken bile WIM XML'inden bilinir.
- **Çizim kuralı:** model ve metinler AppState bildirimlerinde (pahalı girdiler parça parça: `OverviewPage::Part`),
  genişlikler `layout()`'ta; `paint()` yalnız hazır metinleri çizer.
- **Uygula ekranı** (davranış ve akış aynı; şerit eylemleri ve bağlantıları değişmedi): her modun başında bir baş kartı
  (`pages/apply/ApplyCards`) ve ekranın tek dolu düğmesi orada. Özet: plan kartı (değişiklik · aşama, imaj, uyku notu,
  boyut önce → sonra + çubuk + kazanç, süre aralığı, **Uygula · n**); plan tablosuna "Boyut etkisi" sütunu ve grubun
  yüksek riskli adım sayısı; 7 sayaçlı şerit ve süre InfoBar'ı kalktı. Çalışıyor: aşama + adım + büyük yüzde, "9 / 40 adım
  · geçen · kalan", 6 px ölçer, tamam / atlandı / bekliyor; kart yüksekliği sabit, liste + log yerinde (geçen süre
  saniyede bir, sayfanın mevcut 250 ms zamanlayıcısıyla). Duraklatıldı: neden + üç seçim kartın içinde, kalan süre tahmini
  yok. Bitti: sonuç kartı (durum şeridi, sonuç cümlesi, süre, ne olduğu düz sözlerle, boyut önce → sonra + çubuk, kazanç,
  adımlar, sıradaki adım: ISO Oluştur / Kaydet… / Görüntü'ye git + rapor / preset / log) ve altında mevcut sonuç tablosu +
  atlanan adımların nedenleri. D-131'in `sessionLost`'u durdurulan çalışmanın kartında söyleniyor (wlcli tarafı D-140'ın
  küçük işlerinde).
- Ortak sayılar: `pages/apply/RunSummary` (`summarizeRun`, `planRows`, `planSeconds`). Yeni çizim yardımcıları:
  `PageBits.h` `paintSizeBar` (önce → sonra çubuğu), `paintMeter` (yuvarlak ölçer). Yeni token yok (`sizeBar` D-137'de
  vardı). Erişimci: `AppState::heldDownloads() const`.
- Render durumları: `--demo-overview[=unread|running|paused|done|held]` (kaynak yoluyla ya da yolsuz; `--page=apply` ile
  Uygula ekranları), render_diff envanterinde.
Sonuç:
- Doğrulanan: Debug + Release `build.ps1 -Test` geçiyor (yeni `tests/app/OverviewTests.cpp`: 12 vaka, 181 denetim — kaynak
  yok / açık ama bağlı değil / bağlanıyor / bağlı ama listeler okunmamış / okunmuş / kuyruk ve boyut farkı / uyumluluk
  raporu / çalışıyor / bitti — kaydedildi, bekletildi, kuyruk değişince düşer; çalışma özeti her sonuçla; plan satırları;
  iki sayfa her modda pencere olmadan kurulur). Görüntüler koyu + açık, 1207x700 @1,75, 1100x700 @1,0 ve saf siyahta
  render edilip bakıldı (`build\lab\overview\r1..r3`, son kareler `build\lab\overview\final\`).
- Regresyon (`tools/render_diff.py`, önce = bu işten önceki Debug exe, sonra = yenisi, x64-9448.iso ile; 208 kare): 149
  aynı, 25 farklı, 34 "başarısız". 34'ün hepsi eski exe'nin tanımadığı yeni durumlar (`--page=overview`,
  `--demo-overview*`; eski exe 1 ile çıkıyor). 25 farkın 12'si Sürümler sayfasının kartındaki yeni "Özet" sekmesi (her
  karede aynı 333 piksel, 695–741 × 226–246), 8'i Uygula'nın bitti / atlandı / bekletildi karelerinin yeni sonuç kartı ve
  onay dialoğunun arkasındaki yeni özet (dialog aynı), 5'i durum çubuğundaki "Boş alan" rakamı (bilinen gürültü). Başka
  hiçbir sayfa / demo / diyalog değişmedi. (Çalışıyor / duraklatıldı demoları sayfa verilmeyince Özellikler'i gösterir: o
  yüzden aynı; yeni çalışma kartı `--demo-overview=running|paused --page=apply` karelerinde.)
- Doğrulanmayan: canlı pencere (yalnız `--render`): kutucuk / çip tıklaması, klavye ile gezinme, kaydırma; gerçek bir
  Uygula çalışması (demo durumları elle kurulmuş `ApplyRun`); bağlı bir imajda `ComponentController::groups()`'un özet
  açıkken her bileşen bildiriminde maliyeti (~1.300 öğe, ölçülmedi); Windows'un yüksek karşıtlık temaları.
→ 2026-11-11: Kuyruk çipleri artık işlem sayısı, işlemi koyan sayfanın sekmesinde (Kurulum › Katılımsız, Diğer dahil),
boyut değişmeyince nötr çubuk, durdurulan çalışmadan sonra kaydet / çöz kartta görünür (D-148); sonuç kartında ve çalışma
kartında otomatik ISO bandı, yeni sıradaki adım `IsoReady` (D-144); dönen gösterge D-142'nin halkası.

## D-137 — WinLove'un kendi renkleri (2026-11-11; D-109'un renklerini geçersiz kılar)
Bağlam: D-136'dan sonra yazı, yoğunluk ve köşeler WinLove'undu ama renkler hâlâ referans aracın ölçülen grileri ve
Windows'un vurgusuydu (#4CC2FF siyah yazılı) — uygulamanın "kopya" gibi görünmesinin en büyük nedeni. Kodda da roller
karışmıştı: koyu temada `bg.input` = `bg.raised` (#333333), form kartındaki açılır listeler ve metin kutuları
görünmüyordu; durum çubuğu kart rengindeydi; seçili gezinti öğesi gri (bg.pressed), seçili tablo satırı mavide gri
(#3E3E3E), diğer vurgularda renkli; açılır menünün üzerine gelinen satırı menüden bir ton farklıydı; açma / kapama
düğmesinin topu koyu temada siyahtı; 16 yerde metin vurgunun dolgu renginde yazılıyordu (Bakır / Deniz / Nar / Zeytin'de
koyu zeminde ~3:1). Sahibi 2026-11-11 gece: "Arayüzü sana bırakıyorum, [referans araçla] yarışır kıvama gelsin" ve "Bana
bir şey sorma, sabaha her şey bitmiş olsun": koordinatör kararı (onay kapısı yok, hareket işi yok, şeridin tasarımı aynı;
her görünür adım render'la raporlandı). Koordinatörün sırası: 1 kimlik ve renk (bu kayıt), 2 imaj özeti, 3 Uygula ekranı
(D-138), 4 yoğun sayfalar (D-139), sonra güncelleme denetimi (D-140) ve VHD / VHDX (D-141).
Karar: `design/tokens.json` 5.0.0 — hafif soğuk (arduvaz) eğilimli nötr yüzeyler; sayfa en derin katman, çerçeve (başlık
çubuğu, şerit, durum çubuğu, gezinti kartı) ve içerik kartı onun üstünde; giriş alanı koyu temalarda içe gömük, basma
düğmesi yeni `bg.control` ile her yüzeyden açık; vurgu her temada tek dolgu + beyaz yazı (Mavi de artık diğer dördü gibi);
`accent.text` / `accent.focus` vurgunun okunur tonu, `accent.subtle` seçim dolgusu (vurgu, kartın üstünde %20 / %10 /
%24).

| Koyu | Eski | Yeni | Neden |
|---|---|---|---|
| Sayfa (bg.base) | #202020 | #17191C | daha derin zemin; kartlar onun üstünde |
| Başlık + durum çubuğu (bg.titleBar; durum çubuğu bg.panel'di) | #202020 / #2B2B2B | #1C1E22 | ikisi de çerçeve |
| Şerit + gezinti kartı (bg.chrome) | #262626 | #1F2125 | çerçeve tonu |
| İçerik kartı (bg.panel) | #2B2B2B | #25282C | içerik; gezintiden bir ton açık |
| Kart içi grup (bg.raised) | #333333 | #2D3035 | gruplar karttan ayrı |
| Menü / diyalog / ipucu (bg.overlay) | #2C2C2C | #2A2D32 | kenar + gölgeyle yüzer |
| Basma düğmesi (bg.control, yeni) | (bg.input #333333) | #34383D | düğme alandan ayrıldı |
| Giriş alanı (bg.input) | #333333 | #1B1D21 | içe gömük: alan alan gibi okunur |
| Basılı (bg.pressed) | #3A3A3A | #3B3F45 | |
| Seçim (accent.subtle) | #3E3E3E | #27364F | gri değil, vurgunun tonu |
| Çizgi ince / güçlü | #353535 / #474747 | #34373C / #484C53 | kart kenarı görünür, ağır değil |
| Metin 1 / 2 / 3 / pasif | #FFFFFF / #B8B8B8 / #858585 / #5C5C5C | #ECEEF1 / #B1B6BE / #8A9099 / #62676F | saf beyaz yerine yumuşak |
| Vurgu dolgusu (accent.base) | #4CC2FF (siyah yazı) | #3070D9 (beyaz yazı) | WinLove mavisi, neon değil |
| Vurgu metni / odak | #4CC2FF / #4CC2FF | #74A3EA / #74A3EA | her yüzeyde >= 4,6:1 |
| Başarı / uyarı / hata / bilgi | #39B54A / #F4D534 / #F54137 / #4CC2FF | #57C27C / #E2AC45 / #F27B72 / #6FA8F2 | tek aile; limon sarısı yerine kehribar |
| Durum dolguları | #253A28 / #3D3720 / #442726 / #1F3A47 | #24392F / #3D3322 / #432B2C / #253650 | |
| Şablon (gizlilik / genel / hafif / önerilmez) | #39B54A / #3C7BA7 / #F4D534 / #F54137 | #57C27C / #5E95DA / #E2AC45 / #F27B72 | anlam aynı, tonlar durum renkleriyle bir |
| İkon mavi / amber / yeşil / kırmızı / mor / camgöbeği / arduvaz | #4CC2FF / #FFB13B / #5FD068 / #FF6B5E / #B794F6 / #3DD6C6 / #A9B7C6 | #6CA6F0 / #E9AA48 / #5FC27F / #EF766B / #A996EE / #47C2B5 / #A3ADBA | biraz sakin, durum renkleriyle uyumlu |
| Boyut çubuğu (sizeBar) | (kullanılmıyordu) | #2F343C | Bileşenler'in boyut çubuğu seçimden ayrı |

| Açık | Eski | Yeni | Neden |
|---|---|---|---|
| Sayfa | #EEEEEE | #ECEEF1 | en derin katman |
| Başlık + durum çubuğu | #E6E6E6 / #FBFBFB | #F3F4F6 | çerçeve |
| Şerit + gezinti kartı | #F5F5F5 | #F7F8FA | |
| İçerik kartı | #FBFBFB | #FFFFFF | temiz beyaz kart |
| Kart içi grup | #F0F0F0 | #F5F6F8 | |
| Menü / diyalog | #FBFBFB | #FFFFFF | |
| Basma düğmesi (yeni) | — | #FFFFFF | |
| Giriş alanı | #FFFFFF | #FFFFFF | gruptan beyaz, kenarla |
| Basılı | #E3E3E3 | #E6E9ED | |
| Üzerine gelme | #0000000A | #1C2A400D | soğuk, nötr |
| Seçim | #E3E3E3 | #EAF1FB | gri değil, vurgunun tonu |
| Çizgi ince / güçlü | #E3E3E3 / #CFCFCF | #E1E4E8 / #C9CED5 | |
| Metin 1 / 2 / 3 / pasif | #1B1B1B / #525252 / #858585 / #B5B5B5 | #1A1D21 / #4A515B / #6B727C / #A0A6AF | üçüncül metin daha okunur |
| Vurgu dolgusu | #0067C0 | #3070D9 | iki temada aynı dolgu |
| Vurgu metni / odak | #0058CD / #0058CD | #2462C7 / #3070D9 | |
| Başarı / uyarı / hata / bilgi | #0F7B0F / #9D5D00 / #C42B1C / #0058CD | #1A7535 / #8F5A00 / #C3352B / #2361C6 | |
| Durum dolguları | #DFF6DD / #FFF4CE / #FDE7E9 / #E3EEFA | #E7F4EA / #FCF1DC / #FCEAE8 / #E7EFFC | |
| Şablon | #39B54A / #0058CD / #F4D534 / #F54137 | #2E9A4E / #3B76CF / #B57F0E / #CF3B33 | sarı beyazda okunmuyordu |
| İkonlar | #0067C0 / #C77700 / #1F8A2E / #C42B1C / #7A4FCF / #00837A / #5A6B7D | #2A66C9 / #B86E00 / #1F7F3A / #C8392D / #6D4FC6 / #0D7C70 / #5B6676 | |
| Diyalog perdesi | #00000066 | #0F172A59 | kirli gri yerine soğuk, hafif |

| Saf siyah | Eski | Yeni |
|---|---|---|
| Sayfa | #000000 | #000000 |
| Başlık + durum / şerit + gezinti | #000000 / #0D0D0D / #080808 | #08090A / #0A0B0D |
| Kart / grup / menü | #0D0D0D / #171717 / #0F0F0F | #101114 / #181A1D / #16181B |
| Düğme (yeni) / basılı / giriş | — / #1F1F1F / #161616 | #1E2125 / #25282C / #060708 |
| Seçim | #1C2630 | #182843 |
| Çizgi ince / güçlü | #1E1E1E / #303030 | #222428 / #383B41 |
| Metin, vurgu, durum, ikon, şablon | koyuyla aynı (eski) | koyuyla aynı (yeni); durum dolguları #13261C / #2A2213 / #2E1A1B / #142238 |

Yüksek karşıtlık kendi setinde kalır; yalnız seçim dolgusu tam camgöbeğinden (#1AEBFF, üstünde beyaz yazı) koyu
#00363D'ye indi ve `bg.control` (#000000) eklendi. Vurgu seçenekleri (`ui/theme/Palette.cpp`): Mavi #3070D9, Bakır
#AF5F29, Deniz #147971 → #177F77, Nar #C13C5C → #C64262 (ikisi koyu kartta 3:1 işaret olsun diye bir ton açık), Zeytin
#5F7A2E; üzerine gelme %8, basma %16 koyulaşır (beyaz yazı korunur); her vurgu için koyu / açık okunur ton ve seçim
dolgusu, saf siyah için ayrı seçim dolgusu (`build\lab\identity\derive.py` ile türetildi).

Vurgunun kullanımı (her yerde aynı): seçili şey = `accent.subtle` dolgusu + vurgu işareti (gezinti öğesi: yeni 3 DIP sol
çubuk; tablo satırı: mevcut 2 px çubuk; sekme: alt çizgi; bölümlü seçim: dolgu). Sayfanın / diyaloğun tek ana eylemi dolu
vurgu düğmesi, gerisi sessiz. İlerleme, odak halkası (okunur ton), bağlantılar, sayaçlar ve "değiştirilecek" yazıları
`accent.text`; işaretli onay kutusu ve açık anahtar vurguda (Bileşenler / Özellikler'in "kalır" ağaç işareti nötr kalır).
Kodda düzeltilen yanlış token kullanımları: `StatusBar` (bg.panel → bg.titleBar), `Button` Secondary (bg.input →
bg.control), `MenuPopup` ve komut paleti satırı (bg.raised → bg.hover), `Checkbox::paintBox` (açıkken accent.text),
`NavPanel` (seçim bg.pressed → accent.subtle + çubuk; sayaç rozeti %20 vurgu → accent.subtle), `RadioGroup` (iz bg.raised
→ bg.input, seçili bg.panel → accent.subtle), Bileşenler boyut çubuğu (accent.subtle→accent.hover yerine
sizeBar→accent.text), Simgeler listesi / ızgarası (üzerine gelme bg.panel = görünmüyordu → bg.hover, seçim bg.raised →
accent.subtle), Başlat sabitleri ızgarası (bg.pressed / bg.raised → bg.hover / accent.subtle), vurgu dolgusuyla yazılan 16
metin → accent.text (Sürücüler, Hosts, Görevler, Simgeler ×3, Diller ×3, Programlar ×3, Kayıt Defteri, Kaynak, Ayarlar,
Katılımsız XML); `refreshSystemContrast` yeni / çerçeve tokenlarını da Windows renklerine bağlar.
Sonuç:
- Doğrulanan: Debug + Release `build.ps1 -Test` geçiyor; yeni `tests/ui/PaletteTests.cpp` (5 vaka, 1360 denetim): her
  temada ve her vurguyla metin 1 / 2 her yüzeyde (sayfa, çerçeve, kart, grup, menü, giriş, düğme, basılı, seçim, üzerine
  gelme, dört durum dolgusu) >= 4,5:1, metin 3 >= 3:1; vurgu metni ve durum kelimeleri >= 4,5:1; beyaz yazı vurgu dolgusu
  / üzerine gelme / basmada >= 4,5:1; vurgu işareti sayfa, gezinti ve kartta >= 3:1; ikon ve şablon renkleri >= 3:1; seçim,
  üzerine gelme ve kart birbirinden ayrı; pasif metin >= 2,2:1. En düşükler: koyu metin 3 basılıda 3,29, açık 3,99; vurgu
  metni 4,67; durum kelimesi 4,67; beyaz-üstü-vurgu 4,68 (Bakır). `TokensTests` yeni değerlere çekildi. Bütün envanter
  (sayfalar, demo durumları, diyaloglar, menüler) koyu + açık, saf siyah / yüksek karşıtlık ve beş vurgunun her biri iki
  sayfada render edilip bakıldı.
- Geometri: `render_diff` 174 karenin hepsinde renk farkı; `build\lab\identity\geometry.py` (eski ve yeni karenin tek
  renkli bölgeleri birbirine tek renkle eşlenmeli; kayan kenar iki yönde de tutarsız piksel bırakır; ayrıca kaybolan /
  yeni kenarlar) 172 karede 0 tutarsız piksel; kalan 2 kare (Simgeler demosu) eski exe'nin başka klasörde durmasından
  (demo marka ikonunu exe'ye göre arar) — iki exe aynı derinlikte render edilince 0. Kaybolan kenarlar yalnız yuvarlak
  köşelerde tek piksellik kenar yumuşatma çakışmaları ve durum çubuğundaki "Boş alan" rakamı (canlı veri; eski exe'nin
  kendisiyle karşılaştırmasında da değişiyor). Yeni kenarlar: gezinti çubuğu (bilerek eklendi) ve eskiden aynı griyi
  paylaşan tokenların ayrışması (tablo başlık çizgisi ile seçili satır, giriş ile grup).
- Doğrulanmayan: canlı pencere (yalnız `--render`); pencere çerçevesinin 1 px vurgu kenarı (DWM, sahibinin isteği; artık
  yeni vurgu); Windows'un yüksek karşıtlık temaları açıkken; gerçek ekranlarda / farklı gama ve gece ışığında algı.
  Görüntüler: `build\lab\identity\final\` (önce / sonra çiftleri).
→ D-143 (2026-11-11): vurgu artık herhangi bir renk (Ayarlar › "Özel renk", kurallarla türetilir); beş hazır renk bu
tablolarla kalır, mevcut kullanıcı için değişen yok. D-142 ilerleme halkası `accent.text` ile çizer.

## D-136 — Yeni görünüm: 13 px yazı, ferah satırlar, yuvarlak köşeler, yükseltilmiş kartlar (2026-11-10; D-109'un ölçülerini geçersiz kılar)
Bağlam: D-109'un 12 px / 23,5 yoğunluğu ve D-133 cilası sahibine "değişmemiş gibi" geldi; daha cesur iki yön seçti
(kartlar / yüzeyler; tipografi / yoğunluk). Ayrı bir kopyada (`build\lab\preview\`) iki varyant render edildi; sahibi önce /
sonra karelerinde A'yı onayladı ("2-4 iyi"). Bu kayıt D-133'ün "karar yok" maddesini kapatır.
Karar: renkler D-109'da kalır (sonra D-137); yazı, yoğunluk, köşe, boşluk, kart yüksekliği WinLove'un kendisi
(`tokens.json` 4.0.0): body 12/16 → 13/18; section 12/16/400 → 14/19/600; display / title / panel 15 → 17, 14 → 15, 12 →
14; control 24 → 28; tableRow / row / tableHead 23,5 / 24 → 28; listRow 28 → 32; tab 26 → 30; navItem 28 → 32; titleBar 34
→ 36; statusBar 22 → 26; r1 / r2 / r3 2 / 4 / 6 → 3 / 6 / 10; button, badge 4 → 6; menu 4 → 8; contentPad 16 → 20; panelPad
8 → 12; checkbox 13 → 14; toggle 38×18/12 → 40×20/14; yeni `elevation.card`. Şerit 50'de ve aynı tasarımda kalır.
Kodda: `Canvas::raisedCard` (önbellekli dokuz parçalı gölge + 1 fiziksel piksel `line.subtle` çerçeve; koyu temada kartı
çerçeve ayırır); sekme şeritleri taşınca kayar (`ui/widgets/StripScroll`); form / liste / InfoBar / bildirim / palet
ölçüleri token'lardan; tek bölüm başlığı `FormView::paintSectionTitle`; tablo sütunları ad sütununa yer bırakır
(`TableColumn::minWidth`, `setFlexMin`), kesilen hücre / etiket ipucunda tam. CLAUDE.md kural 3–4 buna göre güncellendi.
Ek — sahibinin aynı gece bu işin ajanına doğrudan yazdığı beş istek (2026-11-11; aynı `dist`'e girdi):
1. İmaj açıkken Kaynak seç'e geri dönüş: Görüntü kartında "Kaynak seç · Sürümler" sekmeleri (`Shell.cpp`:
   `workspaceTabs`, `attachSubNav`); D-138 üçüncüsünü (Özet) ekledi.
2. Başlık çubuğu menülerinde ikon (`TitleBar`).
3. Bağlı imajlar listesinin gecikmesi: liste önce wimgapi'den (`core::wimMountedImages`, `inspectMountsFast`;
   `ImageController::scanSystemMounts`), DISM okuması arkadan (D-064'ün listesi). **Yeni motor yolu gerçek bağlı imajla
   denenmedi (kural 6): lab bekliyor.**
4. Windows indir'de "Seç…" gecikmesi: Mağaza uygulama listesi ayrı runner'da (`WindowsDownloadController::m_appLists`).
   Süre ölçülmedi.
5. Katılımsız'da fare tekerleği: sayfa bütün olarak kayıyor (`UnattendedPage`, `ScrollableBody`). Canlı pencerede
   denenmedi.
Sonuç:
- Doğrulanan: bütün sayfa, demo durumu, diyalog ve menü koyu + açık, 1207x700 @1,75 ve 1100x700 @1,0'da render edilip
  bakıldı (384 kare); Debug + Release testleri geçiyor (yeni `tests/ui/RestyleTests.cpp`).
- Doğrulanmayan: canlı pencere, gerçek yoğunluk ayarı (Rahat / Geniş yalnız ölçek yaklaşımıyla), aygıt kaybında gölge
  önbelleği, WARP maliyeti; beş isteğin hiçbiri pencerede denenmedi.
- Bilinen: Görüntü yan kartı 700 yükseklikte son 2–3 bilgiyi göstermiyor; Katılımsız'da bir ipucu önizlemenin üstüne
  düşüyor.

## D-135 — Arayüz hareketi (DirectComposition, Windows Animation Manager, DPI): İPTAL — uygulandı, ölçüldü, sahibinin isteğiyle tamamen kaldırıldı (2026-11-10 → 11)
Bağlam: Sahibi (2026-11-10): "DirectComposition, Windows Animation Manager, DPI Awareness API da kullan akıcı güzel
animasyonları olsun arayüzün." D-011'den beri tek HWND flip swap chain'e her karede bütün pencere çiziliyor, bütün
hareketler arayüz iş parçacığında. Plan ve taslak kayıt `docs/MOTION-PLAN.md` (§7 aşamalar, §9.1 taslak): aşama 0
ölçüm düzeneği, 1 compositor kök (aynı pikseller, kapatma anahtarı), 1b DPI cilası, 2–4 yüzen katmanlar, sayfa /
diyalog / menü / bildirim geçişleri, Windows Animation Manager, kalan hareket dili.
Karar ve olan:
- Aşama 0 + 1 + 1b bir alt ajanda uygulandı ve ölçüldü (`build\lab\motion\`: kare günlükleri, `render_diff` 174 / 174
  aynı): sayfa geçişi kare aralığı 8,3 → 4,2 ms; ekran kartı belleği +11,5 MB; açılış +10 ms; durgun pikseller aynı.
  Aşama 2–4 hiç başlamadı.
- 2026-11-11 gece sahibi: "bunu iptal et, animasyon istemiyorum", ardından "tamamen sil" → **iptal**. Alt ajan kendi
  değişikliklerini elle geri aldı. `MOTION-PLAN.md`'nin başındaki nota göre aşama 1b'den yalnız küçük, bağımsız DPI
  düzeltmeleri kaldı: pencere simgesinin DPI'ya göre yüklenmesi, `--size`'ın istemci alanı olması, çift tık alanının
  DPI'ya göre olması.
- D-133'ün küçük widget hareketleri (kayan sekme çizgisi, kendini çizen tik, üzerine gelme solması) yerinde: sahibi onlar
  için bir şey demedi.
Sonuç:
- Tutulmamasının nedeni sahibinin kararı, sayılar değil: ölçüm geçişte kare aralığını yarıya indirmişti; bedeli ekran
  kartı belleğinde +11,5 MB ve açılışta +10 ms idi.
- D-011 geçerli kalır. `MOTION-PLAN.md` rafa kalkmış plan olarak duruyor; §9.2'nin "değişecek satırları" uygulanmadı
  (CLAUDE.md'nin "Arayüz" satırı, ARCHITECTURE'ın render akışı, ROADMAP aynı).
- Doğrulanan (geri alma): 2026-11-11'de `src/` altında DirectComposition ya da kare günlüğü izi yok (`dcomp`,
  `IDComposition`, `Compositor`, `FrameLog`, `composition=` aramaları boş). Ölçüm sayıları alt ajanın raporundan;
  PERF-BASELINE'a tablo olarak girmedi.
→ D-142 (2026-11-11): sahibinin ayrıca istediği tek hareket, ilerleme halkası; DirectComposition / hareket altyapısı
olmadan, var olan `tickIntervalMs` ile (dönerken 25 ms, hareketi azaltta duran yay). İptal geçerli.

## D-134 — Kurulum ortamına (boot.wim) sürücü: önce elenir, DISM'in listesiyle doğrulanır, depolama imaja da (2026-11-10; D-056'nın boot.wim sürücülerini genişletir)
Bağlam: Sürücüler › "Kurulum ortamı (boot.wim)" sekmesi vardı (D-056), sürücü yolu hiç denenmemişti. Kodda: sürücüler
ISO'daki "atlamaları boot.wim'e de yaz" kutusuna bağlıydı (kutu kapanınca sessizce düşerdi); reddedilen sürücü yalnız
log'a giderdi; ARM64 INF seçilebilirdi; sekmede sınıf tıklaması yeniden kurulan satırları gezerdi (çökme riski). Lab:
DISM yalnız NTarm64 modelli INF'i ve kataloğu uyuşmayan ağ sürücüsünü x64 boot.wim'e "imzasız" diye sessizce ekler;
imzasız depolama sürücüsünü 0x80070032 ile reddeder; aynı paketi ikinci kez eklemez ama başarı döner. Sahibinin seçtiği
üç özellikten biri. Sayfa ayrıntıları `docs/pages/09-drivers.md`.
Karar:
- Motor (`core/image/BootImage`): `checkBootDriver` (dosya, mimari, katalog) DISM'den önce; her paketten önce / sonra
  `DismGetDrivers` farkı → eklendi / zaten var / imzasız (`DismRemoveDriver` ile geri). Rapor `driversAdded / Present /
  Refused`. Yalnız index 2 (medya onu açar).
- `core/image/MediaDrivers`: medyadaki üçüncü parti sürücüler bağlamadan okunur; `DriverInf::setupRole`: depolama / ağ /
  kablosuz / sanal.
- ISO / USB: sürücüler atlama kutusundan bağımsız; özette "Ortam sürücüleri"; yalnız boot.wim'e giden depolama
  sürücüsüne uyarı; reddedilen sürücü bitiş bandında adı ve nedeniyle, ISO yine üretilir.
- Sürücüler › Kurulum ortamı: "boot.wim'de olanlar" grubu; rol etiketi; başka mimari / zaten var soluk ve ipuçlu; "Bu
  bilgisayardan al" depolama + kablolu ağı işaretler; imaj bağlıyken depolama sürücüsü imaj kuyruğuna da girer.
- CLI: `wlcli iso --boot-driver / --bypass / --legacy-setup`, `media-drivers`, `driver-scan`.
- Kapsam dışı: WinRE (install.wim içindeki Winre.wim) sürücüleri; ForceUnsigned.
Sonuç:
- Doğrulanan: `tools\lab_boot_drivers.ps1` ALL PASSED (pvscsi, Intel VMD, Realtek GbE index 2'de; 4 yanlış INF elendi;
  verify 13.894 akış). `-Vm`: sürücüsüz boot.wim ile kurulumun disk listesi boş; aynı kopyaya `wlcli iso --boot-driver`
  ile pvscsi → kurulum ~10 dk, masaüstü. Kanıtlar `build\lab\bootdrv\out\`, görüntüler `build\lab\bootdrv\shots\`.
- Doğrulanmayan: gerçek VMD / RAID makinesi; ağ sürücüsünün WinPE'de ağı açması; USB yolu (yalnız birim testi); arayüz
  pencerede tıklanmadı.

## D-133 — Arayüz cilası: hareket, kabuk, sayfa düzeltmeleri, Kaynak sayfası; D-109'dan sapmalar (2026-11-10)
Bağlam: Kullanıcı (2026-11-10): "tüm arayüzü oldukça güzel kaliteli bir tasarıma kavuştur". Şerit için üç taslağa baktı
ve şeridin olduğu gibi kalmasını seçti. Kullanıcı bilgisayar başında değildi, onayı devretti ("sen hallet benim yerime
herşeyi") ve işin alt ajanlarla yapılmasını istedi. Dört aşama; her aşama derlendi ve render edildi.
Karar:
1. **Widget çatısı (`ui`):** `anim/ItemMotion` (`ItemHover`: öğe hover dolgusu söner-yanar; `SelectionMove`: seçim
   göstergesi yeni öğeye kayar). Sekme alt çizgisi yeni seçime kayar; işaret kutusunun tiki kendini çizer (koddan
   konan tik doğrudan görünür); tablo satırının hover'ı söner-yanar, odak halkası satırın içinde; form ipucu kartın
   dolgusunda biter, sığmazsa `…`. Anında harekette (Windows animasyonları kapalı, `--render`) son durum çizilir.
2. **Kabuk:** gezintide sayfa başına kuyruk sayısı rozeti (vurgu tonunda; yeni token `opacity.accentTint` = 0,2;
   üç haneden sonra "999+"). Durum çubuğu tıklanabilir bölümler: bağlı imaj (→ Görüntü), kuyruktaki değişiklikler ve
   tahmini kazanç (→ Uygula); söyleyecek bir şeyi olmayan bölüm çizilmez, boş alan hep görünür. Başlık açık imajı
   yazar ("Windows 11 Pro · install.wim"; imaj yoksa uygulamanın adı). Eylemi olmayan sayfada şerit bandı çizilmez
   (kartlar yine aynı yükseklikte başlar). Öğe başına tooltip'ler düzeldi (`Widget::tooltipAnchor`,
   `Host::tooltipChanged`; AUDIT'in "kapsam dışı gözlem" notundaki tablo satırı tooltip'leri dahil; canlı denenmedi).
   Windows indir'in "Yenile"si şeride taşındı.
3. **Sayfalar:** sütun başlıkları sütunlarıyla hizalı; ağaç kategori satırları "n/m" ve toplam boyutu yazar; Ayarlar'ın
   üçlü değişiklik işareti çubuk + metne indi; boş durumlar tutarlı (bağlı imaj yoksa boş durum gövdenin tamamını
   alır); kendini çizen tik 8 sayfada ve 4 diyalogda; Hakkında, Başlat menüsü ve Uygula şerit eylemleri aldı. Diller,
   Kayıt Defteri ve Uygulamalar'da kartı taşan sütunlar, ISO ve Presetler'de kesilen etiketler, büyük harfli başlıklar
   giderildi; bölüm başlığı stili tek (`paintFormSection`). Windows indir kaydırılabilir oldu: en küçük pencere
   yüksekliğinde "İndir ve ISO oluştur" düğmesine ulaşılamıyordu.
4. **Kaynak sayfası** (`docs/pages/01-source.md`): bırakma alanı tek kompakt satır (`pages/source/DropRow`); "Son
   kullanılanlar" tek sütunlu liste (iki satırlık satırlar), görünür kaldır düğmesi (×), klavye (→ ile ×'e, Enter /
   Boşluk / Delete); bölüm başlıkları diğer sayfalardaki gibi.
D-109'dan sapmalar (kullanıcının isteğiyle; referans araçta yok): durum çubuğu bölümleri, başlıkta imajın adı, vurgu
renkli gezinti rozetleri, eylemsiz sayfada bant olmaması, Kaynak sayfasının düzeni (D-091'in kartlarından da ayrılır).
Şerit referans araçtaki gibi kaldı (kullanıcının seçimi).
→ D-147 (2026-11-11): başlıkta imajın adı kaldırıldı; başlık her zaman "WinLove", markanın yazı tipiyle (IBM Plex Sans
SemiBold, "Love" vurgu tonunda).
Sonuç:
- Doğrulanan: Debug + Release derleniyor, birim testleri geçiyor (yeni: `tests/ui/UiAuditTests.cpp`'deki "polish"
  testleri, `tests/app/ShellPolishTests.cpp`, `PagesPolishTests.cpp`, `SourcePolishTests.cpp`); her aşama durgun
  render karesiyle görüldü; `dist\WinLove.exe` (2026-11-10 22:06) bunları içeriyor.
- Doğrulanmayan: hareketler canlı bir pencerede hiç izlenmedi (render anında hareketle durgun kare çizer); uygulama
  içinde tıklama turu yapılmadı (durum çubuğu bölümleri, rozetler, Kaynak listesinde kaldırma / klavye); kare hızı
  ölçülmedi; tasarımı kullanıcı onaylamadı.
- Açık, **karar yok:** kullanıcı sonucu görünce "değişmemiş gibi" dedi ve daha cesur iki yön seçti: kartlar / yüzeyler
  ve tipografi / yoğunluk. Ağacın bir kopyasında önizleme hazırlanıyor; seçilen yön D-109'u daha da değiştirecekse yeni
  bir kayıt olur.
  → D-136 ile karara bağlandı (A varyantı; renkler sonra D-137). Buradaki widget hareketleri D-135'in iptalinden
  etkilenmedi.

## D-132 — Denetimin ikinci turu: operatörün ve sonraki oturumun bilmesi gereken davranış değişiklikleri (2026-11-10)
Bağlam: Kullanıcı: "yapmadıkların varsa onları da yap … sen hallet". Denetimin (`docs/AUDIT-2026-11-10.md`) kalan
bulguları sekiz parçada düzeltildi, parçalar arası yapıştırma yapıldı, sonra bağımsız bir ikinci okuma (altı bulgu) ve
düzeltmeleri, en son yönetici laboratuvarı (`build\lab\r2\REPORT.md`). Bulgu bulgu durum AUDIT'in başındaki tabloda;
DISM oturumu D-131'de, çocuk süreçler D-130'da. Bu kayıt, D-129'un "Loglar sayfası 20.000 sınırında 18.000'e iner"
maddesini geçersiz kılar.
Karar (görünen ya da betiği etkileyen değişiklikler):
- **Günlükler:** açılışta `%LOCALAPPDATA%\WinLove\logs`'ta en yeni 50 oturum logu kalır (`base/Log.cpp`
  `kSessionLogsKept`): yalnız klasörün doğrudan içindeki, adı tam `WinLove-YYYYMMDD-HHMMSS.log` olan normal dosyalar;
  wlcli ve `--render` de oturum logu açtığı için sayılır; açık (başka bir WinLove'un yazdığı) dosya silinemez, kalır;
  listeleme yarıda hata verirse hiçbir şey silinmez. `dism.log` 32 MiB'ı aşınca `DismInitialize`'dan önce
  `dism.old.log` olur (öncekinin üstüne). Loglar sayfası tam 20.000 satırda keser (yalnız taşan satırlar düşer, pay
  yok); Uygula konsolu 20.000 satırla sınırlı.
- **Arayüz:** Shift+F10 bağlam menüsünü açar (`WM_SYSKEYDOWN`'da iletiliyor; düz F10 Windows'ta kalır).
  `SearchBox::setMaxLength` var, varsayılan sınırsız (ilk hâlindeki 32.767 kayıt değeri verisini sessizce kesiyordu).
  Ayarlar'da dil / yazı tipi, bağlı imajın listelerinden biri okunurken ve sürücü dışa / uygulama içe aktarımı
  sürerken kilitli (mevcut kilit ipucuyla); bekleyen indirmeler arayüz yeniden kurulunca kaybolmaz. Diller'de indirme
  ilerlemesi açık listeyi kapatmaz. Uygula bitince bileşen, envanter, sürücü ve dil listeleri de yeniden okunur; Başlat
  uygulama listesi yalnız koşu Done'a geçerken bırakılır. Önceki WinLove kapanırken (mutex var, pencere yok) yeni açılış
  10 sn'ye kadar bekler (yeni ileti yok). Çökme dökümü `terminate`, purecall, geçersiz parametre ve `SIGABRT`'de de
  yazılır. Hosts içe aktarma 64 MB'tan büyük dosyanın ilk 64 MB'ını okur (sonuç eskisiyle aynı).
- **Diller'in %100 CPU döngüsü** (eskiden beri açıklanamıyordu; `--demo-languages` bu yüzden ölçülemiyordu): başarısız
  okuma sayfayı yeniliyor, sayfanın yenilemesi okumayı yeniden başlatıyordu. Artık başarısız okuma `force` ya da başka
  bir bağlama gelene dek yeniden başlamaz ve yalnız durumun beklediği yanıt yazılır (`LanguageController`; aynı koruma
  `ServiceController`'da). Test: `tests/app/PagesPolishTests.cpp`.
- **Ağ:** süreç ömürlü tek WinHTTP oturumu (zaman aşımı / TLS bir kez; çerez kabı istekler arasında ortak, önce her
  istek temiz oturumdu). İptal beklemedeki WinHTTP çağrısını keser (istek tutamacı iptal olayında kapanır); UUP
  indirmesinde iptal bütün işçilere hemen gider. Beklenen boy verilmemişse yanıtın `Content-Length`'i beklenir: gövde
  kısa biterse `.part` kalır ve "download ended at X of Y" hatası döner (önce tam sayılıyordu; karmasız indirmeler —
  katalog güncellemeleri, winget dizini — etkilenir), yeniden başlatınca kaldığı yerden sürer; devam isteğine 416 gelirse
  `.part` silinir. UUP'de var olan dosyanın karması okunamazsa ~1 sn sonra bir kez daha, yine olmazsa silinip yeniden
  indirilir. Windows indir'in liste istekleri yenisi gelince öncekini iptal eder; dönüştürme başarılıysa UUP klasörü
  motor işinin sonunda silinir.
- **Ayrıştırıcı sınırları** (bu dosyalar artık reddedilir): PE'de `SizeOfHeaders` dosyadan büyük ya da `FileAlignment`
  > 64 KiB → yamalanamaz (okunur); kaynak verisi toplamı dosyanın 2 katını ya da yaprak sayısı 65.536'yı aşarsa
  ayrıştırılmaz. WIM'de lookup tablosu 256 MiB'ı (≈5,3 milyon akış) aşarsa "okunamıyor"; `wimFileData` 4 GiB üstü tek
  dosyayı okumaz; doğrulama işçisindeki istisna / bellek yetmezliği "hasarlı" olarak raporlanır. `.psf`'i aşan girdi
  arabellek ayrılmadan `ParseError`.
- **İmaj ve kurulum medyası:** kurulum medyası değiştirilirken `sources\` içinde silinemeyen her eski girdi loglanır ve
  herhangi biri işi durdurur (önce yalnız sonuncusu); kök listelenemezse hiçbir şey silinmeden durulur. Var ama
  okunamayan `hosts` / `SetupComplete.cmd` artık hata (üzerine yazılmaz). Ek sürümler (D-055) `mountSafely` ile
  bağlanır; aynı sürüm zaten bağlıysa hata döner, mount olduğu gibi kalır. Kilitli dosyada zorla silme: ayrıcalıklar
  süreç başına bir kez, çocuktan gelen erişim reddinde üst klasör yeniden sahiplenilmez. Bağlama klasörü junction /
  SUBST / 8.3 adıyla verilmişse kurulu kopyaların bağlantı adları gerçek yolla eşlenir; SUBST altındaki kovanlar
  görülür, kovan boşaltma erişim reddinde 3 × 200 ms yeniden denenir.
- **wlcli** (lab betikleri bunu bilmeli): `apply --also`'da `optimizeWim` hatası çıkış 3 (önce 0), geçerli dizin
  içermeyen `--also=abc` iş başlamadan çıkış 2 — `tools/lab_features.ps1` (`--also=2`) `optimizeWim` hatasında artık
  kırmızı; `catalog --download` hiçbir teklif seçmediyse hata satırı + çıkış 1; `icon-pack --source` hiçbir dosya
  yamalamadıysa hata satırı + çıkış 1 (hata varsa yine 3); `reg <mount>`'ta `[unsupported]` hâlâ çıkış 0. Konsol
  kapatma / oturum kapatma / kapanış da iptal eder, işleyici komut dönene kadar en çok 20 sn bekler. `iso`, `append`,
  `uup apps` geçici klasörlerini hata ve iptalde de siler.
Sonuç:
- Doğrulanan: Debug + Release, birim testleri (yeni dosyalar AUDIT'in durum tablosunun başında); yönetici
  laboratuvarında aynı 30 adım eski ve yeni `wlcli` ile aynı sonucu verdi (D-131); `tools/render_diff.py` ile 2. turdan
  önceki ve sonraki ikili 174 durgun karede karşılaştırıldı, bir gerileme bulundu ve düzeltildi (Dil ekle diyaloğunda
  boyutlar).
- Doğrulanmayan: ağ yolları (WinHTTP oturumu, iptal, `Content-Length`), kilitli dosya, junction / SUBST bağlama yolu,
  kurulum medyası değiştirme, `wlcli` çıkış kodları, konsol kapatma; hiçbiri çalıştırılarak denenmedi.

## D-131 — DISM oturumu tembel açılır: kapanan oturum bir sonraki DISM çağrısına kadar kapalı kalır (2026-11-10)
Bağlam: Denetim IMG-01 (`docs/AUDIT-2026-11-10.md`). `removeComponent` içinde DISM oturumu beş ayrı yerde askıya
alınıp hemen yeniden açılıyordu (açılış dosyası denetimi, `editHives`, `removeStoreCopies`, derin sürücü kaldırma, AppX
sonrası), Applier adım sonunda bir kez daha; aralarında oturumu kullanan çağrı yoktu. D-128'in lab ölçümünde yeniden
açma ~1,4 sn. Bu kayıt birinci turun sonunda taslak olarak yazıldı ("uygulanmadı": oturumun yaşam döngüsüne dokunduğu ve yalnız
laboratuvarda doğrulanabileceği için); aynı gün uygulandı ve laboratuvarda ölçüldü.
Karar (uygulandı):
- `DismSession::suspend()` oturumu kapatır ve kapalı bırakır. Oturumu kullanan her üye önce `ensureOpen()` çağırır
  (kapalıysa `reload()`); `open()` o an açık olup olmadığını söyler. Kapalı oturum kovan tutmaz ve maliyeti yoktur:
  art arda gelen kovan / dosya adımları arasında DISM hiç açılmaz.
- `removeComponent`'teki beş pencerenin ardındaki `reload()`'lar kaldırıldı; `suspend()`'ler yerinde, sıraları aynı.
  `runDismExe` ve yerel AppX kaldırma eskisi gibi hemen yeniden açar.
- Applier adım sonunda yalnız DISM yeni oturum istediyse (`reloadRequired()` ve oturum açıkken) ya da adım oturumu
  açamadıysa bir kez daha yeniden açar.
- **Oturum açılamazsa koşu durur** (bağımsız incelemenin 1. bulgusu, aynı gün): `reload()` başarısız olunca hata
  `lost()`'ta kalır (başarılı bir `reload()` temizler). Applier o adımı her adım gibi raporlar, `report.sessionLost`'u
  doldurur ve döner (`completed = false`); böylece ne ana sürüm ne `applyToEdition`'daki ek sürümler kaydedilir.
  `lost()` olmadan koşu "N adım başarısız" diye sürüyor, ek sürüm yarım hâliyle commit edilebiliyordu.
- D-127'nin yolu değişmedi: `0x800703EE`'de yer tutucular diske indirilir, DISM isterse oturum yeniden açılır, paket
  bir kez daha denenir.
Sonuç:
- Lab (`build\lab\r2\REPORT.md`, 2026-11-10): 26200.8037 Pro'nun dışa aktarılmış kopyası, her koşu taze kopyada; aynı
  30 adım (18 depo kopyası, CBS paketli ve appx'li tarifler, kayıt / servis adımları karışık); eski (commit `1259c1e`)
  ve yeni `wlcli`. İkisi de 30 / 30, ScanHealth temiz, Uygula sonrası yüklü kovan yok; kalan dosya (59.316), servis
  anahtarı (682), CBS (3.403) ve appx (43) kayıtları birebir aynı; 18 adımda "store copies removed" sayımları aynı.
  **Depo kopyası adımı medyan 1,360 → 0,508 sn** (18 adımın toplamı 31,0 → 15,9 sn), Uygula 152,4 → 126,0 sn. Yeni
  ikilide adım süresi adımın kendi işi kadar (`perf store copies` satırı; çoğu açılış analizi, ~0,4 sn): DISM'in payı
  kalmadı.
- Taslağın kabul ölçütü: "< 0,5 sn" kıl payı tutmadı (0,508 sn), kalan süre DISM değil açılış analizi (IMG-02,
  atlandı). "CheckHealth temiz": ScanHealth temiz. "D-127 yolu yeniden denenmiş": hayır, `0x800703EE` oluşmadı.
- Doğrulanmayan: oturumun yeniden açılamaması (`lost()`; `DismSession` birim testte kurulamıyor: dismapi + yönetici +
  bağlı imaj ister), SSU'nun istediği yeniden yükleme, D-127 yolu, arayüzden Uygula. Her ikili bir kez koştu.
- Bilinen: oturum açılamazsa, durulan adımın DISM'den önceki işleri (kovan düzenleme, depo kopyaları, derin sürücü
  kaldırma) yapılmış olur; eskiden ilk yeniden açma düşünce bunlar hiç çalışmazdı. Uygula sayfası ve `wlcli`
  `sessionLost`'u ayrıca göstermiyor: "iptal" gibi okunur (AUDIT-2026-10 A20'nin görünen yarısı).
  → Gösterilmemesi kapandı: Uygula'da D-138 (durdurulan çalışmanın kartı), `wlcli apply`'da D-140'ın küçük işleri
  (gerçek bir oturum kaybıyla hâlâ denenmedi).

## D-130 — Çocuk süreçler kill-on-close job'da; servis yapan araçlar dışında; iptal yalnız zararsız iki araçta (2026-11-10)
Bağlam: Denetim ENG-02 · OPS-05 · ENG-07 · CLI-06. `runProcess` iptal belirteci almıyor, süreci sonuna dek bekliyor ve
job object'e koymuyordu: Durdur bu adımlarda etkisizdi, takılan çocuk motoru ve kapanışı kilitliyordu, WinLove ölürse
çocuk bağlı imajın kovanlarını tutarak çalışmaya devam edebiliyordu; eşzamanlı iki çağrı birbirinin borusunu
kalıtıyordu. Birinci turun sonunda taslak olarak yazıldı (Durdur'un görünen davranışı araç araç karar istediği için); aynı gün
uygulandı, kapsamı bağımsız incelemeden sonra daraltıldı.
Karar (uygulandı, `core/system/Process.{h,cpp}`):
- `ProcessLife::EndsWithUs` (varsayılan): araç askıda başlar, `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE` bayraklı bir job'a
  atanır, sonra sürdürülür; araç çalışırken WinLove ölürse araç ve başlattıkları ölür. Araç kendiliğinden bitince sınır
  kaldırılır: bilerek geride bıraktığı süreçler eskisi gibi yaşar. Job'a atanamazsa (WinLove iç içe job kabul etmeyen
  bir job'da çalışıyorsa) eskisi gibi job'sız.
- `ProcessLife::Outlives` (job yok): bağlı imajda dism.exe (`runDismExe`: `/Cleanup-Image`, `/Add-Package`,
  `/Set-Edition` …) ve USB yazımında diskpart / bootsect (`UsbMedia` `runLogged`). Bunları işin ortasında öldürmek
  bekleyen işlemli imaj ya da yarım hazırlanmış disk bırakır; bitmelerine izin vermek daha az zararlı. İlk uygulamada
  bunlar da job'daydı; bağımsız inceleme "WinLove çökerse servis yapan dism.exe ortada ölür" dedi, aynı gün ayrıldı.
- Job'da kalanlar: expand.exe (UUP, medya yenileme), tar (simge paketi), powercfg `/export`, pnputil `/export-driver`,
  winget, `dism /Online /Export-DefaultAppAssociations` (imaja yazmaz).
- İptal (`const CancelToken*`, isteğe bağlı): verilirse boru yoklanır (25 ms), iptalde bütün job sonlandırılır, sonuç
  `Cancelled`; başlamadan iptal edilmişse araç hiç başlamaz. **Belirteç yalnız iki yerde verilir:** `winget download`
  (Programlar'ın çevrimdışı indirmesi; yarım klasörü sonraki deneme boşaltır) ve `pnputil /export-driver` (bu PC'nin
  sürücülerini dışa aktarma). `Outlives` ile verilen belirteç programlama hatasıdır: loglanıp yok sayılır.
- Kalıtım: boru uçları kalıtılamaz açılır, yalnız yazma ucu `PROC_THREAD_ATTRIBUTE_HANDLE_LIST` ile çocuğa verilir
  (ENG-07); liste kurulamazsa eski davranış.
- `wlcli`: konsol kapatma / oturum kapatma / kapanış da iptal eder, işleyici komut dönene kadar en çok 20 sn bekler
  (CLI-06).
Sonuç:
- Durdur'un görünen davranışı yalnız winget indirmesinde ve sürücü dışa aktarımında değişti (artık hemen keser); dism.exe
  adımları eskisi gibi "adım bitince durur". Belirteçli yolda çıktı en çok 25 ms gecikir.
- Doğrulanan: birim testleri (`tests/core/EngineAuditTests.cpp`: çıktı ve çıkış kodu tam gelir; iptal edilen belirteç
  aracı keser; `Outlives`'a verilen belirteç yok sayılır, araç sonuna kadar çalışır). Lab (`build\lab\r2`): `wlcli
  health check|scan` (dism.exe, job dışında) ve `export-host-associations` (job içinde) eski ve yeni ikilide aynı sonuç;
  her komuttan sonra lab'ın başlattığı dism / DismHost süreci ve yüklü kovan kalmadı.
- Doğrulanmayan: WinLove ölünce job'daki çocuğun ölmesi, arayüzden Durdur (winget indirme, sürücü dışa aktarma), USB
  yazımı.

## D-129 — Kararlılık / hız / bellek turu: ne değişti, neyin bedeli ödendi (2026-11-10)
Bağlam: Kullanıcı kapsamlı bir denetim + optimizasyon turu istedi, planı görüp "Hepsini uygula" dedi. Bulgular
`docs/AUDIT-2026-11-10.md` (132 bulgu, sonunda durum tablosu), ölçümler `docs/PERF-BASELINE.md`.
Kararlar (davranışa dokunanlar):
- **Kuyruk dizinli** (`ChangeSet`): `find` artık (tür, hedef) dizininden. Anahtar kaba (ASCII küçük harf, gerisi tek
  yer tutucu), eşleşmeye yine `text::iequals` karar verir: kurallar aynı.
- **Geri alma geçmişi boyutla da sınırlı** (64 MB; en yeni durum hep kalır). Sıradan kuyrukta 200 adım yine duruyor;
  karşılama betiği kuyruktayken (~1 MB / durum) en çok ~64 adım geri alınabilir. Bedel bu.
- **Toplu işaretleme tek adım** (`AppState::QueueBatch`): kategori / sınıf / klasör işaretlemesi artık tek geri alma
  adımı ve tek bildirim. `AppState.h` zaten "bir adım = bir eylem" diyordu; eski davranış (n adım) hataydı.
- **`queue.json` biçimi**: içerik aynı, girintisi farklı (değişiklik kümesinin metni olduğu gibi gömülüyor).
- **`TaskRunner` yıkılırken çalışan işi de iptal eder.** İmaj yazan işler `confirmClose` ile zaten korunuyor;
  etkilenenler okuma / karma / dışa aktarma işleri.
- **Modalın sahibi** (`Host::pushModal(..., owner)`): sayfası yok olan menü / diyalog kendiliğinden kapanır. Modal
  açıkken pencereye bırakılan dosya yok sayılır.
- **Sistem dosya diyaloğu açıkken** motorun arayüze gönderdikleri bekler (`ui::PostHold`), diyalog kapanınca çalışır:
  o sürede ilerleme göstergeleri güncellenmez (pencere zaten devre dışı).
- **Açılış sürücüleri okunamazsa** bileşen kaldırma servis anahtarlarını silmez (dosyalar için kural zaten buydu).
- **`wlcli apply --pause`**: yanıt okunamazsa (stdin kapalı, Ctrl+C) kaydetmez, bağlı bırakır. Önce kaydediyordu.
  stdin'siz çalışan lab betikleri `--pause` ile artık bağlı imaj bırakır: betik `--pause` vermemeli.
- **`wlcli`**: yakalanmayan istisna artık çıkış kodu 2 + hata satırı (önce `abort`, kod 3).
- **Log saati** `current_zone()` yerine Win32'den; metin aynı (testle sabit).
- **Loglar sayfası** 20.000 sınırında 18.000'e iner (her yoklamada baştan kurmamak için).
- **Uygula bitince** kapsama bağlı analiz önbellekleri bırakılır; ikinci bir Uygula bunları zaten yeniden okuyordu.
Tasarım, metinler ve sayfa akışları değişmedi; yeni bağımlılık yok; iş parçacığı modeli aynı.
Doğrulanan: 436 birim testi geçiyor (14'ü bu turda eklendi; 5 mikro ölçüm ayrıca, birim koşusunda atlanır), Debug + Release + dist; ölçümler PERF-BASELINE'da.
Doğrulanmayan: yönetici gerektiren her yol (WinRE iptali, servis anahtarı koruması, önbellek bırakma, Uygula sonu),
uygulama içi tıklama gerektirenler (Mağaza diyaloğu, dosya diyaloğu sırasında sayfa değişimi, toplu işaretleme),
iş sırasındaki kare hızı.

## D-128 — Derin kaldırma adımları imajdan okuduklarını paylaşır: adım başına ~12 sn → ~1,7 sn (2026-11-09)
Bağlam: Kullanıcı: "bileşen silme kısımları çok yavaş, daha da hızlandırmanın yolu yok mu?" Kullanıcının ön ayarında
183 derin kaldırma adımı (D-113 depo kopyaları) var. Lab ölçümü (26200.9818 kopyası, adım başına): COMPONENTS
dizinini offreg ile baştan okumak ~7 sn, açılış analizi (D-116/117) ~1,8 sn, içe aktarım analizi (D-118) ~1,4 sn,
DISM oturumunu yeniden açmak ~1,4 sn. Bu dördü her adımda baştan yapılıyordu, oysa iki adım arasında imajda
değişen yalnız silinen dosyalardı.
Karar: `ImageScanScope` (`core/image/BootDrivers.h`): Uygula koşusu boyunca açık bir kapsam. Kapsam açıkken
şunlar adımlar arasında saklanır:
- bileşen dizini (anahtar, ad, 32 bit), COMPONENTS dosyası değişmediği sürece;
- açılış sürücüleri ve korunan açılış dosyaları, SYSTEM dosyası değişmediği sürece (hizmet silen adımdan sonra
  yeniden okunur);
- API set şeması ve her modülün çözülmüş içe aktarımları, dosyalar değişmediği sürece.
"Değişmedi" demek dosyanın boyutu ve son yazma zamanının aynı kalmasıdır. DISM kovanları bırakırken (her adımdan
önce) yazar, paket kaldırdıysa bu değişiklik görülür. Kapsamın dışında (tekil çağrılar, testler) her şey eskisi
gibi baştan okunur.
Ölçüm (`wlcli apply`, 10 derin adım tek süreçte): ilk adım ~38 sn (tek seferlik okuma), sonraki her adım ~1,7 sn
(depo kopyaları 0,1–0,3 sn + DISM'in yeniden açılması ~1,4 sn); bileşen dizini bir kez okundu (36.636 bileşen),
hizmet silen adımdan sonra açılış analizi doğru şekilde yeniden yapıldı.
Kalan: DISM oturumunun her adımda bırakılıp yeniden açılması (~1,4 sn). Kovanlar gerekmediğinde atlanabilir, ama
saklanan bilgi eskiyse DISM'in tuttuğu kovanı okumak gerekir; şimdilik dokunulmadı.

## D-127 — Bileşen kaldırmada 0x800703EE: WIM yer tutucusu paket dosyaları diske indirilip bir kez daha denenir (2026-11-09)
Bağlam: Kullanıcının raporunda (Uygula, 26200.9818, servis yığını 10.0.26100.9815) altı bileşen kaldırması
`0x800703EE` (ERROR_FILE_INVALID) ile düştü; imaj kaydedilmeden tutuldu. CBS günlüğü: "Symlink found, not copying file".
Reddedilen 20 dosyanın hepsi `Windows\Servicing\Packages` altında, WIM bağlamasının yer tutucusuydu (reparse etiketi
`0x80000008`, IO_REPARSE_TAG_WIM: içerik hâlâ WIM'den okunuyor, diske yazılmamış). Yığın, paketin kataloğunu özel
oturum deposuna kopyalarken yer tutucuyu sembolik bağ sayıp reddediyor.
Karar: `removeComponent` bir paketin kaldırmasında bu kodu görürse, bir kereliğine `Servicing\Packages`'taki yer
tutucuları diske indirir (yedekleme / geri yükleme yetkisiyle, yazma için açıp kapatmak dosyayı yazılmış kılar), DISM
oturumunu yeniden yükler ve aynı paketi bir kez daha dener. İndirilen dosya sayısı günlüğe yazılır. Başka hata
kodlarında hiçbir şey değişmez.
Doğrulanan: indirme yöntemi lab kopyasında çalışıyor (yer tutucu sayısı 0'a iniyor). Doğrulanmayan: hata lab'da taze
bağlamada tekrar üretilemedi (DLNA ve BranchCache sorunsuz kalktı); büyük olasılıkla önceki adımların bıraktığı durumla
ortaya çıkıyor. Kesin kanıt, kullanıcının tutulan imajında Uygula'nın yeniden çalıştırılması.

## D-126 — Kurulum ekranları "Aurora": WinLove'un kendi görünümü, akıcı çizim (2026-11-09)
Bağlam: Kullanıcı kurulum ekranlarının (D-084) yeniden tasarlanmasını istedi; ilk (Windows benzeri) taslağa "Windows'un
arayüz öğelerine benzemeyelim, özgün ve animasyonlu olsun" dedi (2026-10-08 kararını tersine çevirir). Tasarım
tuvalde onaylandı ("çok hoş bunu entegre et"). Ardından: "çok donuyor".
Karar:
- `resources/scripts/oobe.ps1` (WPF) yeni görünümle yazıldı: koyu / açık zemin üstünde vurgu renginden ve onun döndürülmüş
  tonundan oluşan aurora, numaralı adım çizgisi, iri başlık, cam alanlar, iki renkli hap düğme, her sayfaya bir sahne
  (küre, kart yığını, saat, pencere, duvar kağıdı yelpazesi, paket balonları, gezgin, kalkan), hazırlık ekranında
  yüzde halkası. Bütün işlevler (ağ, hesap, ad / saat dilimi, görünüm, duvar kağıdı, paketler, tercihler, gizlilik,
  hatalar, hazırlık adımları) aynen kaldı.
- **Çizim maliyeti:** ölçüldü (`tools/capture_oobe.py --fps`: her sayfanın geçiş süresi, en uzun kare aralığı, kare
  hızı). Aurora sürekli hareket ettiği için WPF ekrandaki bütün gölge / parlama efektlerini her karede yeniden
  çiziyordu: ekran kartıyla bile 4 sayfa 5–14 kare/sn. Çözüm `Optimize-Effects`: efektli her öğe bit eşlem önbelleği
  olan bir sarmalayıcıya alınır (bir kez çizilir, sonra yalnız birleştirilir); onu hareket ettiren dönüşüm
  sarmalayıcıya taşınır. Sonuç: bütün sayfalar 96–160 kare/sn. Aurora 15 kare/sn'de, yarım çözünürlüklü önbellekle
  kayar; ilerleme halkası kare kare çizimi yalnız hazırlık ekranında başlatır.
- **Ekran kartı yoksa** (Tier 0: VM ya da sürücüsü henüz kurulmamış ilk açılış; önizlemede `--software`): aurora
  durur, süs döngüleri bekleme pozunda kalır (`Start-Loop`; yalnız "çalışıyor" diyenler, `-Always`, döner), gölgeler
  kaldırılır. 9–16 → 55–68 kare/sn.
- Duvar kağıdı küçük resimleri sayfaya girerken topluca değil, pencere boştayken tek tek açılır (geçiş 568 → ~300 ms).
- Uygulamadaki karşılama kartı önizlemesi (`WelcomeCard`) aynı görünümle çizilir; renkler planın temasından gelir
  (uygulamanınkinden değil). `Canvas`'a tema dışı renk çizimleri eklendi: renkli metin / ikon / çerçeve, iki renkli
  dolgu, yumuşak ışık.

## D-125 — Güncellemelere karşı koru: kurulu Windows'ta, güncellemenin geri açtığını düzelten görev (2026-11-09)
Bağlam: Kullanıcı "Referans araçtan farkımız" listesinden 5. maddeyi seçti ("nasıl bir koruma ekleyebilirsin"). Bir imajın
seçimleri Windows Update'ten sağ çıkmıyor: toplu güncelleştirme bir hizmeti yeniden açabiliyor, bir ilkeyi
sıfırlayabiliyor, bir görevi yeniden etkinleştirebiliyor, bir uygulamayı yeniden hazırlayabiliyor (provision). D-113'ün
depoyu koruyan kaldırması yüzünden, güncellenen bir bileşenin dosyaları da geri gelebiliyor. Referans aracın işi imajla
bitiyor; kurulumdan sonra izleyen bir parçası yok.
Karar:
1. **Ayar, varsayılan kapalı:** Uygula özetinde "Kurulumdan sonra ☐ Güncellemelere karşı koru" (`AppSettings::
   updateGuard`). Kurulu sistemi güncellemelerden sonra değiştirdiği için kullanıcı bilerek açar.
2. **Ne korunur (`core/ops/UpdateGuard`):** yalnız Uygula'nın başarılı adımlarının istediği. Kuyruğa yeni bir öğe
   girmez: Applier, adımlardan sonra `guard.json`'u başarılı işlemlerden üretir (sonraki yazım öncekini ezer).
   - Hizmetlerin başlangıç türü (önyükleme / sistem sürücüleri hariç).
   - HKLM değerleri. Kullanıcı değerleri, HKCR, RunOnce, `CurrentVersion\Setup`, `SYSTEM\Setup`, OOBE ve Winlogon
     tutulmaz (bir kez çalışan ya da kurulumun / Windows'un kendi değerleri; AutoAdminLogon yeniden yazılmaz).
   - Kaldırılan uygulamalar (tam addan aile adı) ve bileşen tariflerinin uygulamaları.
   - Kapatılan zamanlanmış görevler (sonradan açılanlar çıkar).
   - Kaldırılan bileşenlerin dosyaları (bileşen başına en çok 16 yol): yalnız raporlanır. Çalışan Windows'ta sistem
     dosyası silmek güvenli değil.
3. **İmajda:** `ProgramData\WinLove\Guard\{updateguard.ps1, guard.json}` ve SetupComplete'te
   `updateguard.cmd` → `updateguard.ps1 -Register`. Görev `\WinLove\Update guard` (SYSTEM, en yüksek; açılışta 3 dk
   sonra, WindowsUpdateClient olay 19'dan 2 dk sonra, günde bir; aynı anda tek kopya, 30 dk sınır). İlk kontrol
   kurulumda çalışır.
4. **Öğe başına kural (betiğin başında da yazılı):**
   - İstendiği gibi → hiçbir şey.
   - İlk kontrolde farklı → düzeltilir (kurulumun değişiklikleri).
   - Son kontrolden beri değişti ve arada güncelleme kuruldu → düzeltilir. "Güncelleme" şu demek: System günlüğünde
     WindowsUpdateClient 19 ya da Setup günlüğünde olay 2 (CBS paket kurdu).
   - Son kontrolden beri değişti, arada güncelleme yok → kullanıcının seçimi; bırakılır ve bir daha korunmaz
     (`state.json` → `adopted`). Böylece kullanıcıyla kavga etmez.
   - Yeniden provision edilen uygulama her zaman kaldırılır (bunu yalnız servis yığını yapar). Kaldırma: provision +
     tüm kullanıcılar + `AppxAllUserStore\Deprovisioned\<aile>` (sonraki özellik güncellemesi geri kurmasın).
5. **Mod:** `guard.json` içinde `"mode": "report"` hiçbir şeyi düzeltmez, her şeyi günlüğe yazar. Günlük `guard.log`
   (512 KB'ta `.old`'a döner). `updateguard.ps1 -Remove` görevi ve klasörü kaldırır.
6. **wlcli:** `apply … --update-guard`.
Sonuç:
- `UpdateGuardTests` (2): plan (hizmet, değer türleri dword / qword / sz / binary / delete, dışlanan anahtarlar,
  uygulama aile adı, görev aç-kapa, bileşen yolları), JSON gidiş-dönüş, imaja yazılan dosyalar ve SetupComplete satırı.
- Betik bu bilgisayarda kendi test anahtarıyla (`HKLM\SOFTWARE\WinLoveGuardTest`, sonra silindi) denendi:
  1. ilk kontrol 5/5 değeri düzeltti (delete, binary, 4294967295 dahil);
  2. güncellemesiz değişiklik kullanıcıya bırakıldı ve sonraki değişiklikte de dokunulmadı;
  3. son kontrol tarihi son WU olayının öncesine alınınca değişen değer düzeltildi, kullanıcınınki kaldı;
  4. rapor kipinde gerçek hizmet / görev / uygulama / dosya yalnız raporlandı.
  `-Register` görevi SYSTEM / en yüksek / üç tetikleyiciyle kaydetti; `-Remove` görevi ve klasörü sildi.
- Doğrulanmayan: kurulu bir VM'de gerçek bir toplu güncelleştirmeden sonraki davranış (uzun tur; referans araçta karşılığı
  yok).

## D-124 — Uygula'da etki raporu; ISO'ya güncelleme girmemesi: güncellemeler kaldırmalardan önce, indirme beklenir (2026-11-09)
Bağlam: Kullanıcı "Referans araçtan farkımız" listesinden 3. maddeyi seçti (uygulamadan önce etki raporu). Aynı gün bir
kullanıcının günlüksüz raporu geldi: "ISO'ya güncelleme eklenmiyor."
Karar:
1. **Etki raporu (Uygula özeti, `ImpactCard`):** kapalı her uyumluluk koruması için (D-121 kataloğu) kuyruğun o
   korumanın tutacağı adımları bulunur (`CompatController::impact`: kapalı korumaların hepsiyle tek geçiş; her adımın
   bloğu onu tutan bütün korumaları sayar). Önce
   Windows özellikleri, sonra uygulamalar ("uygulama" etiketi). Her satırda özellik adı, onu bozan adımlar ve
   "Korumayı aç" düğmesi; açılınca o adımlar kuyruktan çıkar, sayfa yeniden kurulur. En çok 6 satır, gerisi
   "+N özellik daha · Uyumluluk". Bozulan bilinen özellik yoksa yeşil tek satır.
2. **Asıl neden: güncelleme, kaldırmalardan sonra kuruluyordu.** Planlayıcının sırası Sürüm → Kaldır → Özellikler →
   Sürücüler → Güncellemeler idi. Lab (madde 4): aynı ön ayarda 247 kaldırma adımından sonra LCU dism.exe'de
   0x80073712 ile düştü (`ERROR_SXS_COMPONENT_STORE_CORRUPT`, DISM 14098). dism.log'da kaldırılan bileşenlerin
   44 depo bileşeni "Component metadata corrupt … CorruptManifest" olarak görünüyor: App-V, UE-V, Atanmış Erişim,
   BranchCache, Çevrimdışı Dosyalar, Sınav Ol, Edge DevTools … Uygula bu adımı atlayıp imajı kaydediyordu; ISO
   güncellemesizdi. Referans araç ise önce entegre ediyor, sonra kaldırıyor: Uygula ızgarasında "Entegre et ›
   Güncellemeler" "Kaldır › Bileşenler"den önce. D-060'ın notu da aynı sınıfı söylüyordu: sürücüleri kaldırılmış
   imaja LCU 0x80070002 verir. Bu yüzden derin kaldırma zaten güncellemelerden sonraydı.
   Karar: yeni sıra Sürüm → Özellikler → Sürücüler → **Güncellemeler → Kaldır** → Uygulamalar → Derin kaldırma →
   Temizlik → Ayarlar → Küçültme (`Phase` sırası). Güncelleme bütün imaja kurulur. Kaldırma, güncellenmiş
   paketleri sürümleriyle birlikte alır (her sürüm ayrı kaldırılıyor, bkz. `cbs removed …`). LCU'nun yeniden
   provision ettiği uygulamalar da kaldırmaya kalmış olur.
   Değişmeyen: kaldırmaları **önceki bir oturumda** yapılmış bir imaja sonradan güncelleme eklemek yine
   düşebilir. Referans araçta da öyle.
3. **İkinci yol: indirme bitmeden Uygula.** Katalogdan indirilen güncellemeler indirme bitince kuyruğa girer. İki yol
   güncellemeyi imajdan dışarıda bırakıyordu:
   - 5 GB'lık LCU inerken Uygula başlatılabiliyordu. Güncelleme kuyrukta olmadığı için imaj onsuz kaydediliyordu.
   - İndirme Uygula sırasında biterse `AppState::queue` kilitli kuyruğu sessizce yok sayıyordu. Bildirim yine
     "indirildi ve kuyruğa eklendi" diyordu.
   Ardından imaj çözülüyor ve ISO güncellemesiz oluşuyordu. Dil ve Mağaza indirmelerinde de aynısı vardı.
   Düzeltme:
   - `ApplyController::waitingForDownload()`: güncelleme / dil / Mağaza işi sürerken `canStart()` false. Uygula
     özetinde "İndirme sürüyor · %N" çubuğu var. Başlıktaki Uygula bunu bildirimle söyler.
   - Kuyruk alamazken (bağlı imaj yok ya da Uygula sürüyor) biten indirmeler Shell'de bekletilir
     (`PendingDownloads`, kaynağa bağlı). Aynı kaynaktan bir sürüm bağlanınca ya da Uygula bitip kuyruk açılınca
     kuyruğa girer. Bildirim ne olacağını doğru söyler.
4. **Uçtan uca denetim:** `build\lab\upd124`. Microsoft medyasına benzer 25H2 imajı (26200.8037, Pro), kullanıcının
   D-120 ön ayarı (374 bileşen, temizlik, WinSxS küçültme) + 2026-09 LCU (KB5129195, checkpoint yanında) + .NET,
   uygulamanın yolu (`wlcli apply --commit --compat --update-guard`). Kayıttan sonra imaj yeniden açılıp derleme
   ve depo sağlığı okundu.
   - Eski sıra: LCU 0x80073712, derleme 26200.8037'de kaldı (koşu, sonuç belli olunca durduruldu).
   - Yeni sıra:
     - LCU ilk adımda 6 dk'da kuruldu.
     - 807 adımın 805'i başarılı. Kalan 2'si bilinen kalıcı yetenekler (0x800F0825, atlanır).
     - Kayıt tamam; toplam 55 dk.
     - Yeniden açılan imaj 26200.9457: WIM üst verisi de, RollupFix 26100.9457 "Installed".
     - DISM CheckHealth: "No component store corruption detected" (kaldırmalar ve küçültmeden sonra).
     - Güncelleme korumasının dosyaları (`guard.json`, `updateguard.cmd`) imajda.
Sonuç: ApplyTests (indirme beklenir, kilitli kuyruk eklemez, faz sırası), ChangeSetTests / ComponentStoreTests /
SystemComponentTests (güncelleme kaldırmalardan önce), CompatTests (etki raporu: Xbox kapalı → Xbox satırı;
"Korumayı aç" → rapor boş, adımlar çıktı, ilgisiz adım kaldı). 422 test. Render: `shots\ui_h_impact_*`,
`ui_h_applywait`, `ui_h_applyguard`.

## D-123 — Arayüz düzeltmeleri, DISM motoru / ESD iş parçacıkları / ön ayar ISO'da, Katılımsız üst menüde (2026-11-09)
Bağlam: Kullanıcının 14 maddelik listesi. Hepsi render ile denetlendi (`scratchpad\ui_render.ps1`, `shots\ui_f_*`).
Karar:
1. **Sol menü kayar:** NavPanel yüksekliği sığmayınca tekerlek ve ince kaydırma çubuğuyla kayar, satırlarını kırpar,
   seçili sayfayı görünür tutar. Önceden büyük yoğunlukta "Uygula" kartın dışına taşıyordu.
2. **Tema seçici:** iz ve seçili parça kıl çizgiyle (LineSubtle), üzerine gelince soluk dolgu. Başlıktaki güneş / ay
   simgesi 1,6 → 1,1 DIP.
3. **≡ menüsü:** her öğe sayfasının simgesiyle (Ayarlar, Windows indir, Ön ayarlar, Loglar, Hakkında, Çıkış: Power).
4. **Bileşen boyutu:** referans aracın çubuğu kalıyor: aynı log ölçeği, 20 px, sayı çubuğun üstünde. Değişen yalnız
   şunlar: köşeler yuvarlak; dolgu gri yerine vurgu renginin soluk tonu, büyük satırda biraz koyu (4 GB'ta en koyu).
   Ayrı bir ince ölçek ve sağa hizalı sayı denendi; kullanıcı beğenmedi, geri alındı.
5. **Bildirim (toast):** 420 genişlik; başlık ve metin satır sarar, kutu yukarı doğru uzar (en çok 220). "…" yok.
6. **Özellikler › İsteğe Bağlı Özellikler tıklanmıyordu:** `TabBar::measure({})` genişlik 0 döndürüyordu. Sayfa
   sekmeleri `min(measure({}).width, …)` ile yerleştirince tıklama alanı 0 piksel oluyordu (çizim sınırın dışına
   taştığı için görünüyordu). Sınırsız ölçüm artık sekmelerin genişliği. WidgetTests'e test eklendi.
7. **Katılımsız üst menüde, tam genişlik:** "Görüntü · Ön Ayar · Araçlar · Katılımsız". Sayfa sol menüsüz açılır.
   En üstte "WinLove kurulum ekranı" kartı:
   - büyük anahtar ve AÇIK / KAPALI etiketi; açıkken kart vurgu çerçeveli;
   - kurulumdaki sırayla 9 aşama çipi (Hesap … Hazırlık); planın sormadıkları soluk;
   - seçili aşamanın uygulama içi çizimi: sihirbazın düzeni, adım çubuğu "i / n", aşamanın denetimleri, planın
     vurgu rengi ve teması (Görünüm'de seçili döşeme, köşede etiket); çizime tıklamak sonraki aşamaya geçer;
   - kapalıyken önizleme soluk ve üstünde "Windows'un kendi ekranları" notu;
   - "Gerçek pencerede önizle" bugünkü `oobe.ps1` önizlemesini açar.
   Form ve canlı XML önizlemesi kartın altında. Kartın yüksekliği içeriğine göre.
8. **İlk açılış tam ekran:** pencere ekranı kaplayarak açılır (`--size` / render verilmediyse).
9. **Ön ayar ISO'da:** referans aracın "ön ayarı düzenlenmiş görüntüye yedekle"si gibi. ISO Oluştur, son tamamlanan
   Uygula'nın ön ayarını (kuyruk, cevap dosyası, boot.wim sürücüleri) ISO köküne `WinLove.wlpreset` olarak yazar.
   Ayarlar › Ön ayarlar › "Ön ayarı ISO'ya kaydet" (varsayılan açık).
10. **ESD sıkıştırma iş parçacıkları** (Ayarlar › Motor ve performans; Otomatik, 1…N mantıksal işlemci):
    - Referans araç ESD'yi kendi WIM yazıcısıyla sıkıştırıyor (`EsdMaxThreads`; ikilide wimgapi fonksiyon adı yok, `$Solid`
      var).
    - WinLove Microsoft'un wimgapi'sini kullanıyor ve onun iş parçacığı parametresi yok. Bu yüzden LZMS dışa
      aktarımı sürerken süreç ilk N mantıksal işlemciyle sınırlanıyor (işlemci benzeşimi) ve sonra geri alınıyor;
      wimgapi'nin iş parçacıkları da bu sınıra uyuyor.
    - Otomatik = bugünkü gibi sınırsız.
11. **DISM motoru:** Otomatik / Bu bilgisayar · v{sürüm} / Windows ADK · v{sürüm}. ADK seçeneği yalnız kuruluysa
    listede. Seçim açılışta verilir (DISM ilk kullanımda bir kez seçiliyor); ADK seçilince wimgapi de ADK'nınki.
    Değişince "WinLove yeniden açılınca geçerli olur" notu çıkar.
12. **Pencere kenarı:** Windows 11'in 1 px DWM kenarlığı vurgu renginde. Windows 10 bu özniteliği desteklemiyor.
13. **Onay kutuları her yerde yuvarlak:** Bileşenler sayfasının halkalı tik stili (`Checkbox::paintBox`, 14 px
    halka, dolgusuz).
14. **Hakkında:**
    - etiketler sürüm satırına binmiyor (başlık, sürüm, etiketler bir blok, kartta ortalı);
    - Sistem kartındaki uzun değerler satır sarıyor;
    - kısa pencerede sayfa kayıyor (`ScrollableBody`).
Kanıt:
- Testler geçiyor. Yeni testler: "TabBar measured without a limit …"; ayarlarda dismEngine / esdThreads /
  presetInIso gidip geliyor.
- Render'lar: sol menü 1100×560 @150 %, ≡ menüsü, Uyumluluk penceresi, uzun bildirim, Hakkında 1000 / 1180,
  Ayarlar, Bileşenler gerçek depo boyutlarıyla (`--demo-store-share`), Katılımsız (açık / kapalı, açık / koyu tema,
  1100×720).
- Yeni render seçenekleri: `--demo-toast=başlık|metin`, `--demo-app-menu`.
Görülmeyen:
- Gerçek pencerede: tam ekran açılış, DWM kenarı, sürükleyerek kaydırma, ADK'ya geçiş.
- ESD'nin sınırlı işlemciyle süresi.
- ISO kökündeki ön ayar: kod yolu, VM'de ISO kurulmadı.

## D-122 — referans aracın aynı özelliğe bağladığı paketler ve yeni bileşen tanımları (2026-11-09)
Bağlam: Bu partinin işi "daha çok tanım" (referans araç ~1.300 satır, WinLove 268). Yöntem:
- Kullanıcının bağlı 25H2 imajında (26200.9818, yalnız okundu) `wlcli store-share` çalıştırıldı. Katalog WinLove'un
  desenlerine ek olarak sona bir "geri kalan" (`*`) deseni aldı. WinLove'un hiçbir desenle seçmediği 6.357 ad
  (4,7 GB) çıktı ve her birinin boyutu ayrıca ölçüldü.
- Bu adlar referans aracın ikilisindeki AddComponent desenlerine en özgül eşleşmeyle verildi. Kodda yan yana duran
  desenler tek tanım sayıldı (`scratchpad\gap2.py`, `ntl_gap.txt`). Sonuç: referans araç bu payın 2,1 GB'ını tanımlıyor;
  geri kalan 2,6 GB'ı ikisi de tutuyor.
- Bu payın bir kısmı WinLove'da zaten CBS paketiyle kaldırılıyor (Defender, OneDrive, Fotoğraf Görüntüleyicisi, medya
  akışı …); store-share yalnız desenleri sayar. Bunlar elle ayıklandı.
- Kalanlar için referans aracın satırları (`components_catalog.json`) ve renkleri incelendi. Renklerin karşılığı: sarı
  Hafif = light, mavi Genel = general, yeşil Gizlilik = privacy, kırmızı Önerilmez = notRecommended.
Karar:
- **Var olan tanımlar, referans aracın aynı özelliğe bağladığı adlarla tamamlandı:**
  - Bluetooth (17,5 → 36,6 MB): Windows.Devices.Bluetooth, OneCoreUAP Bluetooth ses / kullanıcı hizmeti, BT PAN
    arayüzü, DAF sağlayıcısı, işlev bulma.
  - Uzak Masaüstü sunucusu: `rdpserverbase`.
  - WIA: WIA otomasyonu, eSCL tarama. TWAIN bileşeni Windows'un yazımıyla (`windowsimageacqusition`) adlandırıldı.
  - Biyometri: kimlik sağlayıcıları, VBS trustlet'i, bağdaştırıcılar, geri bildirim arayüzü.
  - Akıllı kart: kimlik sağlayıcısı, WinRT API'si.
  - Ebeveyn denetimleri: aile güvenliği. Yedekleme: Windows 7 Yedekleme denetim masası.
  - Telemetri dosyaları: DeviceCensus, UTC. Uyumluluk değerlendirici: CompatTelRunner, GeneralTel.
  - Oyun DVR: yayın / kayıt, Oyun Çubuğu bileşenleri.
  - Kapsayıcılar: Container Manager (CmService).
  - IE: ieinstal, F12 çerçevesi.
  - WWAN: mobil geniş bant medya yöneticisi, MBB sınıf uzantısı, CellCore.
  - Telefon: telefon yığını, VoIP çalışma zamanı. TAPI: hizmet sağlayıcıları.
  - Sorun gidericiler: 16 sorun giderme paketi ve PowerShell modülleri.
  - Windows Media Player'ın sorun gidericileri. Sistem Geri Yükleme görevleri.
- **Yeni tanımlar (referans araçtaki düzeyleriyle):**
  - IEFrame (kırmızı): Store, Office ve Visual Studio korumaları ile Önerilen tutar.
  - Yardım dosyaları / CHM (gizlilik).
  - VCM kodekleri / Indeo (kırmızı): Netflix koruması ile Önerilen tutar.
  - Windows Şimdi Bağlan (genel, wcncsvc).
  - Yakınlık ve Wi-Fi Direct (gizlilik).
  - Ayar eşitleme (hafif).
  - Pencere öğeleri platformu (genel).
  - Metin tanıma / OCR (genel).
  - Adım Kaydedici (genel).
  - Toplam 277 bileşen.
- **Alınmayanlar:**
  - Taşınabilir aygıtlar (WPD/MTP): kullanıcının 2026-10-02 kuralı; telefon ve kamera bağlantısı hiçbir zaman
    seçenek olmaz.
  - `rdpbase` / `rdpnano`: istemciyle ortak olabilir.
  - Mobil geniş bant API'si: referans araçta kırmızı.
  - XPS dönüştürücüleri: yazdırma yolu.
  - Grup İlkesi düzenleyicisi: referans araç yalnız istemciyi sunuyor, o da kırmızı.
  - Sürücü paketleri (`*.inf` adları): WinLove sürücüleri derin kaldırmayla ele alır.
  - Referans aracın kilitli çekirdek tanımları (uygulama modeli, aygıt ilişkilendirme, MSA …).
- Referans aracın tabloları kopyalanmadı. Desenler Windows'un kendi bileşen adları; adlar, notlar ve gruplama WinLove'un.
Kanıt:
- `scratchpad\sharediff.py`: önce / sonra sahiplik. Başka bir WinLove tanımından tek ad geçmedi; bütün yeni adlar
  sahipsiz paydan geldi (165 MB).
- Testler: katalog ayrıştırma, her kırmızı satır Önerilen'de, hiçbir desen korunan bir adla başlamıyor. Release dist geçti.
- Dosyalar imajda (psr.exe, wcncsvc.dll, ProximityService.dll).
Görülmeyen: yeni tanımların VM'de kaldırılması. Yol, D-120'de onlarca bileşenle denenen D-113 kaldırması.

## D-121 — referans aracın kalan uyumluluk korumaları, uygulama korumaları ve "Windows'un temeli" (2026-11-09)
Bağlam: Kullanıcı: "Referans araçta olup bizde olmayan özellikleri ekle". İlk iş uyumluluk korumalarının eksiğiydi. Referans aracın
uyumluluk penceresinde (`ui_compat_dialog.tsv`) varsayılan açık olup WinLove'da karşılığı olmayanlar vardı: Windows
Etkinleştirme, TPM / Güvenli Önyükleme (hile önleme), SFC, Yükselt + Ana Bilgisayar Yenileme, Video oynatma, USB,
Sistem yazı tipleri ve Uygulamalar grubu (BattleNet, Discord, EA Javelin, Eski oyunlar, Office, Netflix, NVIDIA,
Spotify, TeamViewer, Visual Studio). Her korumanın kilitlediği satırlar referans aracın bileşen dökümünden
(`components_catalog.json`, "Uyumluluk nedeniyle kilitli" notları) çıkarıldı. Servisler, Hizmetler sayfasındaki
"Bileşen:" bağıyla eşlendi (`scratchpad\guardrows.py`, `svcguard.py`). Bulgular:
1. Referans araç bir servisi, ait olduğu bileşenle birlikte kilitler. WinLove'da "Yazdırma" yalnız Spooler'ın devre dışı
   bırakılmasını engelliyordu; `printing` bileşeni (Spooler'la birlikte) kaldırılabiliyordu. Bluetooth (Wi-Fi
   koruması), Xbox servisleri, RDP sunucusu, SMB sunucusu, işlev bulma, SSDP ve mobil etkin nokta için de aynıydı.
2. Referans aracın Hizmetler sayfası 13 servisi "Temel işlevsellik" için her zaman kilitler. Bunlardan DeviceAssociationService,
   AppReadiness, wlidsvc, TokenBroker, WindowsTrustedRT, fvevol, rdyboost, DXGKrnl ve Wdf01000 WinLove'un korunan
   servislerinde yoktu. fvevol ve rdyboost birim sınıfının filtreleri: anahtarı silinirse Windows açılmaz.
3. Video oynatma (EVR), USB ve Bluetooth sürücüleri, FS filtre sınıfları WinLove'da kaldırılabilir değil
   (`microsoft-windows-enhancedvideorenderer`'ı hiçbir tarif seçmiyor; derin kaldırma yalnız 6 eski sınıf). Bunlar için
   koruma gerekmiyor. Sistem yazı tipleri D-119'la zaten her zaman tutuluyor. Spotify'ın tuttukları (OneCore proxy
   saplaması, TWINUI) D-120'den beri hiç gitmiyor. TeamViewer bileşen kilitlemiyor.
Karar:
- **Motor (`core/ops/Compat`):** bir `RemoveComponent` adımı, tarifinin (`services`, `appx`) götürdüğü bir servisi ya da
  uygulamayı tutan her açık korumada tutulur. Korumaların listelerine her bileşeni tek tek yazmak gerekmez. Bir
  korumanın bileşen listesi tek satırı da adlandırabilir: `vss:32`, `font:Gadugi`.
- **"Windows'un temeli" (`core`, `always: true`):** kapatılamaz, pencerede kilitli satır. Yalnız servislerinin devre dışı
  bırakılmasını engeller: RpcSs, DcomLaunch, LSM, Power, PlugPlay, BrokerInfrastructure, SamSs, ProfSvc, UserManager,
  CoreMessagingRegistrar, StateRepository, SystemEventsBroker, nsi ve referans aracın 13'ü. Bileşen kaldırmayı tutmaz: o
  servislerin anahtarları zaten kalır. `kProtectedServices`'e 2.'deki adlar eklendi; TokenBroker hariç, o yalnız kendi
  dosyalarıyla (`aad`) gider ve `aad`'yi dört koruma tutar.
- **Yeni korumalar** (hepsi varsayılan açık, referans araç gibi):
  - Windows etkinleştirme: sppsvc, ClipSVC, wlidsvc, Winmgmt, wmiApSrv, Dnscache.
  - TPM: TPM sürücüsü.
  - SFC: WinSxS'i en aza indirme, TrustedInstaller.
  - Yükseltme ve yerinde onarım: migration, edition-upgrade.
  - "Uygulamaların ihtiyaçları" bölümü:
    - Battle.net: curl, seclogon.
    - Discord: WPF, qWave, hidserv.
    - EA Javelin: userdata, UnistoreSvc.
    - Eski oyunlar: DirectPlay, General MIDI.
    - Office: görev zamanlayıcı, MDAC, eski HTML motoru, Exchange eşitlemesi, Appinfo, Çince ve Gadugi / Leelawadee
      UI / Nirmala UI yazı tipleri.
    - Netflix: Media Foundation, PlayReady, PEAUTH.
    - NVIDIA: görev zamanlayıcı, eski HTML motoru, DispBrokerDesktopSvc, qWave, bindflt.
    - Visual Studio: .NET (ASP.NET, WCF, WPF), COM+, WebView2, eski HTML motoru, WSDAPI, 32 bit PowerShell ve VSS.
- **Var olan korumalar referans aracın listeleriyle genişledi:**
  - Windows Update: görev zamanlayıcı, teslim iyileştirme, BITS, DoSvc, NlaSvc, netprofm, mpssvc, wlidsvc.
  - Store: Store altyapısı, teslim iyileştirme, VSS, eski HTML motoru, `aad`; wuauserv, BITS, DoSvc, StorSvc, wlidsvc,
    TokenBroker, mpssvc, VaultSvc.
  - "Yazdırma ve tarama": Yazdırma, WSDAPI, işlev bulma, WIA; yazdırma iş akışı servisleri, ShellHWDetection, stisvc.
  - Wi-Fi: NlaSvc, netprofm.
  - Windows Güvenliği: Güvenlik Merkezi; wscsvc ve Defender sürücüleri.
  - Ağ paylaşımı: SMB sürücüleri.
  - Kamera: Media Foundation, Kamera Çerçeve Sunucusu.
  - Gece ışığı: DisplayEnhancementService.
  - Modern uygulama: VCLibs, .NET Native, UI.Xaml, App Installer; EFS, FileCrypt, bindflt.
  - Kurulum: Sysprep; wlidsvc, CDPUserSvc, UdkUserSvc, SensorService, AppReadiness, EFS, bindflt.
  - Windows Güvenliği, Ağ paylaşımı ve Kamera yeni kullanıcıda açık (referans araçta açık). Bunları daha önce görmüş
    kullanıcının seçimi değişmez (`guardsSeen`, D-120).
- **Pencere:** "Windows" ve "Uygulamaların ihtiyaçları" bölüm başlıkları; `core` kilit simgesiyle, tıklanmaz. Başlıktaki
  sayı ("Uyumluluk · N") yalnız kullanıcının seçtiklerini sayar. CLI `--compat` `always` korumaları da uygular.
- Yalnız kesin bağımlılıklar (D-082) ilkesi sürüyor. Referans aracın tuttuğu ama Windows'un çalışması için gerekmeyenler
  alınmadı: WaaSMedicSvc, Netlogon, luafv, LxpSvc, dmwappushservice ve Yükseltme'nin PcaSvc'si. PcaSvc,
  WinLove'un telemetri bileşeni `appcompat`'ı tutardı.
- Bütün servis adları 25H2 imajının SYSTEM hive'ında var (681 servis). İmajda olmayan TBS, MsSecCore, MsSecFlt ve
  MsSecWfp çıkarıldı.
Kanıt:
- CompatTests: 31 koruma ve varsayılanlar.
  - Bluetooth / mobil etkin nokta Wi-Fi korumasıyla, Xbox Xbox korumasıyla, RDP sunucusu Uzak Masaüstü korumasıyla, SMB
    sunucusu / işlev bulma / SSDP Ağ paylaşımı korumasıyla tutuluyor; korumaların listesinde adları yok.
  - Tarifteki uygulama da uygulamanın kendisi gibi tutuluyor.
  - `core` kaldırmayı geçiriyor ama devre dışı RpcSs / fvevol / TokenBroker'ı geçirmiyor.
  - `powershell:32` tutuluyor, `powershell` tutulmuyor; `font:Gadugi` tutuluyor, `font:Impact` tutulmuyor.
  - Eski ayarda (`guards: []`) Windows Güvenliği kapalı kalıyor; etkinleştirme ve Office açık.
  - `core` kapatılamıyor ve sayılmıyor.
- ComponentStoreTests: fvevol, rdyboost, wlidsvc, AppReadiness, DeviceAssociationService ve Wdf01000 korunuyor;
  TokenBroker korunmuyor.
- Render: Uyumluluk penceresi (`--demo-components --demo-compat`).
- Kullanıcının ön ayarı (`scratchpad\held121.py`), kullanıcının ayarlarıyla: 28 adım tutuluyor.
  - Yeni: curl, userdata, DirectPlay, PlayReady, mailsync ve yazı tipleri, ASP.NET, migration, edition-upgrade, WinSxS
    küçültme.
  - D-120'den: winre, sysprep:32, cdp, search:32.
- Yeni kullanıcıda (CLI `--compat`) 54 adım tutuluyor.
Görülmeyen: VM'de uçtan uca kurulum. Değişiklik yalnız kuyruktan adım düşürüyor ve servis anahtarı bırakıyor;
D-120'nin testi bu ön ayarla masaüstüne geldi.

## D-120 — referans aracın kaldırmalarının dışında tuttukları WinLove'da da kalır; kurulum, uygulama, arama ve gece ışığı korumaları (2026-11-08)
Bağlam: D-119'lu imajda OOBE bitti ama ilk oturumda explorer "Unknown Hard Error" ile çöktü, görev çubuğu gelmedi.
Bileşenler tek tek geri konup denenmek yerine (kullanıcının isteği: "Referans araç tersine mühendislik ile ne koruyor bak, aynı
korumaları ekle") referans aracın kendi tanımları okundu. `reference_unpacked.exe`'de dosya konumu = RVA. .text'teki
`lea reg,[rip+…]` başvuruları çözüldü (31.050 dizgi başvurusu, 428.464 çağrı). Bileşen tanımlarında iki çağrı var:
`0x2125d0` / `0x2122e0` = **ekle** (AddComponent, 2.830 desen), `0x2650d0` = **dışla** (929 desen). README'deki
".NET Framework Core" tanımı doğruladı: `NetFx-Shared-*` ekle, `NetFx-Shared-WCF*` dışla. AddComponent'in yığın
argümanı (`[rsp+0x40]`) bir koşul taşıyor; beş desende bu değer `ServicingStack`. Ön ayarın sildiği her paket adı bu
desenlerle eşlendi (`scratchpad\presetmap.py`). Referans aracın WinLove'dan farklı davrandığı yerler:
1. `microsoft-onecore-accessibility-experience`: referans araçta her zaman kilitli "Modern Uygulama desteği"nin parçası
   (Xaml*, WindowsUI*, UI-*, DataTransfer*, CredDialogController, AppX-Deployment-* ile birlikte). WinLove'un
   `accessibility` bileşeni `microsoft-onecore-accessibility*` ile bunu siliyordu.
2. `tabletpc-platform-input-core` ve `tabletpc-platform-input-service-client` referans araçta ayrı "Metin Girişi Yönetim
   Hizmeti"nin (TextInputManagementService) paketleri. Bu hizmet varsayılan açık Modern Uygulama, Shell Arama ve
   WLAN korumalarıyla kilitli. WinLove'un `tabletpc`'si (`microsoft-windows-tabletpc-*`) ikisini de alıyordu.
3. Windows Insider: `Microsoft-Windows-Flighting*` ekle, `flighting-featureconfiguration*` (Windows'un özellik
   yapılandırması) ve `flighting-onesettingsclient*` dışla. `flighting-settings` ile `cortana-brokeredapi-*` Başlat
   menüsünün tanımında. WinLove'un `insider`'ı dördünü de siliyordu.
4. `ServicingStack` koşullu desenler: `errorreportingfaults` (werfault.exe), `tabletpc-platform-comruntime`,
   `tabletpc-journal`, `securestartup-filterdriver`, `update-orchestratorcore-oobeexpedite`. WinLove'un `wer`,
   `tabletpc` ve `bitlocker` bileşenleri bunlardan üçünü siliyordu.
5. Bağlı Cihazlar Platformu (CDP, CDP-Api, Onecore-CDP-*): referans araçta kırmızı ve varsayılan açık Gece ışığı
   korumasıyla kilitli. CDP Kullanıcı Hizmeti Windows Kurulumu korumasıyla kilitli. Açıklaması "görev çubuğu açılır
   menüleri bunu ister". WinLove'da `cdp` "gizlilik" düzeyindeydi.
6. Referans aracın varsayılan açık uyumluluk korumalarından (`ui_components_defaultplus.tsv`, 151 kilitli satır)
   WinLove'da karşılığı olmayanlar: OOBE / Windows Kurulumu, Modern Uygulama desteği, Shell Arama, Gece ışığı. Ayrıca
   WinLove'un "Önerilen" koruması 36 kırmızı bileşenden yalnız 4'ünü tutuyordu. Koruma sistemi Windows özelliklerini
   (RemoveCapability) hiç görmüyordu: ön ayar `Edge.Webview2.Platform`'u (WebView2) kaldırıyordu. Referans aracın
   "WebView2 Çalışma Zamanı (Çekirdek)" ve "VBScript" satırları Önerilen ile kilitli.
7. Kullanıcının ayarlarında `"guards": []` vardı (08.10, "0 of 13 on"); korumaların hiçbiri açık değildi. CLI `apply`
   ise korumaları hiç uygulamıyordu.
Karar:
- **Motor (`SystemComponents.cpp`, `kProtectedStore`):** 1–4'teki adlar (referans aracın "Modern Uygulama desteği"
  çekirdeği dahil, 24 önek) hiçbir kaldırmayla gitmez. `TextInputManagementService` korunan servislere eklendi.
  Eski bir ön ayar bu adları içeriyorsa adım artık başarısız olmaz: o adlar kalır, gerisi gider. Logda
  "kept, Windows needs them (D-120)" yazar. Bileşenler sayfası tarifleri kurarken bu adları dışarıda bırakır; yalnız
  bunlardan oluşan bir 32 bit satırı hiç gösterilmez. Tam adla korunanlar: `system.web`, `system.web.extensions`,
  `system.web.services`, `system.web.regularexpressions`, `system.web.applicationservices`. Windows PowerShell'in
  `ConvertFrom-Json`'u bunlara dayanır. İlk D-120 laboratuvar turunda ön ayarın eski ASP.NET tarifi (D-117'den önce
  kaydedilmiş, test düzeneği yenilememişti) System.Web'i sildi. WinLove'un kurulum sihirbazı `oobe.json`'u
  okuyamadı ve metinsiz, renksiz açıldı. Arayüz bu tarifi zaten yeniler; motor artık eski tarifte de tutar.
- **Katalog:** `cdp` önerilmez oldu (notu nedenini söyler). Yeni korumalar `compat.json`'da, hepsi varsayılan açık:
  - `setup` (Windows Kurulumu ve OOBE): winrt-core, ie-core, mdm, aad, tasksched, sensors, print3d, vss, cdp, wmi,
    firewall, winre; ilgili servisler.
  - `modern-apps`: winrt-core, dotnet, dotnet-wcf; AppXSvc, TextInputManagementService, camsvc.
  - `shell-search`: search; TextInputManagementService, EventLog.
  - `night-light`: cdp, aad; CDPSvc, TokenBroker.
  Store korumasına mailsync (eşitleme ana bilgisayarı) ve applocker (AppIDSvc) eklendi, kurtarmaya migration.
  "Önerilen" artık **her** kırmızı bileşeni tutuyor; testi katalogdaki her kırmızı satırı denetliyor.
- **Windows özellikleri korunur:** `CompatGuard::capabilities` (adın `~`'den önceki kısmı, her sürüm). WebView2
  korumasında `Edge.Webview2.Platform`, Önerilen'de o ve `VBSCRIPT`. Özellikler sayfasında korunan satır kilitli ve
  soluk; tıklayınca neden korunduğunu söyler. Grup kutuları korunanları atlar. Ön ayardan gelirse kuyruktan düşer
  (D-082).
- **Bir bileşenin 32 bit satırı** (`tabletpc:32`) bileşeniyle birlikte korunur (önceden korumasız geçiyordu).
- **Sonradan eklenen korumalar varsayılanla açılır:** `AppSettings::guardsSeen`, kaydedildiği andaki koruma
  listesidir. Listede olmayan bir koruma katalog varsayılanını alır. Listesiz eski ayarlar D-082'nin 14 korumasını
  görmüş sayılır. Böylece korumaları kapatmış bir kullanıcıda bile kurulum, uygulama, arama ve gece ışığı açılır.
- **CLI:** `wlcli apply … --compat=<compat.json>` o dosyanın varsayılan korumalarının tuttuğu adımları uygulamaz ve
  tek tek yazar. Arayüzle aynı kural kullanılır (`core::ops::compatConflicts`).
- Referans aracın tabloları kopyalanmadı. Taşınan şey kural: hangi Windows paketinin neden kaldığı. Adlar Windows'un kendi
  bileşen adları, gruplama ve notlar WinLove'un.
Kanıt:
- CompatTests: 18 koruma, varsayılanlar; her kırmızı bileşen Önerilen'de; `tabletpc:32`; `Edge.Webview2.Platform~~~~`
  ve `VBSCRIPT~~~~` tutuluyor, Not Defteri tutulmuyor; eski ayar (`guards: []`, `guardsSeen` yok) → store ve
  recommended kapalı, setup ve night-light açık; kullanıcı kapatınca kapalı kalıyor.
- ComponentStoreTests: 1–4'teki adlar korunuyor; aynı bileşenlerin geri kalanı (accessibilitycpl,
  legacyinkingcomponents, flighting-featuretuning, errorreportingconsole, securestartup-core, ui-xaml-maps) korunmuyor.
- ComponentTests: hiçbir kaldırılabilir satırın deseni korunan bir adla başlamıyor.
- RE: `scratchpad\leascan.py`, `ntlpat.py`, `presetmap.py`, `presetmap.txt`.
- Uçtan uca (2026-11-09, `build\lab\d120`). Kullanıcının masaüstündeki ön ayarı (859 adım) arayüzün yaptığı gibi
  yenilendi (tarifler ve `oobe.ps1`) ve `dist\wlcli apply --compat` ile özgün imaja baştan uygulandı. Sonuç: 836 adım,
  korumaların dışarıda bıraktığı 23 adım, 2 kalıcı özellik (Sense, Work Folders; DISM 0x800F0825). ISO → VMware'de
  boş diske temiz kurulum: Kurulum → WinLove sihirbazının 7 sayfası (WINLOVE-PC kabul) → ilk oturum → masaüstü.
  Görev çubuğu ve Başlat duruyor, explorer çökmüyor; Dosya Gezgini, Ayarlar (Windows Update "Güncel") ve WinLove'un
  program kurulum penceresi (winget: Chrome, Lightshot) çalışıyor.

## D-119 — Windows'un ve web motorlarının geri düştüğü yazı tipleri ile WebView2'nin kaydı kalır (2026-11-08)
Bağlam: D-118'li imajda OOBE "Bilgisayarım neden yeniden başladı?" döngüsüne girmeye devam etti. CloudExperienceHost
(WWAHost / EdgeHTML) ilk sayfada 0xC0000602 ile durdu. Ön ayar 136 yazı tipi dosyası siliyordu. VM'de önce hepsi, sonra
yalnız 34 çekirdek dosya (Arial, Times New Roman, Courier New, Tahoma, MS Sans Serif …) geri kondu; iki seferde de OOBE
geçti. Aynı incelemede `edge-update` tarifi `HKLM\SOFTWARE\WOW6432Node\Microsoft\EdgeUpdate` anahtarını bütünüyle
siliyordu; WebView2'nin kaydı (`Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}`) da gidiyordu.
Karar:
- `isCoreFontFamily` (`ImageInventory`): Segoe UI ailesi, Segoe Fluent Icons / MDL2 Assets, Marlett, Arial, Times New
  Roman, Courier New, Courier, Tahoma, Microsoft Sans Serif, MS Sans Serif, MS Serif, Small Fonts, Consolas, Lucida
  Console, Symbol, Wingdings, Webdings, Modern, Roman, Script. Bileşenler sayfası bunları kilitler. Motor, bu
  ailelerden bir yazı tipini silen tarifin o kısmını uygulamaz (eski ön ayarlar için; "the font stays … D-119").
- `edge-update` yalnız Edge Update'in kendi kaydını, kaldırma girdisini ve servislerini siler. WebView2 ve Edge'in
  istemci kayıtları kalır.
- Not: ön ayar disk belleği dosyasını kapatıyor (`PagingFiles` boş). Çökmeye neden olmadığı 8 GB bellekle denendi;
  yine de az bellekli makinelerde risklidir.
Kanıt: InventoryTests "the fonts Windows and its web engines fall back to are held (D-119)"; ComponentTests "Edge
Update leaves WebView2's registration …"; VM: 34 çekirdek dosya geri konunca OOBE geçti; yalnız ince ayarlı imaj
(bileşen kaldırması yok) masaüstüne geldi.

## D-118 — Kalan bir modülün içe aktardığı ya da gecikmeli yüklediği dosya gitmez; EFS'nin çekirdeği kalır (2026-11-08)
Bağlam: D-117'li kodla ön ayar özgün ISO'ya baştan uygulandı (857/859 adım), VM'e temiz kuruldu. WinLove'un kurulum
sihirbazı yedi sayfasıyla çalıştı, hesap oluştu. Ardından Windows masaüstü yerine sürekli **"Bilgisayarım neden yeniden
başladı?"** sayfasına döndü. Ayrıntı:
1. UnattendGC\setupact.log: CloudExperienceHostBroker OOBE'yi bitirirken imaj durumunu `IMAGE_STATE_COMPLETE` →
   `IMAGE_STATE_SPECIALIZE_RESEAL_TO_OOBE` yaptı; sonraki her açılışta msoobe kurtarma ("Recovery") sayfasını açtı.
   Nedeni: kullanıcı OOBE'sini çizen CloudExperienceHost paketi `defaultuser0` için kaydedilemedi.
2. Kurulum bayrakları elle kapatılıp (OOBEInProgress, SetupType, ImageState) yeni kullanıcılarla oturum açıldı:
   masaüstü geldi ama görev çubuğu yoktu; explorer, StartMenuExperienceHost ve SearchHost birkaç saniyede bir
   çöküyordu ("Unknown Hard Error", "yığın tabanlı arabellek taşması"). AppXDeploymentServer günlüğü:
   `C:\Program Files\WindowsApps` altındaki **her** paket (VCLibs, UI.Xaml, WindowsAppRuntime, Store, App Installer …)
   `windows.stateExtension` adımında 0x80004001 (E_NOTIMPL) ile düşüyor, olay 5210 "Creation of the application
   data local root folder failed". Başlat ve görev çubuğu bu çerçevelere dayandığı için kabuk çöküyordu.
3. Kaynağı bulmak için AppX'in kendi TraceLogging sağlayıcıları (GUID'ler sağlayıcı meta verisinden okundu:
   `Microsoft.Windows.AppxDeploymentFallback` aa1b41d3-…, `AppXDeploymentServer` fe762fb1-…,
   `AppXDeploymentExtensions` d9e5f8fb-…) `logman` ile yakalanıp `tracerpt` ile açıldı. İlk hata:
   `onecore\admin\appmodel\osim\src\deh\appx\state\lib\state.cpp:1041 — CdplIsSupported → 0x80004001`, ardından
   `IsDPLSupportedOnPlatform` ve `SetDataProtectionLevel`. `CdplIsSupported`, `ext-ms-win-security-efswrt` üzerinden
   `efswrt.dll`'den gelir; efswrt → `feclient.dll` → `efscore.dll` ve EFS hizmeti (lsass içinde, `efssvc.dll`).
   `efs` bileşeni (`microsoft-windows-efs*` + EFS hizmeti) bunların hepsini siliyordu.
4. Doğrulama: aynı VM'e yalnız `efscore/efssvc/efslsaext.dll`, `efsui.exe` ve özgün EFS hizmet anahtarı geri kondu
   (WinPE turu, çevrim dışı `reg import`). Sonuç: VCLibs kaydı "ok", son 8 dakikada 0 kayıt hatası, kullanıcının paket
   sayısı 34 → 44, Store ve Başlat işlemleri çalışıyor.
5. Aynı incelemede statik tarama (imajda kalan her modülün içe aktarma ve **gecikmeli yükleme** tabloları, şemayla
   çözülerek) 33 silinmiş DLL buldu. D-117 yalnız statik içe aktarmaya bakıyordu; bunlar ya E_NOTIMPL döndürür ya da
   süreci durdurur. Örnekler:
   - `AppXDeploymentServer.dll` → `IsoSessionCore.dll` (yalıtım oturumu kaldırması);
   - `shell32`, `userenv`, `twinui`, `AppXDeploymentExtensions.onecore` → `cldapi.dll` (Bulut Dosyaları);
   - `feclient` → `efscore.dll`;
   - `twinapi.appcore` / `twinui.appcore` → `ContactActivation.dll`;
   - `edgehtml` → `edgeangle.dll`;
   - `user32` / `ole32` → `wtdccm.dll`;
   - `xamlinputviewhost` → `magnification.dll` (statik!).
Karar:
- **`keptImportedFiles` (`core/image/BootDrivers`):** Bir adımın sileceği dosyalardan, imajda kalan bir modülün içe
  aktardığı **ya da gecikmeli yüklediği** her biri kalır, geçişli olarak. Kalan modül = bu adımda gitmeyen ya da
  kendisi korunan modül. Taranan klasörler: Windows, System32, System32\drivers, SysWOW64 (doğrudan içerik) ve
  SystemApps ile ImmersiveControlPanel'in tamamı. Çözümleme: API kümesi şeması → System32 (SysWOW64'teki 32 bitlik
  bir modül için SysWOW64), System32\drivers, modülün kendi klasörü. İçe aktarma listeleri dosya boyu + yazma
  zamanıyla süreç boyunca önbelleklenir. Her adımda yalnız klasör listesi tazelenir; 374 adımlık ön ayar yavaşlamaz.
  `removeStoreCopies` ve `removeComponent` tarif yollarında uygulanır. Logda "kept X: a module that stays needs it
  (Y delay-loads it, D-118)" yazar. Şema okunamazsa hiçbir modül gitmez.
- **`efs` bileşeni artık yalnız EFS'nin arayüzünü alır** (`efs-ui`, `efs-rekeywiz`, `efsadu`); servis listesi boş.
  Çekirdek kitaplık, LSA eklentisi, platform eklentisi, util kitaplığı ve EFS hizmeti kalır. Bunlar import kuralıyla
  yakalanamaz, çünkü efswrt → EFS bağı bir hizmet çağrısıdır. Adı "Şifreleme Dosya Sistemi (EFS) arayüzü" oldu; notu
  nedenini söyler. Arayüz ön ayarı açarken tarifi katalogdan yeniler (D-076), eski ön ayarlar da düzelir.
Kanıt:
- BootDriverTests "imports: a module that stays keeps …": bu PC'nin `feclient.dll`'i `efscore.dll`'i gecikmeli
  yüklüyor (statik değil); sahte imajda `efscore.dll` "feclient.dll delay-loads it" nedeniyle kalıyor; feclient aynı
  adımda giderse kalmıyor; şema yoksa hata.
- ComponentTests "EFS leaves the core …".
- VM: EFS geri konunca paket kaydı düzeldi (yukarıda).
Sonuç (2026-11-08): D-118'li imajda paket kaydı düzeldi ama OOBE yazı tipleri yüzünden yine döndü (D-119); o da
düzeltilince ilk oturumda explorer çöktü (D-120).

## D-117 — Windows'un açılıp oturum açması ve Kurulum'u bitirmesi için gereken her dosya kalır (2026-11-08)
Bağlam: D-116'dan sonra kullanıcı ISO'yu yeniden oluşturdu. VM testi (VMware, EFI, NVMe, ekran görüntüleriyle) aynı
0xc000000f'i verdi; `fvevol.sys` bu sefer imajdaydı. Sorun katman katman çıktı. Her katman ayrı doğrulandı: dosya VM
diskine kurulum ortamından (aynı yapı, boot.wim) elle kondu ve yeniden açıldı.
1. **Önyükleyici, 0xc000000f.** `ntoskrnl.exe`, `werkernel.sys`'i API kümesi üzerinden içe aktarır
   (`ext-ms-win-ntos-werkernel-l1-1-1`). `wer` bileşeninin deposu globu (`microsoft-windows-errorreportingkernel`) onu
   sildi; çekirdek yüklenemedi. Hyper-V globu `winhv.sys` / `winhvr.sys` / `vmbkmcl.sys`'i sildi, ama onları içe aktaran
   `vmbus.sys` (önyükleme sürücüsü) ve `Vid.sys` (sistem başlangıcı) kaldı ("Vid.sys cannot find winhvr.sys" açılır
   penceresi).
2. **Siyah ekran, dönen nokta donuk.** VM belleği Volatility ile okundu (suspend → `.vmem`). Oturum 1'in `smss`'i ve
   `csrss`'i bir saniyede bitmişti; `winlogon.exe` hiç başlamamıştı. Kurulum ortamında olup imajda olmayan 183 dosya geri
   konunca oturum açıldı. Ardından ikiye bölme yapıldı: her tur bir veri CD'sindeki betikle dosya sildi, sonra açılış
   denendi; 8 turda tek dosyaya inildi: **`utcapi.dll`**. Telemetri kaldırması onu siliyor. `winlogon.exe`,
   `wininit.exe` ve `lsm.dll` onu adıyla yüklüyor; içe aktarma tablosunda yok.
3. **"Windows Kur, Windows'u bu bilgisayarın donanımı üzerinde çalışacak şekilde yapılandıramadı."** setuperr.log:
   `Could not load DLL …\ProvSysprep.dll`. Provisioning kaldırması onu sildi, ama
   `System32\Sysprep\ActionFiles\Specialize.xml` onu çağırıyor. O da geri konunca `ProvSysprep.dll`'in adıyla başlattığı
   `provtool.exe` eksik çıktı (0x80070002).
4. Oturum açıldıktan sonra statik kontrol daha fazlasını gösterdi:
   - `explorer.exe` `AEPIC.dll`'i içe aktarıyor; appcompat kaldırması onu siliyordu.
   - `iertutil.dll` `srvcli.dll`'i içe aktarıyor; SMB sunucusu kaldırması onu siliyordu.
   - Kalan otomatik servislerin DLL'leri silinmişti; örneğin TextInputManagementService → `TabSvc.dll`.
Karar (`core/image/BootDrivers`, `keptBootFiles`): Kaldırma, Windows'un açılması, oturum açması ve Kurulum'u bitirmesi
için gereken dosyaları hangi bileşene ait olursa olsun bırakır. Korunanlar:
- D-116'nın sürücüleri ve filtreleri.
- **Yükleyicinin kendi dosyaları:** `ntoskrnl.exe`, `hal.dll`, `kdcom/kd/kdstub/kdnet.dll`, `ci.dll`, mikro kod
  güncellemeleri, `apisetschema.dll`, `l_intl.nls`, `winload/winresume`, `securekernel.exe`, `skci.dll`. Bunlara
  `Control\Nls\CodePage`'in ACP / OEMCP dosyaları da eklenir.
- **Oturum ve kurulum programları:** smss, csrss + sunucu DLL'leri, wininit, winlogon, services, lsass, svchost, LogonUI,
  dwm, userinit, explorer, sihost, fontdrvhost, ctfmon, taskhostw, RuntimeBroker, dllhost, conhost, cmd, rundll32, reg,
  consent, setupcl, `oobe\windeploy / setup / msoobe / oobeldr`, PowerShell.
- **Kendi kendine başlayan (Start 2) ve kalan her Win32 servisinin** ServiceDll'i ya da programı. Bu kural yalnız
  `Windows\` altındaki ikili dosyalar için geçerlidir. Program Files'taki bir servis başlamazsa (Edge kaldırıldıktan
  sonra güncelleyicisi) Windows yine açılır; ilk laboratuvar çalışması bu sınır yokken EdgeUpdate klasörünü tutmuştu.
  Adım servisi de siliyorsa bu kural uygulanmaz.
- **Kurulum'un sysprep modülleri:** `Sysprep\ActionFiles\*.xml` içindeki `moduleName`'ler, `$(runtime.system32)` /
  `$(runtime.windows)` çözülerek.
- **Yukarıdakilerin içe aktardığı her modül, geçişli olarak.** PE içe aktarma tablosu okunur. `api-` / `ext-` adları
  imajın kendi API kümesi şemasıyla (sürüm 6) hostlarına çözülür. Modül System32'de, System32\drivers'da ve içe
  aktaranın klasöründe aranır.
- **Oturum programlarının ve sysprep modüllerinin adıyla yüklediği / başlattığı .dll / .exe'ler** (`namedModules`: tek
  baytlık ya da UTF-16, NUL ile biten "ad.dll" / "ad.exe"). Oturum programlarından yalnız adı geçen DLL'ler kalır;
  adı geçen programlar (notepad, taskmgr, werfault, sethc …) oturumu başlatmaz, gidebilir. Kurulum zincirinde ise
  adı geçen programlar da kalır ve taranır (`ProvSysprep.dll` → `provtool.exe`). İlk laboratuvar çalışmasında
  sınırsız zincir winlogon → … → taskmgr.exe → `Windows.WARP.JITService.exe`'ye kadar gidip WARP kaldırmasını boşa
  çıkarmıştı. winlogon + wininit + diğerleri toplam yaklaşık 70 farklı DLL adı verir. `lsaiso.exe` (Credential
  Guard) oturum listesindedir.
- **API kümesi şemasının adını verdiği her host** (imajda olanlar). Bu ayrı bir kuraldır ve içe aktarma yürüyüşünden
  sonra eklenir, böylece bir dosya içe aktarıldıysa kayıttaki nedeni o kalır. Gerekçe, VM'deki beşinci katman:
  WinLove'un kendi kurulum sayfaları açıldı ama hiçbir yazı yoktu. Bellekte
  `PropertySetterNotSupportedInConstrainedLanguage` bulundu: PowerShell kısıtlı dil kipindeydi. Nedeni şu: AppLocker
  kaldırması `srpapi.dll`'i siliyordu; `KernelBase.dll` uygulama denetimi sorgusunu adıyla yüklediği bu dosya
  üzerinden yapıyor (`ext-ms-win-security-srp`), dosya yoksa PowerShell güvenli tarafta kalıyor. Aynı yoldan
  `winlogonext`, `wininitext`, `userinitext`, `winsrvext`, `dwminit` gibi oturum zincirinin "ext" hostları da yalnız
  gecikmeli yüklenir ve içe aktarma tablosunda görünmez. Bu PC'de ölçüldü: oturum programlarının kapanışı 186 dosya /
  161 MB; şema hostları ve onların içe aktarmalarıyla 464 dosya / 306 MB. KernelBase'in adlarını taramak ise 690 dosya
  ederdi, o yüzden yapılmadı.
- **Bir API kümesi hostunun adıyla yüklediği DLL'ler**, host en çok 10 mevcut DLL adı taşıyorsa korunur. Gerekçe:
  `srpapi.dll` kalınca da kısıtlı dil kipi sürdü, çünkü srpapi `appidapi.dll`'i adıyla yüklüyor. Doğrulama: VM'de
  sihirbaz yerine çalışan küçük bir C++ tanı programı (`WldpGetLockdownPolicy`, `SaferIdentifyLevel`, SYSTEM olarak,
  specialize içinde) ve `$ExecutionContext.SessionState.LanguageMode`. appidapi eklenince sonuç `FullLanguage` oldu.
  Çok sayıda ad taşıyan bir host bağımlılık listesi değil katalogdur: KernelBase 546, shell32 90 ad taşır.
  Sınır 10'dayken korunan küme 45 dosya büyür; sınırsız olsaydı 934 dosya / 627 MB olurdu.
- **ASP.NET kataloğu (`dotnet-aspnet`) artık `system.web*` almıyor.** Yerine yalnız ASP.NET'e özgü derlemeler
  geliyor: mobile, entity, dynamicdata, datavisualization, `aspnet_*` araçları, netfx4-aspnet bileşenleri. Gerekçe,
  VM'deki altıncı katman: dil kipi `FullLanguage` olduktan sonra da sihirbaz yazısızdı. Sihirbazın kendi
  günlüğünde `ConvertFrom-Json : 'System.Web, Version=4.0.0.0' … yükleyemedi` yazıyordu. Windows PowerShell
  5.1'in JSON komutları System.Web.Extensions → System.Web'e dayanır; Microsoft.PowerShell.Commands.Management de
  System.Web.Services'e dayanır. Derleme başvuruları okunarak çıkarıldı: System.Web, System.Web.Services,
  System.Web.RegularExpressions, System.Web.ApplicationServices, System.Web.Extensions. Artık bunlar yalnız bütün
  .NET Framework (`dotnet`) kaldırılırken gider. Arayüz bir ön ayarı açarken tarifleri katalogdan yeniler (D-076);
  `wlcli apply` ise ön ayarın kaydettiği tarifi kullanır. Bu yüzden laboratuvar ön ayarın düzeltilmiş bir kopyasıyla
  koştu. Test: ComponentTests "ASP.NET leaves the System.Web assemblies …".
- **Kurulum sihirbazındaki (`resources/scripts/oobe.ps1`) Türkçe "I" hatası.** Bu katmanlar geçilince sihirbaz Türkçe
  Windows'ta "WINLOVE-PC"yi geçersiz bilgisayar adı saydı. Nedeni: PowerShell'in `-match`'i büyük-küçük harf
  duyarsızdır ve tr-TR kurallarına uyar; "I"nın küçüğü "ı" olur ve `[a-z]`'ye girmez. Düzeltmeler:
  - bilgisayar adı ve ağ anahtarı denetimi `-cnotmatch` / `-cmatch` oldu;
  - varsayılan bilgisayar adı `ToUpperInvariant()` + `-creplace` ile üretiliyor;
  - ayrılmış hesap adları (`administrator` …) ve diğer karşılaştırmalar `ToLowerInvariant()` kullanıyor. Önceden
    "ADMINISTRATOR" Türkçede "admınıstrator" olup denetimden kaçıyordu.
- Şema okunamazsa (Windows 10 öncesi) çağıran hiçbir modül ya da NLS dosyasını silmez.
- İçe aktarma ve ad listeleri dosya boyu + yazma zamanına göre süreç boyunca önbelleklenir. Bu sayede 374 bileşenlik bir
  ön ayar her adımda explorer.exe'yi yeniden okumaz.
- `removeStoreCopies` ve `removeComponent` tarif yolları bu kümeyle süzülür. Logda "kept X: Windows needs it before it
  starts (neden)" yazar.
Kanıt:
- BootDriverTests: bu PC'nin `ntoskrnl.exe`'si `ext-ms-win-ntos-werkernel` içe aktarıyor ve şema onu `werkernel.sys`'e
  çözüyor; `winlogon.exe` adıyla `utcapi.dll` ve `userinit.exe` yüklüyor; Specialize.xml'den `ProvSysprep.dll`
  bulunuyor; sahte imajda kapanış doğru dosyaları tutuyor; şema yoksa hata dönüyor.
- Laboratuvar testi (`WL_TEST_IMAGE_ROOT`, özgün 26200.9818 Pro imajı salt okunur bağlı): 223 sürücü/filtre/servis
  girdisi, 852 dosya korunuyor. EventLog → wevtsvc.dll, ACP kod sayfası, `utcapi.dll`, `srpapi.dll`,
  `ProvSysprep.dll` var; `taskmgr.exe` ve WARP JIT servisi yok (26/26).
- VM: yukarıdaki her katman eksik dosya elle konarak tek tek aşıldı.
Uçtan uca (2026-11-08): ön ayar D-117'li kodla özgün ISO'ya baştan uygulandı ve temiz kuruldu. Açılış, specialize ve
WinLove'un sihirbazı tamamlandı (hesap, ad, saat dilimi, gizlilik). Ardından OOBE "Bilgisayarım neden yeniden başladı?"
döngüsüne girdi; nedeni ve düzeltmesi D-118.

## D-116 — Önyükleme sürücüleri ve aygıt filtreleri hiçbir kaldırmayla gitmez (2026-11-07)
Bağlam: Kullanıcı raporu. 859 adımlık ön ayar kaydedildi, ISO kuruldu (VMware, EFI, NVMe). İlk açılış önyükleyicide
"Kurtarma — 0xc000000f" ile durdu. VM diski 7-Zip ile salt okunur açıldı:
  - BCD normal.
  - `System32\drivers\fvevol.sys` yok. Servisi duruyor: Start 0, ErrorControl 3 (kritik), grup "PnP Filter", Volume
    sınıfının LowerFilters'ında.
  - Silen, `bitlocker` bileşeninin D-113 deposu globu (`microsoft-windows-securestartup*`): BitLocker'ın filtre sürücüsü
    de o bileşenlerden biri.
  - Aynı yoldan `scfilter.sys` (akıllı kart okuyucu sınıfının UpperFilters'ı) da gitmişti; açılışı değil o aygıtı bozar.
Karar (`core/image/BootDrivers`):
- **Korunan sürücüler.** `findBootDrivers` imajın SYSTEM'ini offreg ile okur (tüm denetim kümeleri). Korunanlar:
  - Start 0/1 olan çekirdek / dosya sistemi sürücüleri.
  - Önyükleme yolu gruplarındakiler (Boot / System Bus Extender, SCSI miniport / port / class, Primary Disk,
    Boot File System, Filter, PnP Filter). Ntfs Start 3'tür ama önyükleyici onu grubundan yükler. stornvme / storahci
    imajda Start 3'tür, sistem diskini sürdükleri PC'de önyükleme sürücüsü olurlar.
  - Bir aygıt sınıfının Upper/LowerFilters'ında adı geçen her sürücü.
- **`removeStoreCopies` (D-113).** Kurulu kopyalar arasından bunları çıkarır ve logda "kept …" yazar.
  - Start 0/1 bir sürücünün dosyası, yalnız aynı adım servisini de siliyorsa gider: o durumda onu yükleyen bir şey kalmaz.
  - Filtre sürücüsü hiç gitmez. Tarifin `services` listesinde olsa bile anahtarı kalır, çünkü sınıf adı tutar ve
    yüklenmeyen bir birim filtresi 0x7B demektir.
  - SYSTEM okunamazsa hiçbir .sys silinmez.
- **`removeComponent` tarif yolları.** Bir yol böyle bir sürücüyse ya da böyle bir sürücüyü içeriyorsa o yol atlanır
  (uyarı yazılır).
Kanıt: BootDriverTests. Bunlara VM'in gerçek hive'ı üzerinde laboratuvar testi de dahil (`WL_TEST_IMAGE_ROOT`): 158 sürücü
bulundu; fvevol (erken + filtre), volsnap, Ntfs ve stornvme var, WSearch yok.
Kullanıcının kaydedilmiş imajı yeniden uygulanmadan onarıldı. Özgün ISO'dan (aynı yapı 26100.9818) alınan `fvevol.sys`
ve `scfilter.sys`, install.wim'e TrustedInstaller sahipliği ve kardeş dosyaların ACL'siyle kondu; sonra commit edildi.
fvevol'un bağımlılıkları imajda var (ksecdd, CEA).
Görülmeyen: onarılan ISO'nun VM'de açılışı. Kullanıcı ISO'yu yeniden oluşturup denemeli.

## D-115 — Küçültülmüş ya da servis edilemez imajda CBS adımı çalışmaz; küçültme yükleyicileri tutar (2026-11-07)
Bağlam: Kullanıcı raporu (`WinLove-apply.log`). Held kalan (kaydedilmemiş) bir çalışmadan sonra aynı ön ayar (1031 adım)
bağlı imaja yeniden uygulandı. İlk çalışma WinSxS'i küçültmüştü (D-079). `bio-enrollment` paketlerini DISM'le kaldırırken
CSI "AppX Registration Installer"ı yükleyemedi: `appxreg.dll`, D-079 listesinde olmayan
`microsoft-windows-s..-installers-onecore` klasöründeydi ve silinmişti (0x8007007E). İşlem yarıda kaldı (pending.xml,
PrimitivesCommitted). CBS imaja `Unserviceable=1` yazdı; sonraki her paket / yetenek adımı 0x800F0830 aldı ("Discard the
modified image"). Sonuçta 4 adım bekletti, imaj kaydedilmedi.
Karar:
- **Küçültme:** servis yığınının yükleyicileri (`microsoft-windows-s..-installers*`, ~9 MB) de kalır (`servicingStack`).
- **`servicingClosed(mountDir)`** (StoreShrink.h): WinSxS'te küçültmenin alacağı klasör 1000'den azsa `StoreShrunk`.
  SOFTWARE `…\Component Based Servicing\Unserviceable` ≠ 0 ise `Unserviceable`. ApplyJob bunu oturum açılmadan okur ve
  `ApplyOptions`'a koyar.
- **Applier:** kapalı imajda `usesServicing` adımları (özellik aç / kapat, paket ekle / kaldır, yetenek, sürüm, depo
  temizliği; Safe OS hariç, o WinRE'ye gider) DISM'e gitmeden `Unsupported` + 0x800F0830 ile "atlandı" olur.
  `unchangedByFailure` bunları ve DISM'in kendi 0x800F0830'unu bekletmez saymaz. Bileşen kaldırmada paketlere dokunulmaz
  (kilitleri de açılmaz); dosya, kayıt defteri, servis ve uygulama kısmı çalışır. Yalnız paketten oluşan bileşen atlanır.
  Küçültülmüş depoda ShrinkStore "yapılacak bir şey yok" der.
- Raporda nedeni yazılır (`apply.reason.servicingClosed`).
Kanıt: testler (StoreShrinkTests, ApplyTests). Kullanıcının imajı da onarıldı:
  - Eksik 5 WinSxS klasörü özgün WIM'den kondu.
  - `DISM /Cleanup-Image /RevertPendingActions` başarılı ("rollback … pending online operations").
  - `Unserviceable` SeBackup/SeRestore ile silindi.
  - `/Get-Packages` çalışıyor, `/CheckHealth` "No component store corruption detected".
Görülmeyen: onarılan imajın kurulup ilk açılışta geri almayı tamamlaması (VM'de denenmedi).

## D-114 — Büyük güncelleme: renkli ikonlar, saf siyah tema, kart düzeni, referans araç mantığıyla Güncellemeler / Sürücüler / Güç planı / Özellikler (2026-11-07)
Bağlam: Kullanıcının 17 maddelik listesi (UI kusurları, ikonlar, tema, Hakkında, Görüntü düğmeleri, sağ tık ikonları,
güncelleme bulma, sürücü sınıfları, güç planları, Özellikler ve Ayarlar referans araç gibi) + bileşen tanımlarını artırma.
Karar:
- **Bileşenler:** D-113'e 74 tanım daha (219): kalan büyük paylar kilitli çekirdek düğümler olarak (kabuk 1,3 GB, çekirdek,
  grafik, WinRT, servis yığını, önyükleme, OOBE, Windows Update, MSI, güvenlik duvarı, lisanslama, ses, ağ) ve yeni
  kaldırılabilirler (MDAC/Jet, MSXML, Vulkan, WARP, DirectShow, MPEG-2/WMV kodekleri, TAPI, MAPI, CDOSYS, ReFS, depolama
  yönetimi, sıfırlama, UserData/posta eşitleme, Store altyapısı, Entra aracısı, WebAuthn, TTD, WPR, WinSAT, yazım
  denetimi …); referans aracın imajında tanımların payı 8,9 GB. Depo adlarında "( )" ve iç boşluk geçer
  ("…-(mdac)-ado15-dll"): doğrulama buna göre.
- **İkonlar (Fluent renkli):** vurgu mürekkebiyle çizilen ikon kendi anlam rengini (klasör/dosya amber, kaldırma kırmızı,
  güvenlik/çalıştır yeşil, özellik/uygulama mor, donanım teal, imaj/bilgi mavi) ve kapalı şekillerde %20 dolgu alır
  (`IconTone`); devre dışı / vurgulu zemin / yüksek kontrastta tek renk kalır. Belirteçler: `icon.*`.
- **Tema:** "Yüksek kontrast" seçeneği kaldırıldı, yerine **Saf siyah** (`black` belirteç kümesi, #000000 zemin);
  Windows'un kendi yüksek kontrastı erişilebilirlik olarak yine kazanır; eski "hc" ayarı siyaha döner.
- **Düzen:** düğme metni ölçülmeden sabit genişlik alınca ortadan başlıyordu (sağa kayık) → çizimde ölçülür; şerit
  düğmelerinin hepsine ikon; kart sekmesi kartın kendi rengiyle birleşir (köşe dolgu + iç bükey yay); yan paneller
  yuvarlak kart; formlar bölüm kartları, 34 satır / 26 denetim, aralarında çizgi; metin sekmeleri hap biçiminde aralıklı;
  pencerenin en küçük boyutu çalışma alanını aşmaz (durum çubuğu görev çubuğunun altında kalmaz); Windows indir araç
  satırı dar pencerede ikinci satıra iner; Hakkında kartlı (kahraman kartı, vaat hapları, Bağlantılar / Sistem);
  Görüntü şeridine Kaydet · Çöz · Bağlama klasörü · ISO Oluştur, tabloda dar genişlikte Mimari/Dil sütunları çekilir;
  sürüm satırının sağ tık menüsünde ikonlar (`MenuPopup::setIcons`).
- **Güncellemeler:** referans araç listeyi kendi lisans sunucusundan alır (kullanılamaz). WinLove kaynağın kendisine sorar: FE3
  (`WuDiscovery`: GetCookie + SyncUpdates WU_OS / Setup360 DUScan / NETFX) imajın yapısına sunulan KB'leri verir, her KB
  Katalog'da numarasıyla aranır (dosya ve SHA-256 yine Katalog'dan). 26200 için 3 → 8 güncelleme; türünün yalnızca en
  yenisi önerili.
- **Sürücüler:** sınıf adı Windows'un kendi açıklaması (SetupDiGetClassDescription, bu bilgisayarın dili), ikon sınıf
  anahtarının IconPath / Icon'undan (Aygıt Yöneticisi'nin ikonları). "Bu bilgisayardan al" yalnızca takılı cihazların
  kullandığı oemN.inf'leri dışa aktarır (sürücü anahtarının InfPath'i), eski sürümler artık kopyalanmaz.
- **Güç planı (Kayıt Defteri ▾):** Nihai Performans / Yüksek performans / Dengeli / Güç tasarrufu (ilk açılışta powercfg),
  bu bilgisayarın planları (PowerEnumerate, powercfg /export → .pow → Kurulum Sonrası PowerPlan adımı), .pow ve .reg.
- **Özellikler:** Windows özellikleri referans araç gibi ağaç — üst bağlantı paket manifestlerinden (`detectUpdate` /
  ServerComponent Parent, `readFeatureParents`); diller her dil için bir grup ("Türkçe (Türkiye) - … - tr-TR"); geç kalan
  başarısız okuma yeni listeyi ezmez (render demosu bu yüzden bozuktu).
- **Ayarlar (Tweaks) içeriği:** referans aracın 405 ince ayarı onun kendi seçkisi ve metinleri — kopyalanmadı (D-113 ile aynı
  sınır). Bu turda yalnız tasarım (kartlar, sekmeler) değişti.
- Test: EditionTests'teki yükseltme testi gönderilen işleri motor iş parçacığında çalıştırıyordu (yaklaşık 8'de 1 çöküş):
  kuyruğa alınıp ana iş parçacığında çalıştırılıyor.
Ölçüm: birim testleri (Debug ve Release) geçti; `wlcli catalog 26200.8037 --dynamic` 8 güncelleme (47 sn, FE3 Setup360
yanıtı ~10 MB); render ile Görüntü, Hakkında, Uygulama ayarları (saf siyah), Ayarlar, Sürücüler, Özellikler doğrulandı.

## D-113 — Bileşenler referans aracın tanım modeliyle: bileşen deposundan alt dallar, gerçek boyutlar, bağımlılık ağacı (2026-11-07)
Bağlam: Kullanıcı: "Neden her kategori altına direk ekleniyor, neden alt kategoriler yok … boyutu kb değerine kadar
birebir aynı … yapıda mantık da aynı olsun. rea gücünü kullan." REA ile `reference_unpacked.exe` (çalışan süreçten alınmış
döküm) incelendi: referans aracın ~1.300 statik bileşeni programa gömülü `init` fonksiyonlarıdır — her biri bir boyut sabiti
(`movabs rax, 0x4055fb851eb851ec` = 87.93 → `[this+0x140]`; ekrandaki 87.93 / 300.00 / 195.00 / 345.00 / 12.81'in
hepsi kodda sabit, 200 bileşende doğrudan bulundu), CBS bileşen adı desenleri (".NET Framework Core":
`Microsoft-Windows-NetFx-Shared-*`, `Microsoft-Windows-GacInstaller`, `netfx-System*` …) ve alt bileşenlerin desenlerini
dışarıda bırakan istisnalar (`*-NetFx-Shared-WCF*`, `WPF-*`, `NetFx4-ASPNET*`). Adlar `Lang\<dil>.xml`'de (`S0703`
".NET Framework Core"). Canlı olan yalnızca desenlerin imajdaki bileşenlerle eşleşmesi; boyutlar ölçülmez (Uygula'da
ölçülen gerçek boyut farklı çıkar: Konum Hizmeti 0.9 → 3.2 MB). KB'si KB'sine aynı liste referans aracın kapalı verisini
kopyalamak demek: yapılmaz (WinLove GPL ve açık; kullanıcıya söylendi, "evet et" ile bu yol seçildi).
Karar (aynı yöntem, WinLove'un kendi tanımları, Windows'un kendi verisi):
- **Tanım = bileşen deposu desenleri** (`components.json` `store`): kimlik adı glob'u ya da `ad|PublicKeyToken|sürüm`
  glob'u (`*|b03f5f7f11d50a3a|*` = .NET derlemeleri). `ComponentStoreIndex` her `DerivedData\Components` anahtarının
  `identity` değerini okur (`.Resources` adı kendi adına katılır); `shareStore` her bileşeni **en özgül desene** verir
  (tam ad her glob'dan önce, sonra en uzun sabit metin) — referans aracın "üst, altlarının parçalarını dışarıda bırakır" kuralı
  desen yazmadan kendiliğinden çıkar.
- **Boyut ölçülür**: her bileşenin WinSxS klasörü (tüm sürümleri) bayt bayt; satırın boyutu kendi payı. 32 bit payı
  (x86 / wow64) olan her tanımın altında kendi **"- 32 bit"** satırı (referans araçtaki gibi), ayrı kaldırılabilir.
- **İç içelik = bağımlılık** (`parent`): alt satır üstüne dayanır. Üst kaldırılınca altındakiler de kuyruğa girer
  (tutulan/kilitli olanlar kalır); kaldırılmış bir alt yeniden işaretlenince dayandığı üstler de geri gelir.
  Bağımlılıklar Windows'un kendi ilişkilerinden: servis `DependOnService` (COM+ Olay Sistemi → SENS → COM+ Sistem
  Uygulaması), özellik `Parent` (.NET → ASP.NET / WCF / WPF), WOW64. `container`: kendi parçası olmayan düğüm (Uzak
  Masaüstü). `locked`: gösterilir, kaldırılamaz (WinINet/URLMon, Terminal Hizmetleri çekirdeği, WMI, Chakra) —
  geniş bir ailenin çekirdeği altlarından ayrılır, üst kaldırılınca o kalır.
- **145 tanım** (API 46, Ağ 19, Hizmetler 13, Donanım 12, Tanılama 11, Güvenlik 10, Depolama 7, Eğlence 7,
  Kabuk 6, Kuruluşlar 5, Yerelleştirme 4, Üretkenlik 3, Kurulum 2): adları Windows'un kendi Türkçe servis/özellik
  adları ya da WinLove'un; referans aracın adları, desenleri, sayıları kopyalanmadı. Servis → bileşen eşlemesi imajın
  WinSxS dosyalarından (servisin DLL'i hangi bileşenin yükü). Mevcut girdiler ağaca bağlandı (OneDrive → Bulut
  Dosyaları API'si, WMP ağ paylaşımı → Windows Media Player, biyometrik kayıt → Biyometrik Hizmet).
- **Kaldırma = referans aracın Varsayılan+ özel kaldırması** (`ComponentRecipe::storeNames/storeBits/services`,
  `removeStoreCopies`): bileşenin WinSxS yükündeki her dosyanın imaj içinde WinSxS dışındaki her sabit bağı silinir
  (System32, SysWOW64, Program Files …), listelenen servis anahtarları (her ControlSet) silinir; depo ve kaydı
  dokunulmaz → güncelleme desteği sürer, `/ScanHealth` temiz kalır; bir toplu güncelleştirme ya da `sfc /scannow`
  dosyaları geri getirebilir (ipucu notunda yazar). Tarif imajın adlarını taşır (`afterUpdates`: aynı çalıştırmadaki
  güncelleme önce); hazır ayar açılınca adlar bu imajınkilerle yenilenir (D-076).
- **Koruma**: bir hazır ayar dosyası çekirdeğe uzanamaz — `isProtectedStoreName` (çekirdek, ntdll, win32k, user32,
  combase, RPC, csrss/smss/winlogon/LSA, shell32/explorer, servis yığını/CBS/TrustedInstaller, NTFS, PnP, crypt,
  Terminal Hizmetleri oturum yöneticisi, WinINet/URLMon/iertutil, WMI çekirdeği, apphelp/uyumluluk veritabanları) ve
  `isProtectedService` (RpcSs, DcomLaunch, LSM, Power, PlugPlay, SamSs, EventLog, ProfSvc, CryptSvc, Dhcp, Dnscache,
  BFE, mpssvc, gpsvc, Winmgmt, AppXSvc …) doğrulamada ve motorda reddedilir. Önerilmez (kırmızı) satırlar D-110'un
  "Önerilen" koruması açıkken kilitlidir.
- `wlcli store-share <mount> <components.json>` (tanım başına pay, JSON satırları), render
  `--demo-store-share=<dosya>` ve `--demo-reveal=<id>`; `tools\lab_store_copies.ps1`.
Ölçüm (referans aracın bağladığı 25H2 Pro, salt okuma): 36.636 depo bileşeni, indeks 11 sn (debug); 142 tanım imajda,
108'inin 32 bit satırı var, en derin dal 4; tanımların payı 4.070 MB. C++ eşleştirmesi Python prototipiyle 143/143 bayt
bayt aynı. Örnek: .NET Framework 4.8 352,53 MB (- 32 bit 208,80) → ASP.NET 21,94 · WCF 44,17 · Windows Forms 26,18 ·
WIF 7,07 · WPF 61,65 (- 32 bit 26,66). `tools\lab_store_copies.ps1` (lab imajının kopyası): 23/23 PASS — Karakter Eşlemi
(her mimari), WMIC yalnız 32 bit, WebClient + servis anahtarı kaldırıldı; WinSxS yükü yerinde; çekirdek bileşen
(kernel32) ve çekirdek servis (RpcSs) reddedildi; DISM `/ScanHealth`: "No component store corruption detected".

## D-112 — Bileşenler'e imajın kendi satırları: referans aracın yöntemiyle sürücüler, diller, klavyeler, yazı tipleri, sistem uygulamaları (2026-11-07)
Bağlam: Kullanıcı iki uygulamayı yan yana koydu: "kategoriler hemen hemen aynı olsa da içerikler adları alt
kategorilendirme boyut her şey farklı … aynı yöntem aynı mantık aynı liste buldurabilmeliyiz". Referans aracın listesi iki
kaynaktan gelir (`research\README.md` §6.6): ~1.300 satır referans aracın programa gömülü kendi bileşen tanımları (adları,
iç içeliği, şablonları, gömülü boyut tahminleri — referans aracın verisi; WinLove'a kopyalanmaz, kullanıcıya söylendi) ve
~1.175 satır imajın kendisinden okunanlar. İkinci kısım aynı yerden aynı yöntemle okunur.
Karar:
- Motor `core/image/ImageInventory` (offreg ile, imaja hiçbir şey yüklemeden):
  - **Sürücüler:** DRIVERS + SYSTEM `DriverDatabase\DriverPackages` (paket → INF, sınıf GUID'i), INF'ten sağlayıcı,
    DriverVer ve imaj mimarisinin `[Models]` bölümlerindeki cihaz adları (`DriverInf::devicesFor`); sınıf adı imajın
    `Control\Class\{GUID}\ClassDesc`'inden — MUI ise bu bilgisayarın dosyalarıyla çözülür, `@x.inf,%…%;Yedek` ise
    referans araç gibi `;`'den sonraki metin ("Display adapters"). Satır adı referans aracınki: "Sağlayıcı  |  dosya.inf".
  - **Diller:** `System32\<etiket>` klasörleri (+ SysWOW64, Common Files\microsoft shared\ink), referans aracın gruplamasıyla:
    bir dil bir satır (sr-*, es-*, nb/nn, qps-* birleşir), İngilizce / Fransızca / Portekizce / Çince değişkenleri ayrı.
  - **Klavye düzenleri:** SYSTEM `Keyboard Layouts\<KLID>` (Layout Display Name MUI / Layout Text, Layout File).
  - **Yazı tipleri:** SOFTWARE `Windows NT\CurrentVersion\Fonts` değerleri, aileye göre (`fontFamily`).
  - **Sistem uygulamaları:** SOFTWARE `AppxAllUserStore\InboxApplications` (SystemApps ve WindowsApps'tekiler).
- Kaldırma referans aracın gözlenen yöntemleriyle (README §8), `RemoveComponent` tarifi olarak: dil → yerel ayar klasörleri;
  klavye → KBD*.DLL (başka düzen kullanmıyorsa) + `Keyboard Layouts\<KLID>`; yazı tipi → `Fonts\<dosya>` + Fonts
  değeri; sistem uygulaması → klasörü + `Deprovisioned\<aile>` + `InboxApplications\<tam ad>`; sürücü → yeni
  `ComponentRecipe::driverPackages` ile tek paketin derin kaldırması (D-060'ın yolu: DriverStore, DRIVERS, WinSxS,
  INF, sabit bağlar; sınıf sınırı aynen: yalnız eski sınıflar).
- Kilitler (sabit, `lockReason`; ipucunda yazar): sınıf INF'leri ("doğrudan kaldırma desteklenmiyor"), eski olmayan
  sürücü sınıfları ("Gerekli sürücü sınıfı" — kullanıcının kuralı: ağ, depolama, USB, HID, ekran … asla), imajın
  varsayılan dili ve en-US, ABD ve varsayılan dilin klavyeleri, temel yazı tipleri (Marlett, Segoe UI ailesi, simge
  yazı tipleri), izin listesi dışındaki sistem uygulamaları (kabuk, oturum açma, kurulum, kilit ekranı …).
- Ağaç referans aracınki: Sürücüler → sınıf → "Sağlayıcı | inf" → cihazlar; sınıf INF'leri ve ağ hizmeti / protokolü /
  istemcisi INF'leri Donanım desteği → Tek tek cihazlar [Gelişmiş] → sınıf; Yerelleştirme → Dil / Klavye düzenleri /
  Yazı Tipleri. Sayfa genel ağaç oldu (kategori → düğüm → bileşen → alt satır), düğümün dairesi altındakileri topluca
  işaretler (`ComponentController::setRemoved`, tek kuyruk düzenlemesi); düğüm durumları satırlar kurulurken önbelleğe alınır.
- `wlcli inventory <mount> [--json]`, `wlcli inventory-recipe <mount> <tür:ad>`, `tools\lab_inventory.ps1`;
  render `--demo-inventory=<json>` (gerçek bir imajın satırları).
Ölçüm (referans aracın kendi bağladığı 25H2 Pro imajı, salt okuma; referans aracın döküm listesiyle karşılaştırma):
klavye düzenleri 218 / 218 adlarıyla aynı; sürücü INF'leri 715 / 715 aynı sınıf altında aynı adla; sürücü sınıfları
116 / 116 aynı; cihaz satırları 7.429 / 7.354 (47 INF'te küçük fark); diller 84 / 86 (gruplama aynı kural, adlar
Windows'unki — referans aracın Türkçe dil adları kendi çevirisi; IME grubu yok); yazı tipleri 67 / 88 (referans aracın yazı tipi
gruplaması kendi tanımlarından: ne kayıt adına ne dosyanın aile adına uyuyor); sistem uygulamaları 48 / 26 (referans aracın
22'sini kendi statik bileşenlerinin içinde gösterir). "Tek tek cihazlar [Gelişmiş]"e referans aracın koyduğu 165 INF'in
103 sınıf INF'i + ağ hizmet/protokol/istemci INF'leri kuralla bulunur; kalan 38'i referans aracın tanımlarıdır.
Kanıt (`tools\lab_inventory.ps1`, lab imajının kopyası, 26200.8037 Pro, yönetici): okuma 715 sürücü / 7.425 cihaz, 84 dil,
217 klavye, 67 yazı tipi, 48 sistem uygulaması; cy-GB, Gotik klavye (KBDGTHC + 000C0C00, 00000409 kaldı), Gabriola,
PeopleExperienceHost (klasör, Deprovisioned, InboxApplications) ve mdm5674a.inf (FileRepository; diğer modemler kaldı)
kaldırıldı, bir ağ sürücüsü reddedildi, DISM sonra paketleri listeledi — 27 / 27 PASS, imaj atılarak çıkarıldı.
Doğrulanmayan: bu kaldırmalardan sonra kurulan Windows'un VM'de açılışı.

## D-111 — Özellikler referans araç düzeninde; formlar referans araç yoğunluğunda (2026-11-07)
Bağlam: D-109'un devamı (referans araç birebir). Referans aracın Özellikler sayfası (`research\README.md` §7.6): iki sekme
"Talep Üzerine Özellikler 57/489" ve "İsteğe Bağlı Özellikler 13/134"; yetenekler kategori ağacında (Diller · Yazı
Tipleri · Sürücüler · RSAT · Diğer, kimliğin önekinden); sütunlar Açıklama · Etkin (yeşil tik) · Yüklendi (kurulu
değilse boyutu); işaret kalan / açık olanı gösterir. Ayarlar ve Katılımsız gibi formlar referans araçta 23,5'lik özellik
listesidir (ad | değer | açıklama).
Karar:
- `FeaturesPage` yeniden yazıldı: `TabBar` iki sekme (sayaçlar `targetOn` ile, kuyruk değişince güncellenir,
  `TabBar::setTabs`), "Tümü ▾" (Tümü · Açık · Kapalı · Değişenler) + arama, yetenekler `categoryOf()` ile
  (`Language.Fonts.*` → Yazı Tipleri, `Language.*` → Diller, `Rsat.*` → RSAT, `Microsoft.Windows.*Wifi/Ethernet*`
  → Sürücüler, gerisi Diğer), kategori satırının dairesi kategoriyi topluca açar/kapatır, öğeler ada göre, ayrıntı
  ipucunda. Şerit: Tümünü genişlet · daralt | Sıfırla. Ağaç işareti ortak `paintTreeCheck` (PageBits.h; Bileşenler de).
- `FormView` satırı 23,5 (denetim 21,5), bölüm satırı 30, satır çizgisi yok, değişen satırın solunda 2px vurgu
  çubuğu ("Değiştirilen satırlar: Kenar çubuğu"); Ayarlar değer kutusu 180. Sayfa sabitleri de küçüldü: Hakkında,
  Diller 24; ISO 26; Uygula adımları 24; Başlat menüsü, Simgeler 28; komut paleti 34 / 36 / 24.
Doğrulanmayan: Özellikler sayfasının render'ı — `--demo-features` bu değişiklikten önce de "okunamadı" gösteriyordu
(ön okuma demo verisinin üstüne yazıyor); sayfa birim testleri ve derleme ile denendi, gerçek imajda bakılmadı.
→ FormView ve sayfa ölçüleri D-136, Özellikler'in sütun başlıklarının yeri (süzgeç satırından tablonun başlık satırına)
D-139 ile değişti.

## D-110 — Bileşenler referans araç mantığıyla: kategori ağacı, şablon renkleri, kaldırma düzeyi (2026-11-07)
Bağlam: Kullanıcı sayfaların referans araçla "birebir aynı mantıkta" çalışmasını istedi (D-109). Referans aracın Bileşenler
davranışı `research\README.md` §6'da ölçüldü: 14 sabit kategori, işaret = **tutulur** (işareti kaldırılan
kaldırılır), satır başında şablon rengi (Gizlilik ⊂ Genel ⊂ Hafif, Önerilmez seçilemez), "Önerilen" uyumluluk
koruması kırmızıları kilitler, Kaldırma modu (Varsayılan+ / Derin / Maksimum) listeyi değil korumaları ve depoyu
değiştirir, boyut sütunu MB + çubuk, açıklama ipucunda, filtre "Tümü ▾".
Karar:
- Kataloglara (`appx.json`, `components.json`) her öğe için `category` (referans aracın 14 kategorisi + `uncategorized`)
  ve `tier` (`privacy` / `general` / `light` / `notRecommended` / yok) eklendi; kategori listesi
  `components.json` → `categories`. Atamalar WinLove'un kendi kararı (referans aracın bileşen tanımları kopyalanmadı):
  risk düşük → Genel, orta → Hafif, yüksek → Önerilmez; telemetri, tüketici eklentileri (Haberler, Hava, Bing,
  Copilot, Solitaire, Clipchamp…) ve nadir kurumsal özellikler (App-V, UE-V, BranchCache…) → Gizlilik; derin sürücü
  kaldırma ve depo temizliği şablonsuz.
- `ComponentController::groups()` artık kategori sırasıyla gruplar, öğeler ada göre; depo temizliği ve WinSxS
  küçültme ağaçta değil, **Kaldırma modu**nda: Varsayılan+ = ikisi de yok, Derin = `CleanupImage` (ResetBase),
  Maksimum = temizlik + `ShrinkStore` (+ otomatik güncelleme kapalı). `level()` kuyruktan okunur. Maksimum,
  "Önerilen" ve "Windows Update" korumalarını kapatır; diğer düzeyler açar.
- **Şablon ▾** (Gizlilik / Genel / Hafif): şablonun aldığı, kilitsiz, kuyrukta olmayan öğeleri tek kuyruk
  düzenlemesiyle ekler (sıfırlamaz; "Geri al" toast'u). `templateTakes()`.
- Uyumluluk'a **"Önerilen"** koruması (varsayılan açık): kırmızı öğeleri (`edge-webview`, `edge-update`, `winre`,
  `defender-full`, Store, App Installer, Windows Güvenliği, çalışma zamanları) kilitler.
- Sayfa: tek satır 23,5; kategori satırı (ok + daire işaret + ad), bileşen 17 içeride (daire işaretin solunda
  şablon renginde hilal), Önerilmez adlar soluk, kilitli satırlar gri; boyut "87.93" log ölçekli çubuk
  (~20 DIP / log10(1+10·MB)); işaret **tutulanı** gösterir (kuyruk tersi). Filtre: Tümü · Tutuluyor ·
  Kaldırılıyor · Uygulamalar (Kullanıcı) · Bileşenler (Sistem) · Şablon (Gizlilik / Genel / Hafif). Açıklama,
  boyut, şablon ve kilit nedeni satırın ipucunda (`TableView::tooltipFor`); sağ denetçi paneli kalktı.
  Şerit: Mod · Varsayılan+ | Uyumluluk | Şablon ▾ | Geri al · Yinele | Tümünü genişlet · daralt | Sıfırla.
Açık (referans araçta olup henüz yok): imajdan okunan sürücü / dil / klavye / yazı tipi satırları (motor + lab kanıtı
gerekir, kural 6), bağımlılık iç içeliği, ilk girişte Kaldırma modu penceresi.
→ Satır ölçüsü D-136, şablon ve boyut çubuğu renkleri D-137, sütun başlıklarının yeri ve "sağ denetçi paneli kalktı"
D-139 ile değişti (başlıklar tablonun kendi satırında; seçili satır için ortak ayrıntı kartı).

## D-109 — Arayüz v3: referans aracın görünümü ve gezinmesi birebir (2026-11-07; D-107/D-108'in ölçü, renk ve iskeletini geçersiz kılar)
Bağlam: D-108'den sonra kullanıcı: "Hala çok büyük UI … herşey çok iri butonlar … dark ve light temada çok daha
net renk tonları … bak referans araç ne kadar güzel tasarımı var … her menüye bak aynı birebir bu tasarımda renklerde olsun".
Ardından: "bütün sayfalarımızı menülerimizi referans araçla birebir aynı mantıkta çalışacak şekilde değiştir"
(kanıtlar `C:\Users\shades\Desktop\research`: README v2 + `docs/` katalogları + `analysis/evidence`).
Ölçüm: referans araç 2026.10'un penceresi `PrintWindow` ile (yalnız o pencere) %175'te her sayfada ve dört görsel stilin
ikisinde (Koyu = Windows koyu temada "Otomatik", Açık) yakalandı; görüntüler `research\analysis\evidence\screens\design\`
(repoya girmez). Renkler piksel örneklemeyle, ölçüler renk geçişleri ve büyük harf yüksekliğinden (15 px ⇒ 12 DIP font).
Karar:
- **Tasarımın kaynağı referans aracın ölçülen görünümü**; `design/tokens.json` 3.0.0 bu değerleri taşır. `design/WinLoveUI/`
  artık ölçü/renk kaynağı değil (paket salt okunur kalır; ikonlar `WinLove-UI-Handoff/`'tan).
- **Renkler** — koyu: sayfa `#202020`, şerit / gezinti kartı / kart sekmesi `#262626` (`bg.chrome`), kart ve durum
  çubuğu `#2B2B2B`, seçili `#3A3A3A`/`#3E3E3E`, giriş `#333333`; yazı `#FFFFFF` / `#B8B8B8` / `#858585`; vurgu Windows'un
  koyu vurgusu `#4CC2FF` (üstünde siyah yazı). Açık: sayfa `#EEEEEE`, başlık çubuğu `#E6E6E6`, şerit `#F5F5F5`, kart
  `#FBFBFB`, seçili `#E3E3E3`; yazı `#1B1B1B` / `#525252` / `#858585`; vurgu `#0067C0`, bağlantı `#0058CD`.
  Bileşen şablon renkleri (`tier.*`): Gizlilik yeşil `#39B54A`, Genel mavi `#3C7BA7` / `#0058CD`, Hafif sarı `#F4D534`,
  Önerilmez kırmızı `#F54137`; boyut çubuğu `sizeBar` `#2E3C46` / `#C3D7F0`.
  Referans aracın gri yazısı ~3:1 okunur: üçüncül yazının kontrast kuralı 4,5'ten **3,0'a** indi (birincil/ikincil 4,5 kalır).
- **Yazı:** her yerde Segoe UI 12 (referans araç 9 pt); kalınlık yalnız başlıkta 600; büyük harf başlık yok.
- **Ölçüler (DIP):** başlık çubuğu 34, şerit 50, ipucu satırı 34, durum çubuğu 22; tablo satırı 23,5, kontrol 24,
  gezinti öğesi 28 (grup başlığı 25), gezinti kartı 200 geniş, kart sekmesi 26, onay kutusu 13, radius 2/4/6.
- **İskelet (referans araç):** başlık çubuğu = ≡ uygulama menüsü (Uygulama ayarları · Windows indir · Presetler · Loglar ·
  Hakkında · Çıkış) + Görüntü · Ön Ayar · Araçlar sekmeleri (altı çizili) + ortada pencere başlığı; altında **şerit**
  (sayfanın eylemleri soldan sağa, simge vurgu renginde: `ButtonKind::Ribbon`), ortalı **ipucu satırı** (sayfa
  açıklaması), solda **gezinti kartı** (`NavPanel`), sağda sekme başlıklı **sayfa kartı** (`PageView`/`CardTabs`),
  gerekiyorsa sağda yan kart (denetçi), en altta durum çubuğu (solda iş/bağlama, sağda imajın "Windows 11 Pro 25H2 x64 -
  10.0.26100.x" bilgisi). İmaj başlığı, sekme şeridi ve "Değişiklikler" paneli kalktı (referans araçta yok; sayılar
  gezintide, değişiklikler Uygula sayfasında). Komut paleti kutusu kalktı, Ctrl K duruyor. Ctrl B gezintiyi simgelere katlar.
- **Gezinti referans aracınki:** Başlat › Görüntü · Entegre et › Güncellemeler, Sürücüler, Kayıt Defteri, Kurulum Sonrası ·
  Kaldır › Bileşenler · Yapılandır › Özellikler, Ayarlar, Zamanlanmış Görevler, Katılımsız · Bitir › Uygula.
  WinLove'un referans araçta öğesi olmayan sayfaları işi olan öğenin sekmesidir: Güncellemeler = Güncellemeler · Diller ·
  Uygulamalar (referans aracın paket listesi dil ve uygulama paketlerini de alır), Kayıt Defteri · Hosts, Kurulum Sonrası ·
  Programlar · Dosyalar, Ayarlar · Servisler · Başlat menüsü · Kişiselleştirme · Simgeler (referans aracın Ayarlar'ında
  Hizmetler ve Görseller sekmeleri var), Uygula · ISO Oluştur.
- Tablolarda satır arası çizgi yok; sürüm satırı tek satır ("Windows 11 Pro   Professional").
Sonuç: Kabuk ve ortak widget'lar referans araç görünümünde; 387 test geçiyor (tasarıma bağlı beklentiler D-109 değerleriyle).
Sayfaların iç mantığının referans araca uydurulması (Bileşenler ağacı ve şablonları başta) ayrı adımlar: STATUS.md.
→ Yazı, yoğunluk, köşe ve ölçüler D-136 (2026-11-10), renkler ve vurgu D-137 (2026-11-11) ile değişti; iskelet ve
gezinti geçerli.

## D-108 — Arayüz v2'nin sıkılaştırılması: daha küçük ölçek, tek satır sekme (2026-11-07)
Bağlam: D-107'den sonra kullanıcı: "UI çok iri ve her sayfada düzen ve yerleşim baya kötü çok karmaşık bir arayüz var.
her sayfayı ekranı tek tek incele düzelt". İrilik paketin ölçülerinden (14px gövde, 36–44 kontrol, 28px başlık);
karmaşa iskeletten: 88px imaj başlığı, iki satır / 21 sekme (hepsinde "0"), açıklama + eylem satırı, sayfa araç
çubuğu ve çoğu sayfada boş duran sağ panel üst üste biniyordu.
Karar:
- **Ölçek ~%85** (`design/tokens.json` 2.1.0): gövde 13, Small/Caption 12, Title 16, Display 20; kontrol 30 (34/36),
  tablo satırı 34, sürüm 44, liste 46, form 44; başlık çubuğu 40, durum çubuğu 26; radius 4/6/7/8/10; içerik boşluğu 24.
  Paketin renkleri, biçimleri ve yerleşim mantığı aynı.
- **Sekmeler tek satır**: ilişkili sayfalar bir sekmede, sayfa başlık satırının solunda segmentli seçimle —
  Güncellemeler · Diller, Uygulamalar · Programlar, Kayıt Defteri · Hosts, Kurulum Sonrası · Dosyalar, Servisler ·
  Görevler, **Görünüm** = Başlat menüsü · Kişiselleştirme · Simgeler; Katılımsız Kurulum sekmede "Katılımsız".
  21 sekme 14'e indi (1440'ta tek satır; 1100'de BİTİR alta kayar). Sayaç yalnız 0'dan büyükse görünür; sekmenin
  sayacı içindeki sayfaların toplamı, Uygula'nınki kuyruğun tamamı.
- **İmaj başlığı** 64px (ad 20) ve yalnız Görüntü menüsünde, bir kaynak açıkken; Ön Ayar menüsünde sekme şeridi yok.
- **"Değişiklikler" paneli** yalnız paketin gösterdiği yerlerde: Bileşenler ve Ayarlar. Uygula'ya diğer sayfalardan
  BİTİR › Uygula sekmesi (sayaçlı), Ctrl+Enter ve komut paleti.
- Sayfa düzeltmeleri: Görüntü'de başlıktaki Bağla kaldırıldı (WIM panelinde var), ESD → WIM yalnız ESD/solid
  kaynakta görünür; Kurulum Sonrası'nın yedi "ekle" düğmesi tek **Adım ekle ▾** menüsü (`Shell::addMenuAction`);
  Kişiselleştirme formu yazı tipi listesine göre yükseklik alır; Başlat menüsünde düğmeler önizleme kartıyla hizalı;
  Windows indir'in boş paneli yönlendirme metni gösterir; Güncellemeler tablosunun sütunları; mono yazı (Cascadia
  Mono) değişken eksenle 350 ağırlıkta (400'de arayüz yazısından ağır duruyordu).
Sonuç: 900 yükseklikte içerik 162. pikselden başlıyor (D-107'de 266); tüm sayfalar render ile tek tek denetlendi,
387 test geçiyor.

## D-107 — Arayüz v2: WinLoveUI tasarım paketi (2026-11-07)
Bağlam: Kullanıcı hazır bir arayüz paketi verdi (`C:\Users\shades\Downloads\WinLoveUI`: `Theme.h`, `Painter.{h,cpp}`,
`docs/LAYOUT.md`, demo) ve arayüzün tamamen bununla değişmesini, her şeyin aynı çalışmasını istedi: "Bütün özellikleri
bu arayüz içermeyebilir ama tasarım stili mantığını öğreneceğin için sen gerekli eklemeleri yaparsın." Paket üç ekranı
(Görüntü, Bileşenler, Ayarlar) ve ortak iskeleti tarif ediyor; WinLove'un 27 sayfası var.
Karar:
- **Tasarımın tek kaynağı artık `design/`** (kural 3 güncellendi): paketin kendisi `design/WinLoveUI/` altında olduğu gibi
  (salt okunur), token'ları `design/tokens.json` (paketin renkleri ve ölçüleri; paketin adı olmayanlar — pressed,
  overlay, disabled, durum zeminleri, yüksek karşıtlık — oradan türetildi, `meta.notes`). `tools/gen_tokens.py` bunu
  okur. `WinLove-UI-Handoff/` ikonlar, marka ve karşılama betiğinin renkleri için duruyor; ekranları artık ölçüt değil.
- **Görünüm** (kural 4 güncellendi): Zinc nötrleri (açık `#F7F7F8` / koyu `#111113`, panel beyaz / `#18181B`), tek vurgu
  `#2563EB` (iki temada aynı dolgu, üstünde beyaz metin), vurgu *metni* ayrı token (`accent.text`: `#1D4ED8` / `#7AA7FF`),
  `bg.hover` yarı saydam, `caption.close`. Yazı Segoe UI Variable (değişken fontun `wght`/`opsz` eksenleriyle — WWS
  ailesi 500'ü 600'e yuvarlıyordu), Cascadia Mono; gövde 14, Display 28, Title 18, Panel 15, Small 13, Caption 12,5,
  GroupCap 11 (+0,06em), Section 12 (+0,04em). Kontrol 36, büyük 40/44, satır 40 (tablo), 52 (sürüm), 56 (liste), form
  56; radius 5 / 7 / 9 / 10 (düğme) / 12 (kart). Renkli ikon yok; renk yalnız durum için (vurgu seçim/bağlı, uyarı
  dikkat, risk riskli, ok doğrulandı). Inter / Geist ve diğer vurgu renkleri (Bakır, Deniz, Nar, Zeytin; "Gök" maviye
  karıştığı için çıktı) ayarlarda seçenek olarak kaldı; `settings.json`'a `"design": 2` yazılır, eskisi bir kez yeni
  varsayılanlara (Mavi + Segoe UI Variable) geçer.
- **İskelet** (paketin "Ortak iskelet"i): başlık çubuğu 44 (logo, **Görüntü · Ön Ayar · Araçlar** menüleri, ortada
  Ctrl K kutusu, güneş/ay tema düğmesi, pencere düğmeleri) · **imaj başlığı** (sürüm adı 28 + "Bağlı"/"Bağlanıyor %"
  rozeti; sürüm · derleme, mimari, dil, kaynak yolu, "Değiştir"; sağda boyut önce → sonra, kuyruğun `sizeDelta`
  toplamı, 180×4 çubuk) · **gruplu sekmeler** · sayfa | sağ panel · durum çubuğu 30 (nokta + "Bağlı · sürüm · klasör",
  çalışan iş, sağda DISM sürümü ve çalışma sürücüsünün boş alanı). NavRail kaldırıldı.
- **27 sayfa, 10 sekmelik tasarım**: menüler çalışma alanıdır. *Görüntü* imajı düzenleyen her sayfayı paketin
  gruplarında tutar — BAŞLAT: Görüntü (kaynak yoksa Kaynak sayfası, varsa İmajlar) · YÜKLE: Güncellemeler, Sürücüler,
  Diller, Uygulamalar, Programlar, Kayıt Defteri, Dosyalar, Kurulum Sonrası · KALDIR: Bileşenler · YAPILANDIR:
  Özellikler, Ayarlar, Servisler, Görevler, Başlat menüsü, Kişiselleştirme, Simgeler, Hosts, Katılımsız Kurulum ·
  BİTİR: Uygula, ISO. *Ön Ayar*: Presetler. *Araçlar*: Windows indir, Loglar · Uygulama ayarları, Hakkında. Sekmeler
  sığmayınca grup bütün olarak alt satıra akar (gizli sekme yok; 1440'ta iki satır). Ctrl+1…9 çalışma alanının sekmeleri.
  Ctrl+B artık imaj başlığını gizler (yer açar).
- **"Değişiklikler" paneli** (paketin sağ paneli, 320): düzenleme sekmelerinde, sayfanın kendi denetçisi yokken —
  kuyruktaki işlem sayısı, sayfa başına sayaçlar (tıklayınca o sekme), bağlı klasör, tam genişlik **Uygula** (44);
  uygularken aşama + % + çubuk + Durdur. Eski durum çubuğunun "Uygula · n" düğmesinin ve iş akışı adımlarının yerini
  aldı. İkinci bölmesi olan sayfalarda (Katılımsız XML önizlemesi, Başlat ızgarası, Simgeler) ve 1180'den dar
  pencerede gösterilmez.
- **Sayfa çerçevesi**: büyük sayfa başlığı yok (sekme başlıktır); bir satırda açıklama (Small, üçüncül) + sağda eylem
  düğmeleri; canlı başlık (Uygula) başlığı açıklamanın önüne koyar. Gövde, uzunsa kaydırılan bir görünüm alanında
  (`ScrollableBody`; ilk kullanan ISO formu).
- **Paketin ekranları birebir**: Görüntü (kart içinde 34/52 sürüm tablosu, dizin · ad + Edition ID · mimari · derleme ·
  dil · boyut · "● Bağlı"; 340'lık WIM bilgisi paneli, altta tam genişlik Bağla), Bileşenler (arama + **Tümü /
  Seçilenler / Dikkat gerektiren** hapları, başlıksız 56'lık liste, kategori başlıkları büyük harf + "3 seçili", risk:
  güvenli öğede hiçbir şey, "● Dikkat" / "● Riskli"), Ayarlar (form satırı 56, üstte çizgi, değişende adın yanında 6px
  vurgu noktası). Risk filtresi Düşük/Orta/Yüksek açılır listesi yerine "Dikkat gerektiren" hapı (orta + yüksek);
  risk adları Güvenli / Dikkat / Riskli oldu.
- Widget'lar paketin `Painter`'ına göre yeniden çizildi (Button + yeni Ghost, Checkbox 18, Switch 40×22, segmentli
  seçim = `RadioGroup`, yeni `PillGroup`, SearchBox, Kbd tek kutu, TabBar metin sekmesi, TableView satır/başlık
  yüksekliği örnek başına, Dropdown, Dialog, Toast, InfoBar kart, EmptyState, FormView, LogConsole kart, ScrollBar).
Sonuç: Bütün işlevler aynı yerden (sekmeler, eylem düğmeleri, kısayollar, komut paleti) çalışır; 387 birim testi
geçiyor (tasarıma bağlı 9 beklenti güncellendi: renkler, ölçüler, palet konumu, sürüm tablosu satırı). İmajla ilgili
hiçbir motor kodu değişmedi. Bilinen: 900 yüksekliğinde sekmeler iki satır olduğu için içerik alanı eskisinden kısa;
sayfa içi tablolar kaydırır, ISO formu sayfayla kayar. Eski ekran görüntüleri (`docs/screenshots`) eski tasarımı gösterir.

## D-106 — Uygula: kaydetmeden önce dur (elle düzenleme için) (2026-10-10)
Bağlam: Forumda bir kullanıcı ("Diğer sürümlere de uygula"yı kullanan) istedi: her şey uygulandıktan sonra imaj
kaydedilip çözülmeden dursun; bağlı klasörde elle temizlik/ekleme yapıp öyle çözsün (yoksa işi bitince yeniden bağlıyor).
Kullanıcı: "bu özelliği ekleyelim".
Karar:
- Motor: `ApplyJobCallbacks::beforeCommit` — adımlar bitti, hiçbiri düşmedi (tutulan imaj yok), DISM oturumu kapalı;
  motor iş parçacığında çağrılır ve kullanıcı bırakana kadar döner: true kaydeder, false imajı kaydetmeden bağlı bırakır
  (`ApplyJobResult::kept`). Diğer sürümlerde (`applyToEdition`) her sürüm kendi kaydından önce durur; orada false o
  sürümü atar (klasör sıradaki için boşalmalı). `wlcli apply … --pause` stdin'den bir satır bekler ("keep": bağlı bırak).
- Uygulama: `AppSettings::pauseBeforeSave` (settings.json `pauseBeforeSave`), Uygula özetinde "Kaydetmeden önce ☐ Dur:
  bağlı klasörü elle düzenleyeceğim". `ApplyRun::Stage::Paused` → `ApplyPage::Mode::Paused`: Running görünümü + üstte
  klasör yolunu söyleyen InfoBar; başlıkta "Klasörü aç" (`exploreMount`), "Durdur" (`cancel`: kaydetmeden bırak, kalan
  sürümleri atla), "Kaydet ve devam et" (`resume`). Bekleme `ApplyController::Gate` (mutex + condvar, her çalıştırmaya
  yeni); denetleyici yok edilirken (pencere kapanışı) kapı "kaydetme" ile açılır, motor kilitlenmez, imaj bağlı kalır.
- Explorer pencereleri kaydı engellemez (`unmountSafely` onları klasörden çıkarır); dosya tutan başka bir program
  kaydı düşürürse mevcut "kaydedilemedi, imaj bağlı" yolu çalışır.
→ D-144 (2026-11-11): "Bitince ISO oluştur" açıkken duraklatmada **Kaydet ve devam et** imajı kaydeder ve otomatik ISO
başlar; **Durdur** → ISO yok (nedeni kartta). Duraklatma seçiliyse ISO'nun hiç yapılmaması gerekip gerekmediği sahibine
soru (STATUS).

## D-105 — Simgeler: imaja giren her simge, değiştirdiği grubun boyutlarına tamamlanır (2026-10-10)
Bağlam: Forumda bir kullanıcı uyardı: tek boyutlu bir simge eklenirse Windows diğer boyutları kendisi büyütüp küçültür.
Kodda durum: resimden (PNG/JPG…) gelen simge zaten Microsoft'un setine çevriliyordu (256 PNG + 64/48/40/32/24/20/16,
32 bit) ama **.ico olduğu gibi** giriyordu (tek boyutluysa grupta tek boyut), yönlendirme modu .ico'yu olduğu gibi
kopyalıyordu ve grubun kendine özgü boyutlarına (imageres'te 75 grup: 96, 60, 36 px…) hiç bakılmıyordu. Ölçüm: bu PC'nin
imageres.dll.mun'unda 369 grubun 294'ü tam olarak `kIconSizes` setini taşıyor.
Karar:
- `completeIconSizes(images, sizes)` (core/image/icons/IconSource): istenen her boyut, kaynakta yoksa en iyi görüntüden
  üretilir — en derin renkli görüntüler arasından o boyuta eşit/büyük olanların en küçüğü (küçültmek detayı korur), yoksa
  en büyüğü. Windows'un saklayışıyla: 256 ve üstü PNG, altı 32 bit DIB. Kaynakta olan görüntüler bayt bayt aynen kalır.
  Çözme: tek görüntü `.ico` olarak sarılıp WIC'in ICO çözücüsüne verilir (PNG, 32 bit, paletli + maske hepsini okur).
- **Yama modu:** `fitIconReplacements(pe, …)` her değişikliği DEĞİŞTİRDİĞİ GRUBUN boyutlarına tamamlar (orijinal grup
  96 px taşıyorsa yenisi de taşır); `patchImageIcons`, `wlcli icon-patch` ve `icon-pack` bunu kullanır.
  `patchIconBytes` görüntüleri verildiği gibi yazan alt katman olarak kaldı (testler tam kontrol ister).
- **Yönlendirme modu:** Applier'da hedefi `.ico` olan `CopyFile` → `copyIconIntoImage` (kaynak `kIconSizes`'a
  tamamlanır, `writeImageCopy` ile copyImageFile'ın kuralları ve sınırıyla yazılır). Parser'ın reddettiği bir .ico eskisi
  gibi olduğu gibi kopyalanır (uyarı log'u).
- Ölçekleme artık **premultiplied** (32bppPBGRA) yapılır: düz alfa ile saydam piksellerin rengi kenara sızıyordu (koyu
  saçak). Resimden simge yolu da bundan yararlanır.
- Arayüz: seçilen kaynağın en büyük boyutu 256 px'ten küçükse uyarı toast'ı ("Simge küçük … büyük görünümde bulanık
  olabilir"); iki sekmede de. Kaynak yine kabul edilir.
Doğrulama: birim testler (tamamlama, sıra, bayt bayt koruma, premultiplied kenar, 96 px'li grup, paletli kaynak,
yönlendirme kopyası) + `wlcli icon-patch` gerçek imageres kopyasında (yalnız 256 / yalnız 32 px kaynak → 8 boyut; 96'lı
grup #8305 → kendi seti) + **gerçek imajda (mount) `wlcli apply`**: yönlendirme .ico'su 8 boyut, #3/#4 8 boyut, Windows
369 grubu 0 hatayla yüklüyor. Görsel karşılaştırma: 256'dan üretilen küçük boyutlar Microsoft'unkine çok yakın;
32'den büyütülen 256 bulanık (uyarının gerekçesi).
Kapsam dışı: yönlendirme sekmesi hâlâ yalnız .ico seçtiriyor (motor resmi de kabul ediyor; kart önizlemesi .ico okuyor).

## D-104 — Çevrimdışı program kurulumu (motor) (2026-10-10)
Bağlam: Programlar sayfasının winget programları ilk oturumda INTERNETTEN kuruluyordu (programs.ps1 → winget install).
Kullanıcı: hedef PC internetsiz de kurabilsin diye kurulumları build anında indirip imaja gömelim. Karar (kullanıcı:
"motoru şimdi kur, VM testini işaretle").
Motor: `core/programs/OfflinePrograms.{h,cpp}` — `winget download --id <id> --download-directory <klasör>\<id>` installer +
manifest getirir; manifest `parseYaml` ile okunur (InstallerType + Silent); `offlineInstallCommand` türe göre `{path}`
yer tutuculu kurulum komutu üretir (msi/wix → msiexec; msix → Add-AppxPackage; diğerleri dosyayı sessiz anahtarıyla
çalıştırır). `wlcli programs-download <klasör> <id> [<ad>]`. Veri yolu: `PostSetupPlan` → `offlinePrograms` + `offlineInstallers`
(serileşir); `programsJson` her programa `offline:{file,command}` ekler; programs.ps1 çevrimdışı dalı `apps\<id>\<file>`'den
yerel kurar (hepsi çevrimdışıysa winget hiç beklenmez). Installer dosyaları imaja CopyTree ile girer (apps klasörü).
Orkestrasyon: indir+göm `applyPostSetup` içinde Apply anında yapılır (bayrakla; CopyTree'ye gerek yok) — her program
`winget download` edilir, installer imaja `apps\<id>\<file>` olarak kopyalanır, programs.json offline bilgisini alır;
inen programlar yerel kurulur, inmeyenler winget'e düşer. UI: Programlar sayfasında "Çevrimdışı kur" başlık anahtarı
(PostSetupController.setOfflinePrograms → plan bayrağı).
Doğrulanan: birim testler (manifest parse, kurulum komutu, JSON ve plan round-trip, programs.json çıktısı); gerçek
`wlcli programs-download 7zip.7zip` → 1.9 MB .msi + doğru msiexec komutu; anahtar render edildi; **gerçek imajda (mount)
`wlcli apply` ile offline changeset uygulandı → winget download Apply'da çalıştı, 7-Zip .msi (1.9 MB) `apps\7zip.7zip\`'e
gömüldü, programs.json offline+msiexec taşıyor** (apply exit 0, temiz unmount).
**VM (ağ kartı YOK, `lab_vm -LogDisk`):** offline1 — 7-Zip + Notepad++ (ikisi de wix/msi) `msiexec /quiet` ile exit 0,
ikisi de kurulu, 79 sn; offline2 — Git (inno, `/SP- /VERYSILENT /SUPPRESSMSGBOXES /NORESTART`) + Steam (nullsoft, `/S`)
exit 0, ikisi de kurulu, 120 sn; programs.log'da `winget:` boş (hiç gerekmedi). Kontrol betiği bir D-102 göreviyle
(oturum açılışı, SYSTEM) çalıştı — D-102 de gerçek kurulumda doğrulandı. VM'in yakaladığı hata: programs.ps1 ilk
programdan önce ağ bekliyordu (ağsız PC'de liste hiç başlamıyordu) → ağ + `winget source update` artık yalnız winget ile
kurulacak ilk programdan önce; MSI 1638 'zaten var' sayılır. Ayrıca: `--skip-dependencies` (yalnız programın kendi
installer'ı; gereken runtime ayrı bir winget programı olarak seçilir), Apply süre tahmini çevrimdışı program başına +25 sn.
Bilinen sınır: winget olmayan imajda karışık liste (bazıları inmedi) hepsini durdurur; installer adında/sessiz anahtarında
tırnak olursa `cmd /c` tırnak kuralı sorun çıkarabilir (winget'in dosya adlarında görülmedi).

## D-103 — Çok dilli karşılama sihirbazı (15 dil, kurulan Windows'un diline göre) (2026-10-09)
Bağlam: Sihirbaz metinleri yalnız TR/EN'di ve derleme anında tek dile sabitleniyordu; dağıtılan ISO'lar farklı dillerdeki
kullanıcılara gidiyor. Hedef: sihirbaz, kurulan Windows'un diline (InstalledUICulture) göre kendini göstersin, yoksa
İngilizce.
Karar: Metin iki yerden geliyordu — düz `texts` ($t ile) ve yapısal adlar (themes/accents/privacy/prefs/bundles, derleme
anında gömülü). Yapısal öğelere `nameKey`/`detailKey`/`factsKey` eklendi; oobe.ps1 bunları çalışma anında `$t`'den çözer
(`Tr` yardımcısı, baked-in'e düşer). oobe.json artık `textsByLang` (kod→metinler) taşır; oobe.ps1 InstalledUICulture ile
seçer (zh-Hant/TW→zh-TW, zh→zh-CN, pt→pt-BR, iki harf, 'en'). 15 dil: tr, en, de, fr, es, it, pt-BR, ru, uk, pl, nl,
zh-CN, zh-TW, ja, ko. Çeviriler `resources/strings/welcome/<kod>.json`; `tools/gen_welcome_langs.py` bunları + tr/en
welcome bölümlerini `welcome-langs.json`'a birleştirir (eksik anahtar İngilizce'den dolar), `IDR_WELCOME_LANGS` olarak
gömülür; `welcomeOperations(plan, languages)` oobe.json'a koyar. Preset'e tek bir WriteFile op'u olarak girer.
Doğrulanan: birim testler (textsByLang + key alanları + `welcomeLanguagesFromJson` round-trip); `capture_oobe.py --ui-lang`
ile gizlilik sayfası pt-BR ve **ja (CJK, mojibake yok)** render edildi — başlık/detay/4 rozet hepsi seçili dilde, "Windows"
marka olarak kaldı; 13 çeviri 3 paralel alt-ajanla üretildi (her biri 129 anahtar, `{n}` ve ';' facts korunarak doğrulandı).

## D-102 — Özel zamanlanmış (yinelenen) görev oluşturma (2026-10-09)
Bağlam: Görevler sayfası yalnız var olan görevleri DEVRE DIŞI bırakıyordu; kullanıcı imaja kendi yinelenen görevini
(ör. haftalık bakım betiği) ekleyebilmek istedi. Windows'ta böyle bir görev kurulumdan sonra oluşur.
Karar: Yeni tek-slot ChangeSet op'u `CreateTask` (ScheduledTasks.{h,cpp} genişletildi). SetupComplete.cmd, yeni
`taskcreate.cmd`'den her görev için `schtasks /Create /TN "\WinLove\<ad>" /TR "<komut>" /SC <ONLOGON|ONSTART|DAILY|
WEEKLY|HOURLY> [/ST SS:DD] [/D GÜN] /RU SYSTEM /RL HIGHEST /F` çalıştırır; görev listesi `taskcreate.json`'da tutulur (sayfa
geri okuyabilsin). Devre dışı listesi (tasks.cmd) ayrı kalır. "%" batch için çiftlenir; ad/komutta tırnak yasak
(validCreatedTask). UI: Görevler'de "Görev oluştur…" başlık eylemi + dialog (Ad, Komut, Tetikleyici, Saat, Gün);
nav rozeti CreateTask'i de sayar; TaskController created-task listesini kuyrukta yönetir (ekle/kaldır/temizle).
Doğrulanan: birim testler (satir üretimi, validasyon, JSON + imaj round-trip); gerçek Windows'ta üç örnek
`schtasks /Create` (Weekly/Logon/Daily, /RU SYSTEM) başarıyla oluşturuldu, sorgulandı, silindi; dialog render'landı.

## D-101 — İmaj sağlık denetimi ve onarımı (2026-10-09)
Bağlam: Kullanıcıların getirdiği modlanmış/lite ISO'lar (X-Lite gibi) bozuk olabiliyor; bize açılan imajın bileşen
deposunun sağlığını denetleyen/onaran bir şey yoktu (yalnız host DISM denetimi + ResetBase vardı). DISM API'nin
DismGetImageHealth'i, servis yığını ilerlemiş bir hostta modlanmış imajlarda güncellemeler gibi takılabiliyor.
Karar: `core/image/dism/ImageHealth.{h,cpp}` — mount edilmiş imaj üzerinde Windows'un kendi dism.exe'siyle (oturumumuz
askıya alınarak) `/Cleanup-Image /CheckHealth` (hızlı), `/ScanHealth` (ayrıntılı) ve `/RestoreHealth` çalışır;
verdict çıktı metninden okunur (Healthy / Repairable / NonRepairable). `wlcli health <mount> [check|scan|restore]`
(repairable/non-repairable'da exit 2; restore için `--source=<WIM:...:1|ESD:...:1|\Windows>` ve `--limit-access`).
UI: Sürümler'de mount edilmiş sürümün bağlam menüsünde "Sağlığı denetle / tara"; bozulma bulununca toast "Onar"
düğmesiyle RestoreHealth'i (önce imajın kendi WinSxS'i) sunar. RestoreHealth kaynaksız WinSxS'e, yoksa Windows
Update'e bakar; çevrimdışı derin bozulma için bir kaynak (eşleşen install.wim/ESD) gerekir — 0x800F081F'te net uyarı.
Doğrulanan: gerçek 25H2 imajında (mount edilmiş) `health check` ve `health scan` → "image is healthy" (exit 0);
parse mantığı birim testli (healthy/repairable/non-repairable/restored örnek çıktıları).

## D-100 — Solid install.wim (uzantısı .wim ama içerik ESD) tanınır (2026-10-09)
Bağlam: Kullanıcı, kullanıcıların mount edemediği bir özel ISO bildirdi ([Windows X-Lite] Neon Gamer, 19045.3324).
ISO okunuyor (UDF 1.02; `wlcli info/ls` çalışıyor) ama `sources\install.wim` aslında LZMS solid (içerik olarak ESD).
Karar: `isEsdSource()` yalnız `.esd` uzantısına bakıyordu → Bağla açık kalıp DISM solid imajda başarısız oluyordu.
`isSolidInstall()` başlığı okur (`header.solid` veya LZMS); böyle bir kaynak "paketlenmiş" sayılır: Bağla kapanır ve
net mesaj gösterir, "ESD/solid → WIM" eylemi düşük riskli bir dışa aktarmayla düz (LZX) mount edilebilir WIM üretir
(ISO ise önce çalışma kopyasına çıkarılır). Doğrulanan: X-Lite ISO okundu, solid install.wim 74 sn'de düz WIM'e döndü,
düz WIM mount oldu (ntoskrnl var) ve sorunsuz unmount oldu.

## D-099 — Karşılama sihirbazı: duvar kağıdı ve uygulama paketi sayfaları (2026-10-09)
Bağlam: Kullanıcı sihirbaza "program paketleri + duvar kağıdı sayfaları" istedi; "görsel ekleyemem, PC başında
değilim" dedi → duvar kağıdı sayfası Windows'un kendi hazır duvar kağıtlarını kullanır (görsel gerektirmez). Paketler
için kullanıcı OOBE'de seçer.
Karar: iki yeni premium sayfa (oobe.ps1), diğerleriyle aynı stil. **Masaüstü arka planı:** `Windows\Web\Wallpaper`
(+4K/Screen) küçük resim ızgarası, solda canlı monitör önizlemesi; seçim yeni hesaba yazılır (`Control Panel\Desktop\
WallPaper`, Default profil + .reg). **Uygulama paketleri:** operatörün tanımladığı paketler (Temel, Tarayıcılar,
Medya, İletişim, Geliştirici, Oyun, Araçlar), kullanıcı işaretler; seçilen winget id'leri ilk oturumda kendini silen
bir oturum göreviyle kurulur. Katılımsız modda `auto.wallpaper` / `auto.bundles` uygulanır. ScrollViewer'lara ince
modern (Windows 11) scrollbar (kullanıcı eski scrollbar'ı fark etti). `wlcli welcome-json` + `tools/capture_oobe.py`
ile VM'siz render doğrulaması.

## D-098 — `wlcli welcome-json` + VM'siz sihirbaz render aracı (2026-10-09)
`wlcli welcome-json <out> <strings.json>` gerçek `welcomeJson`'ı bir strings dosyasının welcome bölümünden üretir;
`tools/capture_oobe.py` buna önizleme alanlarını ekleyip oobe.ps1'i önizleme modunda çalıştırır ve PrintWindow ile
yalnız o pencereyi yakalar (tam ekran değil). Sihirbaz her değişiklikte 40 dk'lık VM kurulumu olmadan görülür.
Windows 10'un tam setleri "Feature update to Windows 10, version 22H2" adıyla listelenir → `buildKind` bunları da
Release sayar.

## D-097 — Desteklenmeyen PC'de yerinde yükseltme: medyadaki betik konak denetimlerini düzeltir (2026-10-09)
Bağlam: Temiz kurulum boot.wim'deki LabConfig ile geçiyor; yerinde yükseltme (çalışan Windows'tan setup.exe) *canlı*
konak kayıt defterini okur, boot.wim ona erişemez. Araştırma (Rufus kaynağı, KB5053484): güvenilir yol, kullanıcının
setup.exe yerine çalıştırdığı bir medya betiğinin konak değerlerini yazması.
Karar: Önyükleme atlatması açıkken medyaya "Yukselt - desteklenmeyen PC.cmd" eklenir — kendini yükseltir, Setup'ın
baktığı değerleri yazar (`AppCompatFlags\HwReqChkVars` = SecureBoot/TPM2/8GB, `MoSetup\AllowUpgrades...`=1, eski
önbellekler temizlenir) ve setup.exe'yi başlatır. Eksik TPM, Secure Boot yok, az RAM, desteklenmeyen CPU'yu kapsar;
normal Setup penceresini, dosya ve uygulamaları korur. (POPCNT/SSE4.2 24H2+'da aşılamayan sert alt sınır.)

## D-096 — Kurulum Sonrası: .pow güç planı içe aktarma (2026-10-09)
Yeni bir adım türü (PowerPlan): seçilen .pow imaja kopyalanır ve SetupComplete'te (SYSTEM, ilk oturumdan önce)
üretilen bir GUID altında içe aktarılıp etkin şema yapılır (`powercfg /import` + `/setactive`). GUID adımın
`destination` alanında taşınır (yapı değişmez). "Güç planı ekle" Kurulum Sonrası sayfasında.

## D-095 — "Bu bilgisayardan al": Programlar ve Tweaks (2026-10-09)
Bağlam: Kullanıcı bu PC'nin programlarını ve ayarlarını imaja taşımayı istedi. **Programlar:** bu PC'nin Kaldır
kayıtları (her iki görünüm + kullanıcı; Windows'un kendileri hariç) imzalı winget dizinine eşleştirilir (ürün kodu,
yoksa normalize ad + yayıncı — winget'in yaptığı gibi); dialogda çalışma zamanları ayrı ve kapalı, seçilenler ilk
oturum programlarına tek kuyruk düzenlemesiyle eklenir (bu PC'de 65'in 34'ü). **Tweaks:** her ayar bu PC'nin kayıt
defterine göre değerlendirilir (`liveRegistryHolds`); servis/dosya ayarları yargılanmaz. 61 ayar, "Geri al"lı.

## D-094 — Secure Boot 2023 önyükleme yöneticili medya (2026-10-09)
Bağlam: Kullanıcının seçtiği 2. özellik. Microsoft PCA 2011'i DBX'e alınca eski medya açılmaz; db'sinde "Windows UEFI
CA 2023" olmayan PC'de ise yalnız eski medya açılır. Microsoft'un yolu `Make2023BootableMedia.ps1` (KB5053484): boot.wim
1. imajını bağlayıp `EFI_EX` / `DVD_EX` / `Fonts_EX` dosyalarını medyaya kopyalar, `efisys_EX.bin` ile oscdimg.
Karar:
- Bağlama yok: `wimFileData` (WimVerify'nin okuyucusu; dosya listesinden akışın SHA-1'i, sözlük tablosundan akış,
  LZX/XPRESS çözme, SHA-1 doğrulama) dosyaları doğrudan okur; yönetici gerekmez.
- Medya klasörü değişmez: dosyalar ISO'ya (ve USB'ye) `replacedFiles` ile girer, `IsoOptions::efiBootImage` UEFI
  El Torito imajını değiştirir. BIOS önyüklemesi, BCD, boot.wim aynı kalır. Medya güncellemesinden (D-080) sonra
  uygulanır; boot.wim güncellenmişse en yeni önyükleme yöneticisi ondan alınır.
- Seçenek, varsayılan kapalı (ISO Oluştur › Önyükleme › Secure Boot); yanında bu PC'nin db durumu
  (`GetFirmwareEnvironmentVariable(db)` içinde "Windows UEFI CA 2023"): tanıyor / tanımıyor (uyarı) / bilinmiyor.
- `wlcli iso --secureboot2023`, `wlcli secureboot-db`.
Doğrulanan: ISO'daki `bootx64.efi` imza zinciri Windows UEFI CA 2023 (varsayılan ISO'da PCA 2011).
Doğrulanmayan: Secure Boot açık bir VM'de açılış (VMware Workstation'ın varsayılan db'si bilinmiyor).

## D-093 — Windows indir: UUP'tan ISO, kendi dönüştürücümüzle (2026-10-09)
Bağlam: Kullanıcı Microsoft'tan ISO indirmeyi istedi. Önce Microsoft'un sitesi denendi: API'si otomatik istekleri
"Sentinel" ile reddediyor (Fido'nun yaptığı gibi tarayıcı parmak izi taklidi bot korumasını aşmak olur, yapılmadı).
Media Creation Tool'un `products.cab` adresi (fwlink 2156292) 24H2'de kalmış. Download Center 25H2'nin `products.xml`'ini
veriyor. Güncel MCT (`SetupPrep.exe` 26100.7019) kataloğu artık Windows Update'in meta veri servisinden alıyor
(`SetupMgr.dll` → `DownloadProductsCabFromDCAT`, `udiapi.dll` → `fe3.delivery.mp.microsoft.com`; MCT'nin gömülü
CAB'i açılarak bakıldı). Kullanıcı UUP API'sini önerdi; iki yol karşılaştırıldı (ESD kataloğu: tek dosya, hızlı ama yalnız
güncel sürüm, adresi gizli; UUP: her sürüm, son yama dahil ama parçalı). Kullanıcı "mantık olarak hangisi" diye sordu,
UUP önerildi.
Karar:
- **Liste UUP dump'tan** (`api.uupdump.net`: listid / listlangs / listeditions / get), **dosyalar Microsoft'tan**:
  yalnız `trustedUupUrl` sunucuları (D-061'deki kontrol), her dosya SHA-256'sıyla (yoksa SHA-1) doğrulanır. Linkler
  ~15 dakikada sona eriyor (HTTP 403): indirici linkleri yeniden ister, `.part` kaldığı yerden sürer, 4 bağlantı.
  `get.php` farklı bir set için 10 sn'de bir yanıt veriyor (`USER_RATE_LIMITED`): bir kez beklenip yeniden denenir.
- **Dönüştürücü kendi kodumuz** (`core/uup/UupConvert`, wimlib yok): uup-converter-wimlib'in (abbodi1406) adımları
  kaynaktan çıkarıldı, wimgapi + DISM ile yapıldı. Metadata ESD'nin 3. imajı yalnız dosya listesi; dosyalar paket
  ESD'lerinde ve `update.mum`'lu FOD CAB'lerinde. CAB'ler açılıp ACL'siz referans WIM'e yakalanır (`captureReference`:
  `WIM_FLAG_NO_DIRACL | NO_FILEACL | NO_RP_FIX`; yine de SeBackupPrivilege ister → yönetici), sonra
  `exportImageWithReferences` (WIMSetReferenceFile farklı GUID'li dosyaları da kabul ediyor; ölçüldü: wimlib 2,97 GB,
  wimgapi 3,0 GB, aynı imaj). WinRE (2. imaj) her sürüme `Winre.wim` olarak girer; `boot.wim` WinRE'den kurulur
  (1: "Microsoft Windows PE" FLAGS 9, `SystemRoot` / `InstRoot` = `X:\$windows.~bt`; 2: "Microsoft Windows Setup"
  FLAGS 2, `X:\setup.exe` + Setup'ın `sources` dosya listesi; ikisinde de `winpeshl.ini` silinir, `CustomBackground`,
  `CustomShell` silinir; önyükleme imajı 2).
- **Güncellemeler** (isteğe bağlı, varsayılan açık): rolü dosya adından değil **içerikten** (expand.exe `update.mum` +
  dosya listesi). Her `update.mum` bir "ServicingStack" adı taşıyor; SSU yalnız `-servicingstack_` manifesti olan pakettir.
  Sıra: Edge (`dism /Add-Edge`), SSU, enablement + .NET + diğer, en yeni LCU MSU'su (checkpoint aynı klasörde, DISM
  kendisi bulur), `StartComponentCleanup`. Setup DU medyanın `sources`'una (D-080'in kuralı), güncel Windows'tan
  önyükleme yöneticisi. Mağaza uygulamaları ayrı ve parçalı (2.798 dosya), bu sürümde alınmıyor.
- Arayüz: İMAJ grubunda **Windows indir** sayfası (tablo + sağ panel). İndirme ağ iş parçacığında, dönüştürme motor
  iş parçacığında. Başarıda set silinir (~9 GB), hatada kalır (sonraki deneme sürer). Bitince "ISO hazır · Aç" bildirimi.
Doğrulanan: `wlcli uup convert --no-updates` 375 sn, 4,28 GB ISO; VM kurulumu (`uup1`) aşağıda STATUS'ta.

## D-092 — Yazı tipi ve simgeler: Inter / Geist / Segoe UI Variable, Lucide (2026-10-09)
Bağlam: Kullanıcı IBM Plex'i ve tasarım paketinin simgelerini beğenmedi ("özensiz, çok daha kaliteli keskin güzel ikonlar").
Simgeler 1,25 çizgiyle çiziliyordu; %175'te 2,19 piksel, kenarlar bulanık. Tasarım paketi (`WinLove-UI-Handoff`) salt
okunur kalır; bu kayıt ondan sapmadır.
Karar: Varsayılan yazı tipi **Inter** (OFL); Ayarlar › Görünüm › Yazı tipi: Inter · Geist (+ Geist Mono) · Segoe UI
Variable (sistemin, gömülmez). IBM Plex kaldırıldı. Simgeler **Lucide 1.54.0** (ISC, `third_party/lucide`, yalnız
kullanılan SVG'ler): `resources/icons/icon-map.json` paketin adlarını Lucide'ye eşler (kod `Icon` enum'unu korur),
"extra" yeni adlar ekler (Programlar, Kişiselleştirme, Simgeler, Başlat menüsü); pencere başlığı düğmeleri ve marka
işareti paketin kalır. Çizgi 24 ızgarada 1,75 (16 px'te 1,17). `tools/gen_icons.py` SVG öğelerini (path, circle, rect,
line, poly*) yola çevirir, yayları doğru ölçekler. Yoğunluk (Kompakt / Rahat / Büyük) ve Windows'un metin boyutu bütün
arayüzü ölçekler (`Window::setZoom`).
Sonuç: Galeri sayfası bütün simgeleri sırasıyla gösterir; render `--font=inter|geist|segoe`.

## D-091 — Tasarım yenilemesi: gezinme grupları, iş akışı adımları, koyu tema (2026-10-09)
Bağlam: Kullanıcı 12 tasarım önerisinin hepsini istedi; koyu temanın "siyaha yakın" olmasını ayrıca istedi.
Karar: Gezinmede katlanabilir grup başlıkları (ayarda saklanır), benzer simgeler ayrıldı; durum çubuğunda Kaynak › Bağla ›
Düzenle › Uygula › ISO adımları, kuyruk özeti (Uygula düğmesinin ipucu), indirmeler; koyu temanın yüzeyleri siyaha yakın,
katmanlar daha ayrık (`Palette.cpp` kDarkSurfaces — paketin token'larından sapma); Windows yüksek kontrastı açıksa sistem
renkleri. Diğer maddeler bu kayda eklenir.

## D-090 — Genel incelemenin kritik düzeltmeleri: veri kaybı ve "başarılı" görünen hatalar (2026-10-09)
Bağlam: Beş paralel kod incelemesi (`docs/AUDIT-2026-10.md`); kullanıcı kritik 10 maddenin hepsini seçti.
Karar:
- **Bağlama klasörü:** DISM kaydı olmayan dolu klasör yalnız içi imaj artığıysa (Windows\System32 ya da yalnız imaj
  kökü adları) temizlenir; değilse yeni durum `Foreign` — hiçbir şey silinmez. DISM'in yolu dosya kimliğiyle de eşlenir.
  Ayarlar dolu klasörü bağlama klasörü olarak kabul etmez.
- **Kuyruk:** her değişiklikte `queue.json`'a (answers.dat'ın yanına) yazılır, aynı WIM + index benimsenince geri gelir;
  çözme dialogu uygulanmamış değişiklik sayısını söyler, "Önce uygula" sunar.
- **Preset uygulamaları:** `RemoveAppx` tam adla bulunmazsa (`0x80073CF1` / `0x80070002`) paket ailesiyle imajdaki sürüm
  kaldırılır; preset yüklenirken adlar imajdakilere çevrilir.
- **ISO çalışma kopyası:** klasörün yanında `<ad>.source.json` kaydı (hangi ISO, çıkarma bitti mi, imaj dosyalarının boyut /
  zamanı). Değişmemiş kopya olduğu gibi, yarım kalan tamamlanır (tam dosyalara dokunmadan); değişmiş / kaydı olmayan /
  başka ISO'nunki için sorulur (baştan çıkar / olduğu gibi kullan).
- **Uygula:** bir adım başarısızsa imaj kaydedilmez, bağlı kalır (`ApplyJobResult::held`); sayfa nedenleri ve "Çöz…"
  gösterir. Diğer sürümler (D-055) eskisi gibi kaydedilir. `wlcli apply --commit` de bekletir; `--commit-with-failures`.
- **Arayüz yeniden kurulumu:** aygıt kaybında yalnız aygıt nesneleri yeniden kurulur (kabuk ve çalışan işler yaşar);
  Sıfırla iş sürerken dili değiştirmez; indirmeler de dil değişimini kilitler.
- **Tek örnek:** oturum başına (ve `--profile` başına) tek WinLove; ikincisi öncekini öne getirir, UAC'den önce. İmaja
  yazılırken uyku engellenir, kapanış nedenle sorulur.
- **Yanıt dosyası:** karşılama açıkken diğer hesaplar ve Administrator parolası korunur (oobe.ps1 birleştirir); klasik
  otomatik oturumun parolası ilk oturumda Winlogon'dan silinir; yanıtlar / presetler karşılama dönüşümünden geçmeden,
  `welcome` yanında saklanır.
- **Arama:** bütün arama kutuları `text::fold` (Türkçe İ/ı ve ç ğ ö ş ü) kullanır.
Sonuç: `tools\lab_audit_apply.ps1` ALL PASSED (gerçek imaj: A4 + A7). VM: b1k (karşılama + ek hesap + Administrator
parolası), b2k (otomatik oturum parolası) — sonuçları STATUS'ta.

## D-088 — Karşılamada akıcılık ve anlamlı bir son ekran (2026-10-08)
Bağlam: Kullanıcı karşılamanın efektlerinin ve tıklamalarının daha akıcı / kaliteli olmasını, yeniden başlatmadan önceki
"Her şey hazırlanıyor" ekranının "aşırı hızlı dolmamasını, anlamlı olmasını" istedi.
Bulgu: bekleme döngüsü (`Update-Ui` + `Start-Sleep 30`) arayüz iş parçacığını 30 ms'de bir uyutuyordu: WPF animasyonları
~30 fps'de takılarak, fare olayları uykular arasında gecikerek işleniyordu. Son ekranın asıl işi ~2 sn: adımlar bir anda
geçiyor, çubuk sıçrıyordu.
Karar:
- **Döngü:** `Wait-Seconds` bir `DispatcherFrame` + `DispatcherTimer` ile bekler (dispatcher hiç uyumaz, pencere bekleme
  boyunca çizer ve tıklama alır); D-084 yolunun "en üstte kal"ı 2 sn'lik bir zamanlayıcı.
- **Denetimler** (şablon tetikleyicileri, animasyonlar yalnız saydamlık ve dönüşümlerde — grafik sürücüsü olmayan
  Kurulumda da akıcı): düğmede üzerine gelme 120 ms'de gelir / 200 ms'de gider, basınca yazı kısılır, vurgu düğmesi
  koyulaşır; alanda odakta vurgu çizgisi ortadan açılır; anahtarın topuzu kayıp büyür, iz renkle dolar; onay kutusunda
  kare dolar, tik hafif yaylanarak çıkar; tıklanan satırlarda (tercihler, gizlilik, ağlar) Windows Ayarları'nın üzerine
  gelme rengi.
- **Sayfa geçişi:** eski sayfa 110 ms'de söner; yenisinde başlık, alt başlık, içerik 45 ms arayla kayarak gelir (Fluent
  "decelerate": ExponentialEase), soldaki çizim %94'ten büyüyerek gelir (gölgeleri animasyon boyunca `BitmapCache`'te);
  adım göstergesinde bulunulan çizgi uzar. Seçilen tema / vurgu / gizlilik işareti "pop" ile çıkar. Tema ya da vurgu
  değişince pencerenin eski hâlinin resmi yeni renklerin üstünde 320 ms'de söner (anlık renk çakması yok).
- **Son ekran:** Windows 11'in ilerleme halkası (yay uzayıp kısalarak döner; her karede C#), her adımın altında ne yapıldığı
  (ad · parola durumu, bilgisayar adı · saat dilimi, gizlilik düzeyi, tema · vurgu · açık tercih sayısı, "Ardından
  bilgisayar yeniden başlar"), etkin adımda dönen küçük yay, biten adımda tik "pop"; her adım en az 1,2–1,7 sn ekranda kalır
  (iş daha kısa sürer; bekleme yalnız ekranda), çubuk adım boyunca yavaşlayarak ilerler; sonunda "Her şey hazır", ekran
  0,5 sn'de siyaha kararıp Kurulumun kendi siyah ekranına geçer. Toplam ~10 sn.
- Yeni metinler: `readyHeading`, `readySub`, `stepFinishDetail`, `prefsOn` (karşılama metni 109).
Doğrulama: şablonlardaki 18 storyboard gerçek denetimlerde tek tek çalıştırıldı (yanlış hedef / özellik yolu pencereyi
çalışırken düşürürdü); önizleme kareleri `build\visual\oobe-after\` (son ekran, sayfa geçişi, tema geçişi). **VM `u3`**
(kullanıcının yanıt dosyası, Home SL, NAT ağı, parolasız "user", ekran 3 sn arayla) **ALL PASSED**: Kurulum içinde (SYSTEM,
grafik sürücüsüz) halka derlendi, adımlar ayrıntılarıyla ~6 sn, "Her şey hazır", siyaha kararma, yeniden başlatma;
masaüstü, parola boş ve süresiz, tema / tercihler / gizlilik doğru (`build\visual\u3-busy-sheet.png`).

## D-089 — OOBE'nin güncelleme adımını HOSTS ile atlamak: denendi, işe yaramadı (2026-10-08)
Bağlam: Kullanıcı D-087'yi kendi VM'inde doğruladı; ağ bağlıyken yeniden başlatmadan sonra OOBE'nin "Lütfen bekleyin,
güncelleştirmeler denetleniyor" adımı (ZDP, birkaç dakika + bir yeniden başlatma) yine geliyor, atlanmasını istedi.
Seçenekler: `SkipMachineOOBE` / `SkipUserOOBE` (Microsoft: kullanmayın), NCSI etkin yoklamasını kapatmak (Windows ve
uygulamalar internet yok sanır), HOSTS'ta `sdx.microsoft.com` (referans araca göre OOBE'nin "sonraki adım / zorunlu güncelleme"
sorgusu). Denenen: PreOobe `0.0.0.0` + `::` satırları, ilk oturumda kaldırılır. **VM `u4`** (26200.8037, NAT ağı): güncelleme
ekranı yine geldi — bu sürümde OOBE kararı başka yerden alıyor. Karar: kod geri alındı; 1.1.0 Beta'da "Bilinen" olarak
yazıldı. Yeniden denenecekse: NCSI (ilk oturumda geri açarak) ya da OOBE'nin ZDP'sinin hangi adrese gittiği ölçülerek.

## D-087 — Karşılamanın ilk gerçek testi: boş parola, bölge / klavye, parola süresi (2026-10-08)
Bağlam: Kullanıcı kendi ön ayarıyla (Windows 11 Home Single Language; yanıt dosyasında dil yok, parolasız hesaba izin
var) ISO yapıp NAT ağlı bir VMware VM'ine kurdu: sihirbazda "user", parola boş. Yeniden başlayınca Windows bölge ve
klavye sordu, güncelleme kurup yeniden başladı; sonra kilit ekranı parola istedi, boş parolayı kabul etmedi.
Tanı (VM kapatıldı, diski 7-Zip ile salt okunur açıldı: `oobe.log`, `Panther` / `UnattendGC` günlükleri, SAM'deki hesap
bayrakları):
- **Boş parola bizim hatamız:** PreOobe `Panther\unattend.xml`'i `XmlDocument` ile açıp kaydederken boş parola elemanı
  girintilenip **satır sonu + boşluk** olarak yazıldı; Windows (`[Shell Unattend] UserAccounts: Password set`) bunu
  hesabın parolası yaptı. `AutoLogon`'un değeri farklı derinlikte (10 / 14 boşluk) olduğundan hesabınkiyle eşleşmedi:
  otomatik oturum açılmadı, kilit ekranında yazılamayan bir parola kaldı.
- **Bölge / klavye:** oobeSystem'de `International-Core` yoktu; OOBE bölge ve klavye sayfalarını geçici `defaultuser0`
  oturumunda gösterdi (`CommitRegion` / `CommitKeyboards`), ağ olduğu için ZDP güncellemesini kurup yeniden başladı.
- **Parola süresi:** yanıt dosyasıyla açılan hesapta "parola süresiz" bayrağı yok (SAM ACB 0x10; OOBE'nin kendi hesaplarında
  0x200): Windows'un 42 günlük üst sınırı işler.
- Laboratuvarda üçü de görülmedi: yanıt dosyalarında tr-TR hep vardı, parola hep doluydu, ağ yoktu.
Karar: PreOobe dosyayı `PreserveWhitespace` ile açar (Kurulumun biçimi korunur, boş değer `<Value></Value>`); oobeSystem'de
`International-Core` yoksa ya da eksikse sistemin kendi değerleri eklenir — klavyeler `HKU\.DEFAULT\Keyboard
Layout\Preload` (+ `Substitutes`), `Get-WinSystemLocale`, `InstalledUICulture`, `Get-Culture`; dosyada olan değere
dokunulmaz. PostOobe hesabı `PasswordNeverExpires` yapar (D-084 yolunda parolasız hesap da). WinLove'un C++ yanıt
dosyası üreticisi boş parolayı zaten `<Value></Value>` yazıyordu. Ön ayar betiğin kaydedildiği andaki metnini taşır
(kullanıcınınki hatalı sürümü taşıyordu): ön ayar uygulanırken betik uygulamanın güncel sürümüyle değişir, seçimler
(`oobe.json`) kalır (`withCurrentWelcomeScript`, D-076'daki tarif yenilemesi gibi).
Kanıt: VM `u1` (kullanıcının yanıt dosyası, Home SL, NAT ağı, "user" parolasız) ve `u2` (aynısı, parolalı) **ALL PASSED**:
bölge / klavye sayfası yok, ZDP'nin yeniden başlatmasına rağmen kendiliğinden oturum, u1'de `PasswordRequired` False,
ikisinde parola süresiz, tema / vurgu / tercihler / gizlilik / saat dilimi doğru; ekranlar `build\visual\u1-oobe-sheet.png`.
Kalan: ağ varken OOBE ~3,5 dk Windows'un "Güncelleştirmeler denetleniyor" ekranlarını gösterir ve bir kez yeniden başlar
(soru sormaz). Kullanıcının o VM'i kurtarılamaz (parola yazılamayan boşluk), yeniden kurulmalı.

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
Bağlam: referans araç eksik listesinden (4). D-067'nin "Sabitlenmiş uygulamalar" anahtarı OEM yöntemini kullanıyordu
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
Bağlam: referans araç eksik listesinden kullanıcının seçtiği (3). Bir kaldırma ya da devre dışı servis, imajda kalan bir uygulamanın
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
Bağlam: referans araç karşılaştırmasında kullanıcının seçtiği eksik: toplu güncelleme yalnız install.wim'e gidiyordu; WinRE ve
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
sürümü" `X:\sources\setup.exe`. Yeni kurulum kurduğu imajın WinRE'sini kullanıyor (referans araç: "Modern setup uses winre.wim").
Karar:
- **Motor:** `BootPatch::legacySetup` — D-038'in boot.wim yamasına bir değer: `Setup\CmdLine = cmd /c start /min wpeinit &&
  \sources\setup` (referans araç topluluğunda gerçek donanım + Ventoy/YUMI ile denenmiş biçim; yalın `X:\sources\setup.exe` çoklu
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
→ D-140 (2026-11-11): Hakkında'ya güncelleme denetimi kartı; denetim varsayılan açık. Sözün "kimseye bir şey göndermez"
cümlesi değişmedi, sahibinin kararını bekliyor. D-145 (2026-11-11): "Yükle" ile güncellemenin indirilmesi eklendi; cümle
yine değişmedi.

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
bulalım, referans araçta bile yok. Başlat menüsünü kullanıcı istediği gibi özelleştirsin, temizlesin, uygulama sabitlesin; yüklü
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
Bağlam: "Başka Windows dizinlerinde mount edilmiş sistemleri algılasın, referans araç gibi."
Karar: Kaynak sayfasında "Bu bilgisayarda bağlı imajlar" tablosu (yönetici gerekir: DISM): her DISM mount'u (`inspectMounts`),
klasör, sürüm, WIM, durum (hazır / salt okunur / yeniden bağlanacak / bozuk / WIM yok / kayıtsız artık / WinLove'da açık). Çift
tık / Enter: imaj benimsenir (gerekirse remount), kaynağı açılır ve bağlı imaj olur — başlangıçtaki kurtarmayla aynı yol;
uygulama, kaydetme, ayırma o klasörde yapılır. Delete: onayla kaydetmeden ayırma (bozuk olanlar için onarım yolu). Bir imaj
açıkken başka birine geçilmez. Kaynak sayfası her açılışta listeyi yeniden okur. `--demo-mounts`.
→ 2026-11-11: liste önce wimgapi'den (`inspectMountsFast`), DISM okuması arkadan (D-136'nın eki, madde 3; gerçek bağlı
imajla denenmedi).
Kanıt: render; motor parçaları (`inspectMounts`, `repairMount`, `unmountSafely`) mevcut lab'larla kanıtlı. **Görülmeyen:** başka
araçla bağlanmış imajın uygulamada benimsenmesi.
→ D-148 (2026-11-11): başka araçla `\\?\` yollarıyla bağlanmış imaj açılmıyordu; devralma tek motor işi oldu
(`adoptionStep`: Ok / NeedsRemount → kullan, gerisi dokunmadan red), liste `blockersOf` çalıştırmıyor, başka araca ait
bağlamada `optimizeWim` ve D-055 yok. Motor yolu (`wlcli adopt`, `mounts --fast`) yönetici lab'ında gerçek bağlamayla
kanıtlandı; NeedsRemount yalnız birim testi, pencerede denenmedi.

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
Bağlam: Kullanıcı referans araçtaki gibi derin kaldırmayı istedi ("uyarılarını da kullanıcıya söylemeliyiz"). D-059'daki ölçüm:
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
Bağlam: Kullanıcı Bileşenler sayfasının referans araca göre çok az şey sunduğunu söyledi ("gerçekten iyi bir tarama
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
  kayıtları da tutarlı biçimde söken bir "derin kaldırma" (referans araç tarzı) yeni bir karar ve VM'de güncelleme testi ister.

## D-058 — Kaynak ve İmajlar araçları: SHA-256, arama, sıkıştırma, SWM, çoğaltma, sürüm ekleme, yakalama (2026-10-01)
Bağlam: Kullanıcı Kaynak / İmajlar sayfalarının referans araca göre eksiklerinden 1, 2, 3, 4, 5, 6, 7, 9'u seçti ("profesyonelce").
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
Bağlam: Kullanıcı referans araca göre eksik kolay özelliklerden 1, 2, 3, 4, 5, 6, 8'i seçti. 2 (WinRE kaldırma, D-031)
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
→ boot.wim sürücüleri D-134 (2026-11-10) ile genişledi ve lab + VM ile kanıtlandı.

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
- **Kaynak: Microsoft Update Catalog** (`www.catalog.update.microsoft.com`) — referans aracın ve topluluk araçlarının
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
→ Vurgu renkleri D-137 (2026-11-11; Mavi · Bakır · Deniz · Nar · Zeytin, türetilmiş tablolar) ve D-143 (2026-11-11;
serbest renk, `"accent": "#RRGGBB"`) ile değişti.

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
