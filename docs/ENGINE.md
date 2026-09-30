# Motor (wl::image) — "Kendi DISM API'miz"

Amaç: Uygulamanın geri kalanı DISM'i hiç bilmez; yalnızca `wl::image` arayüzünü bilir. Arkada hangi Windows API'sinin (ya da kendi yazdığımız kodun) çalıştığı değiştirilebilir. Böylece:
- DISM'in yapamadığı şeyleri (ör. "kalıcı" paketleri kaldırma, AppX'in C API'sinin olmaması) kendi uygulamamızla tamamlarız,
- her işlem `wlcli` ile UI olmadan test edilir,
- hata kodları tek tip `Error`'a çevrilir.

## 1. Arka uçlar (backends)

| Backend | Kaynak | Ne için |
|---|---|---|
| `WimgApiBackend` | `wimgapi.dll` (WIMCreateFile, WIMGetImageInformation, WIMExportImage, WIMDeleteImage, WIMApplyImage, WIMCaptureImage, WIMMountImageHandle) | Index listeleme (XML), export, index silme, ESD↔WIM dönüştürme, split (SWM) |
| `DismApiBackend` | `dismapi.dll` (SDK: `dismapi.h`) — DismInitialize, DismOpenSession, DismMountImage, DismUnmountImage, DismGetPackages, DismRemovePackage, DismAddPackage, DismGetFeatures, DismEnable/DisableFeature, DismGetCapabilities, DismAdd/RemoveCapability, DismAddDriver, DismGetDrivers, DismCleanupMountpoints | Mount, paketler, özellikler, capability, sürücü, güncelleme |
| `DismExeBackend` (fallback) | `dism.exe` alt süreç, çıktısı ayrıştırılır | C API'si olmayan işlemler: `/Remove-ProvisionedAppxPackage`, `/Get-ProvisionedAppxPackages`, `/Cleanup-Image /StartComponentCleanup /ResetBase`, `/Set-Edition` |
| `OfflineRegistry` | `RegLoadKey`/`RegUnLoadKey` (SeBackup/SeRestore ayrıcalığı) veya `offreg.dll` | Kayıt defteri tweak'leri, servis başlangıç türleri, CBS paket görünürlüğü |
| `IsoIO` | Okuma: `virtdisk` (AttachVirtualDisk, ISO read-only) veya 7-zip'siz kendi UDF okuyucumuz (gerekirse). Yazma: IMAPI2FS (`IFileSystemImage`, El Torito BIOS+UEFI boot) → fallback ADK `oscdimg.exe` | ISO açma ve oluşturma |
| `Native*` (ileride) | Kendi uygulamalarımız | CBS paket manifestlerini okuyup bileşen bağımlılık grafiği çıkarma, kalıcı paket kaldırma (Visibility/Owners düzenleme + dosya temizliği) |

Kural: Bir işlev önce resmi API ile yapılır. Resmi API yetmiyorsa `DECISIONS.md`'ye gerekçe yazılıp native uygulama yapılır.

## 2. Arayüz taslağı
```cpp
namespace wl::image {
  Result<SourceInfo>             openSource(const Path& isoOrWim, Progress&, CancelToken&);
  Result<std::vector<ImageInfo>> listImages(const Path& wim);
  Result<void>                   exportImage(const Path& src, int index, const Path& dst, Compression, Progress&, CancelToken&);
  Result<void>                   deleteImage(const Path& wim, int index);

  Result<Session>                mount(const Path& wim, int index, const Path& mountDir, Progress&, CancelToken&);
  Result<void>                   unmount(Session&, Commit commitOrDiscard, Progress&, CancelToken&);
  Result<std::vector<MountInfo>> listMounts();
  Result<void>                   cleanupMounts();

  // Session üzerinden (hepsi offline imaj üzerinde)
  Result<std::vector<Package>>    packages(Session&);
  Result<std::vector<Feature>>    features(Session&);
  Result<std::vector<Capability>> capabilities(Session&);
  Result<std::vector<AppxPackage>> provisionedAppx(Session&);
  Result<std::vector<Driver>>     drivers(Session&);
  // değiştiren işlemler yalnızca ops::Applier tarafından çağrılır
}
```

