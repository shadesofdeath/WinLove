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
- Mount dizini kısa ve boşluksuz: `C:\WinLoveLab\mount\<n>` (geliştirme) / kullanıcı ayarı (üretim).
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
Örnek change set: `tests/integration/fixtures/sample-changeset.json`. Yönetici duman testi: `tools/dism_smoke.ps1` → `C:\WinLoveLab\out\dism-smoke.json`.
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