## 3. İşletim kuralları
- DISM oturumu **tek motor thread'inde** açılır ve kullanılır. `DismInitialize` süreç başına bir kez.
- Mount dizini kısa ve boşluksuz: `build\lab\mount\<n>` (geliştirme) / kullanıcı ayarı (üretim).
- Her mount `mounts.json`'a yazılır; açılışta `DismGetMountedImageInfo` ile karşılaştırılır, sahipsizler raporlanır.
- İptal: DISM `CancelEvent` handle'ı `CancelToken`'a bağlanır.
- İlerleme: DISM progress callback → `Progress` (UI'a throttle 30 Hz).
- Hata: `HRESULT` + DISM hata mesajı (`DismGetLastErrorMessage`) + işlem bağlamı → `Error`.

## 4. wlcli (motor test aracı)
UI'dan bağımsız; her motor yeteneği önce buraya komut olarak eklenir.
```
# Var olanlar (admin gerekmez)
wlcli info <iso|wim|esd|swm> [--json]         # sürümler: ISO'yu yerinde okur (UDF + WIM XML)
wlcli ls <iso> [dir] [--json]                 # ISO içinde dizin listesi
wlcli extract <iso> <path-in-iso> <dest>      # ISO'dan dosya çıkar (Ctrl+C iptal, .partial temizlenir)
wlcli plan <changeset.json>                   # ApplyPlan'ı yazdır
wlcli elevated
# Var olanlar (admin)
wlcli mount <wim> <index> <dir> [--readonly]
wlcli unmount <dir> --commit|--discard
wlcli mounts [--json] | cleanup
wlcli packages|features|capabilities <dir> [--json]
wlcli apply <changeset.json> <dir> [--skip-errors]
# Planlanan
wlcli export <wim> <index> <dst> [--compress=max|fast|none|recovery]
wlcli appx|drivers <dir>
wlcli iso <dir> <out.iso>
```
Örnek change set: `tests/integration/fixtures/sample-changeset.json`. Yönetici duman testi: `tools/dism_smoke.ps1` → `build\lab\out\dism-smoke.json`.
Her komut `--json` çıktı verebilir → integration testleri ve AI bunu ayrıştırır.

## 4b. Mount durumları (core/image/dism/MountHealth.h)

DISM `DismGetMountedImageInfo` yalnızca `MountStatus = Ok | NeedsRemount | Invalid` ve `MountMode = ReadWrite | ReadOnly` verir.
Sahada karşılaşılan hataların bir kısmı DISM kaydında görünmez; bu yüzden kendi denetimlerimizi ekleriz.
Durum hesabı saf fonksiyondur (`classifyMount`, unit test'li); `inspectMount` bunu DISM kaydı ve dosya sistemi ile besler, `repairMount` önerilen eylemi uygular.

| Durum | Nasıl anlaşılır | Eylem (`MountAction`) |
|---|---|---|
| Free | DISM kaydı yok, klasör yok/boş | None — bağlanabilir |
| Ok | DISM `Ok` + WIM dosyası duruyor | None — kullanılabilir (açılışta geri yüklenir) |
| NeedsRemount | DISM `NeedsRemount` + WIM duruyor (tipik: yeniden başlatma sonrası WIM filtresi kopar) | Remount → `DismRemountImage` |
| Invalid | DISM `Invalid` | Discard → hive'ları boşalt, discard unmount, `DismCleanupMountpoints` |
| ImageMissing | DISM kaydı var ama WIM silinmiş/taşınmış (commit imkânsız) | Discard |
| Orphaned | DISM kaydı yok ama klasörde dosya var (yarım kalmış mount/unmount; yeni mount 0xC1420116 verir) | ClearFolder → cleanup + klasörü boşalt |

Ek denetimler (`MountCheck`):
- `loadedHives`: `HKLM\SYSTEM\CurrentControlSet\Control\hivelist` içinde dosyası mount altında olan hive'lar (`\Device\HarddiskVolumeN\...` yoluyla karşılaştırılır). Açık hive varken unmount 0xC1420117 verir; her unmount öncesi `unloadHivesUnder` çalışır (SeBackup/SeRestore açılır).
- `windowsImage`: `Windows\System32\config\SOFTWARE` var mı.
- `record->readOnly`: salt okunur mount'ta commit yapılamaz.

Uygulama davranışı:
- Açılış (yönetici): WinLove mount klasörü incelenir → Ok ise kaynağı açar ve "bağlı" gösterir; NeedsRemount ise remount edip aynısını yapar; Invalid/ImageMissing ise atar ve bildirim gösterir; Orphaned ise klasörü temizler.
- Mount öncesi: klasör Free olmalı; Orphaned ise otomatik temizlenir, başka bir durum hata verir.
- CLI: `wlcli mounts` (durum + önerilen eylem + açık hive'lar, `--json`), `wlcli repair <klasör>`.

## 5. Saha notları (öğrendikçe EKLE — AI oturumları buraya yazar)
> Format: `- [tarih] [konu] gözlem → çözüm`
- [2026-09-28] [DISM init] `DismInitialize` log dosyasının klasörünü oluşturmaz; klasör yoksa `0xC0040009 DISMAPI_E_LOGGING_DISABLED` döner. → Klasörü önce oluştur; bu kodu uyarı say (DISM log'suz çalışır).
- [2026-09-28] [DISM header] `dismapi.h/.lib` Windows SDK'da yok, yalnızca ADK'da (`Deployment Tools\SDKs\DismApi`). → Kendi bildirimlerimiz (`core/image/dism/DismApi.h`) + System32 `dismapi.dll` çalışma anında yüklenir (D-017).
- [2026-09-28] [UDF] Win11 25H2 TR ISO: UDF 1.02, tek Type-1 partition map, bölüm başlangıcı sektör 304; `install.wim` LZX, 6 index, 6.72 GB. ISO içinden okuma 72 ms; 700 MB/s çıkarma.
- [2026-09-28] [DISM süreleri] Pro (index 4) salt okunur mount 39 s; features 5.6 s (137, 14 açık), packages 2.2 s (195), capabilities 2.6 s (443, 58 kurulu); **unmount /discard 89.5 s** (salt okunurda bile). → UI'da mount kadar unmount'a da ilerleme + iptal gerekli; testlerde uzun zaman aşımı.
- [2026-09-28] [OfflineRegistry] `RegLoadKey` SeBackup+SeRestore ister ve yönetici gerektirir; hive altında açık tek bir HKEY bile `RegUnLoadKey`'i `ERROR_ACCESS_DENIED`/busy ile düşürür → tüm anahtarlar RAII (`RegKey`), boşaltmadan önce `RegFlushKey`, kısa yeniden deneme. Çevrimdışı SYSTEM'de `CurrentControlSet` yoktur → `Select\Current` okunur. Servis adları `@%SystemRoot%\…,-id` → imaj yoluna çevrilip `SHLoadIndirectString` (P10).
- [2026-09-28] [OOBE sıfırlaması] Çevrimdışı yazılan bazı değerler kurulumda ezilir: OOBE gizlilik sayfası (ConsentStore\location, AdvertisingInfo, Privacy), ilk oturum teması (Themes\Personalize, DWM), ContentDeliveryManager, Win11 görev çubuğu değerleri. → D-026: `SetRegistryFirstLogon` = çevrimdışı + SetupComplete.cmd / Default RunOnce ile yeniden içe aktarım. SetupComplete.cmd OEM anahtarlı etkinleştirmede çalışmaz. Aynı hive iki kez `RegLoadKey` edilemez → Uygula boyunca tek `OfflineRegistry`.
- [2026-09-28] [ACL / junction] `SetNamedSecurityInfoW` junction'ı izler: imajdaki `Documents and Settings` → `C:\Users` gibi bağlantılarda ana makinenin ACL'sini değiştirebilir, üstelik miras ACE'leri ağaca yayar. → Sahiplik/DACL yalnız tanıtıcıyla (`CreateFileW` + `FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS`, `SetSecurityInfo`, `NO_INHERITANCE`, korumalı DACL).
- [2026-09-28] [DISM oturumu] Değiştiren çağrılar `DISMAPI_S_RELOAD_IMAGE_SESSION_REQUIRED` (=1, başarı kodu) dönebilir (ör. SSU sonrası) → oturum kapatılıp yeniden açılmalı; `DismSession::reload()`, Applier her adım sonrası denetler.
- [2026-09-30] [reg import] `reg.exe import`, yazamadığı bir anahtarda durmaz: o girdiyi atlar, dosyanın kalanını uygular, sonda "ERROR: Error accessing the registry" + çıkış kodu 1 verir (yönetici olmadan HKLM + HKCU karışık dosyayla denendi). → Kurulum sonrası .reg dosyasında tek bir korumalı anahtar diğer değerleri düşürmez; çıkış kodu 1 "hiçbiri yazılmadı" demek değildir. .reg içe aktarma sırayla işler, aynı değerin son girdisi kazanır → ertelenmiş dosyaya ekleme (append) = yerine yazma (`DeferredRegistry`).
- [2026-09-30] [.reg türleri] Değersiz `[anahtar]` bölümü de bir iştir (anahtar oluşturur; shellex işleyicileri çoğu zaman yalnız anahtar adıdır) → `RegistryWrite::Kind::CreateKey` (`[+]`). `HKEY_CURRENT_CONFIG` ve `HKU\S-1-5-19/-20` için imajda hive yoktur → yalnız kurulum sonrası içe aktarılır (`isPostSetupOnlyKey`); `HKU\S-1-5-18` = `config\DEFAULT`.
- [2026-09-30] [IMAPI kök dosyası] `IFsiDirectoryItem::AddFile(ad, IStream)` ile imaj köküne bellekten dosya eklenir (`SHCreateMemStream`); klasörden gelen aynı adlı dosya önce `Remove(ad)` ile imajdan çıkarılıyor (Remove'suz AddFile'ın davranışı denenmedi); test: klasördeki eski dosya yerine bellekteki içerik okunuyor. 4096 baytlık sahte `etfsboot.com` ile BIOS önyüklemeli test ISO'su üretilebiliyor (yönetici gerekmez) → ISO üretimi artık unit testte.
- [2026-09-30] [unattend] Parola öğeleri `PlainText=false` iken Base64(UTF-16LE(parola + öğe adı)) — `Password` için ek "Password". `xmlns:wcm` kök öğede bir kez bildirilebilir. windowsPE geçişi yalnız medya kökündeki `autounattend.xml`'den okunur; imaj içi `Windows\Panther\unattend.xml` specialize / oobeSystem'i kapsar.
- [2026-09-30] [batch üretimi] Kullanıcı komutunu betiğe gömmenin sağlam yolu `cmd /d /s /c "<komut>" >>log 2>&1`: `/s` yalnız dış tırnakları atar, komutun kendi tırnakları kalır; yönlendirme bileşik komutun tamamına uygulanır. `if errorlevel N ( … %errorlevel% … )` bloğu önceki satır çalıştıktan sonra ayrıştırıldığı için doğru kodu yazar. robocopy'de hedef `"C:\dir\"` biçiminde verilirse `\"` kaçış sayılır → sondaki `\` atılır; başarı = çıkış kodu < 8. Betik UTF-8 + `chcp 65001` ile ASCII dışı ad / yol çalışıyor (yerelde denendi).
- [2026-09-28] [Unmount] Commit ile ilk denemede 0xC142011D/0xC1420004 gelirse (öncesinde kısmi unmount yoksa) hiçbir şey kaydedilmemiştir; onarım discard eder ama sonuç hata olarak raporlanır ("commit edilemedi").
