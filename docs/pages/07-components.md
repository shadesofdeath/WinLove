# P07 — Bileşenler

> Durum: 🟨 v2 (uygulamalar + sistem bileşenleri + depo temizliği) geliştirme bitti, kullanıcı testi bekliyor
> (2026-09-30). Tasarım: 04, 04b. Karar: D-031.

## 1. Amaç
Bağlı imajdan kaldırılacak bileşenleri seçmek; seçimler kuyruğa girer (P05'te uygulanır):
`RemoveAppx` (uygulama), `RemoveComponent` (sistem bileşeni), `CleanupImage` (bileşen deposu temizliği).

## 2. Kapsam ve dürüst sınırlar
- **Önceden yüklü (provisioned) AppX uygulamaları** — `DismGetProvisionedAppxPackages` /
  `DismRemoveProvisionedAppxPackage` (desteklenen, güvenilir yol). Boyut: `Program Files\WindowsApps\<Ad>_*`
  klasörleri, yedekleme semantiğiyle okunur (ACL yöneticiye kapalı; sahiplik değiştirilmez).
- **DISM'in kaldırtmadığı uygulamalar**: `Microsoft.SecHealthUI` ve `Microsoft.DesktopAppInstaller` 24H2+
  imajlarda (ikincisi Windows 10 22H2'de de) DISM tarafından reddediliyor (0x80073CFA). Uygula bu kodu alınca
  uygulamayı WinLove'un kendi kaldırmasıyla çıkarır (D-038: DISM'in diğer uygulamalarda yaptığı dosya + kayıt
  değişikliklerinin aynısı; 25H2 imajında `tools\lab_appx.ps1` ile kanıtlandı). Listede diğerleri gibi seçilirler
  (risk Yüksek). Eski kilit (simge, "Kaldırılamaz", pasif kutu) kaldırıldı.
- **Sistem bileşenleri** (`resources/catalog/components.json`, yalnız imajda bulunanlar listelenir):

  | Bileşen | Ne silinir | Risk |
  |---|---|---|
  | Microsoft Edge | `Program Files (x86)\Microsoft\Edge` + EdgeUpdate istemci kaydı, kaldırma girdisi, Active Setup | Orta |
  | Edge WebView2 Çalışma Zamanı | `…\Microsoft\EdgeWebView` + istemci kaydı, kaldırma girdisi | Yüksek |
  | Edge Güncelleyici ve ortak dosyalar | `…\EdgeUpdate`, `…\EdgeCore` + `EdgeUpdate` anahtarı, `edgeupdate` / `edgeupdatem` servisleri | Yüksek |
  | OneDrive kurulumu | gizli CBS paketi `Microsoft-Windows-OneDrive-Setup-(WOW64-)Package` + `System32\OneDriveSetup.exe` (Windows 10: `SysWOW64\OneDriveSetup.exe` + varsayılan profilin Başlat menüsündeki `OneDrive.lnk`) + varsayılan profilde `Run\OneDriveSetup` | Düşük |
  | Yeni Outlook'un kendiliğinden kurulması (**her imajda sunulur**, D-039) | `UScheduler_Oobe\OutlookUpdate` anahtarı (Windows 11) + `ProgramData\USOPrivate\ExpeditedAppRegistrations\MS_Outlook` (Windows 10); `BlockedOobeUpdaters = ["MS_Outlook"]`, `UScheduler\OutlookUpdate\workCompleted = 1`, `Deprovisioned\Microsoft.OutlookForWindows_8wekyb3d8bbwe` | Düşük |
  | Windows Kurtarma Ortamı | `System32\Recovery\Winre.wim` | Yüksek |

  Yol ve adlar Windows 11 25H2 (26200.8037) imajında doğrulandı. Gizli paket önce kayıt defterinde açılır
  (`Visibility = 1`, `Owners` silinir), sonra DISM ile kaldırılır; DISM reddederse dosya ve kayıtlar yine silinir,
  logda uyarı kalır (WinSxS kopyası durur).
- **Temizlik → Bileşen deposu temizliği (ResetBase)**: `dism.exe /Cleanup-Image /StartComponentCleanup /ResetBase`.
  Güncellemelerden hemen sonra, kayıt defteri / servis yazımlarından önce çalışır (kendi aşaması). Asıl kazanç bu
  çalıştırmada güncelleme eklendiyse olur (5–20 dk); Microsoft'un dokunulmamış imajında temizlenecek bir şey yoktur
  (lab: 13 sn). Aynı çalıştırmada bir özellik açıldıysa (ör. .NET 3.5) DISM reddedebilir (0x800F0806): adım
  başarısız görünür, imaj etkilenmez. Başladıktan sonra durdurulamaz.
- **Yok (bilerek):**
  - **Defender kaldırma.** 24H2+ imajlarda Defender ayrı bir paket değil; DISM ile sökülemez. Kapatmak için:
    Servisler (WinDefend, Sense…) ve Ayarlar / Tweaks.
  - 865 gizli paket ailesinin ham listesi (çoğu çekirdek; sonucu öngörülemez). Bunun yerine D-059: taranıp tek tek
    denenmiş, gerçekten isteğe bağlı paketler katalogda (aşağıda); gerekli olanlar (ağ / depolama sürücüleri, MTP,
    BitLocker, gpedit) bilerek yok.
  - Media Player, IE, PowerShell ISE, WMIC, VBScript, Hello Face, Wi-Fi / Ethernet sürücü paketleri, System32'deki
    WebView platformu: bunlar görünür FoD → **P04 Özellikler**'de.
- Kaldırmalar imajda geri alınamaz → Inspector "Geri alınabilir: Hayır". Windows Update bazılarını (Edge) geri
  getirebilir; paketi sökülmüş bir bileşen sonraki toplu güncellemede geri gelebilir ya da güncelleme hata verebilir.
- Edge / EdgeCore / WebView2 klasörleri aynı dosyaları içerir (WIM tek kopya saklar): "Boyut" sütunu açılmış boyuttur;
  ISO ancak üçü birden kaldırılınca belirgin küçülür.

- **Derin kaldırma (D-060):** "Eski Donanım Sürücüleri" grubu — modem, teyp, disket, FireWire, PCMCIA, POS sürücüleri imajdan
  ve bileşen deposundan birlikte çıkar (ScanHealth temiz). Uygula'da güncellemelerden sonra çalışır (DeepRemove aşaması);
  sonradan eklenen toplu güncelleme kurulamaz — sayfa ve notlar bunu söyler.
- **Paket düzeyinde bileşenler (D-059):** Gizlilik, Güvenlik, Multimedya, Yazı Tipleri, Kurumsal ve Diğer grupları;
  tarif yalnız CBS paket aileleri taşır. İmajda var mı ve boyutu `ComponentStoreIndex` ile okunur (paket kurulu mu,
  ağacının tek sahipli WinSxS baytı). 25H2 Pro ölçümleri: Defender tanımları 483 MB, Japonca 81 / Basit Çince 56 /
  Geleneksel Çince 26 / Korece 17 MB yazı tipleri, App-V 24, Fotoğraf Görüntüleyici 19, UE-V 15, DLNA 12, telemetri
  11, Edge DevTools 11 MB … Hepsi birlikte ISO'da ~690 MB.

## 2b. Windows'un kendiliğinden kurdukları (D-070)
Grup `autoinstall`: OneDrive kurulumu, yeni Outlook, Teams, Dev Home, Cihazlar Arası Deneyim, Microsoft 365 Copilot, Copilot.
Zamanlayıcı kanalları (`UScheduler_Oobe\<görev>`) silinir ve tamamlanmış sayılır, imajdaki yer tutucu / hazır paket kaldırılır,
aile "istenmiyor" işaretlenir. Hepsi her imajda sunulur (`always`). VM'de ölçüldü (Pro ve Home): hiçbiri kurulmadı.

## 3. Kataloglar
- `resources/catalog/appx.json` (IDR_CATALOG_APPX): grup, TR / EN ad, risk, notlar; eşleşme paket kimliğinin önekiyle
  (en uzun kazanır). Bilinmeyen uygulama → "Diğer", orta risk.
- `resources/catalog/components.json` (IDR_CATALOG_COMPONENTS): gruplar ("Sistem Bileşenleri", "Temizlik") ve tarifler
  (`packages`, `paths`, `registry`; `kind: cleanup`). Geçersiz tarif (kök klasöre yakın yol, `..`, korumalı klasör,
  bilinmeyen hive) yüklenirken atlanır ve loglanır. Yeni bileşen = bu dosyaya bir girdi (kod değişmez).

## 4. Motor
- `core/image/SystemComponents`: `ComponentRecipe` (JSON ↔), `validateComponentRecipe`, `resolveImagePath` (yol
  üzerindeki junction reddedilir), `probeComponent` (var mı, kaç bayt), `readCbsPackages`, `cbsRemovalOrder` (dil
  paketleri önce, kurulu olan staged kalıntıdan önce), `unlockCbsPackage`, `removeComponent`.
- `core/image/dism/StoreCleanup`: `cleanupComponentStore` (dism.exe alt süreci, çıktıdan yüzde).
- Hive düzenlenirken / dism.exe çalışırken DISM oturumu kapatılır (`DismSession::suspend` → `reload`).
- CLI: `wlcli appx <mount>`, `wlcli cbs <mount> [metin]`, `wlcli component <mount> <tarif.json> [--remove]`,
  `wlcli store-cleanup <mount> [--resetbase]`. Tarif örnekleri: `tests/integration/fixtures/recipe-*.json`.
- Gerçek imajda deneme (yönetici, VM'siz, kaydetmeden): `tools\lab_components.ps1` [`-Cleanup`].
  2026-09-30'da 25H2 Pro kopyasında geçti: OneDrive'ın 5 gizli paketi DISM ile kaldırıldı, Edge klasörü silindi,
  imaj sonrasında servislenebilir; `-Cleanup` ile depo temizliği de çalıştı (13 sn, dokunulmamış imajda temizlenecek
  bir şey yok). Güncelleme eklenmiş imajda temizlik ve commit + VM kurulumu henüz denenmedi.

## 5. Ekran
- Başlık: Preset yükle, Tümünü daralt / genişlet.
- Araç çubuğu: arama (`/`, Ctrl+F), Kategori, Risk, "Yalnızca seçili"; sağda "Tahmini kazanç X · n bileşen kuyrukta".
- Yüksek riskli seçim varsa uyarı InfoBar'ı (katalog notu).
- Ağaç: önce **Sistem Bileşenleri** ve **Temizlik** grupları, sonra uygulama grupları. Grup satırı (chevron, üç
  durumlu kutu, "n öğe", toplam boyut) → öğe (kutu, ad, risk, boyut; temizliğin boyutu "—": önceden bilinmez).
- Inspector: ad + teknik satır, Kategori, Boyut, Risk, Durum, Geri alınabilir; UYUMLULUK notu; İÇERİK (paket tam adı;
  sistem bileşeninde paket aileleri + yollar); "Kuyruğa ekle / Kuyruktan çıkar".
- Sistem bileşenleri uygulama listesi okunduktan hemen sonra ayrı bir kısa okumayla gelir (yol var mı + boyut).

## 6. Kabul
- [ ] Bağlı sürümde Bileşenler → en üstte Sistem Bileşenleri (Edge, WebView2, Edge Güncelleyici, OneDrive, WinRE) ve
      Temizlik; altında uygulama grupları, Türkçe adlar, boyutlar.
- [ ] Grup kutusu → hepsi; tekil kutu → kısmi; rozet ve "Uygula · n" güncel.
- [ ] Yüksek riskli seçimde uyarı; Uygula'da onay dialogunda adıyla listelenir.
- [ ] Windows 10 imajında "OneDrive kurulumu" listeleniyor; o ve "Yeni Outlook'un kendiliğinden kurulması" seçili
      Uygula → internetli VM kurulumunda (OOBE sonrası) OneDrive de Outlook da kurulmuyor.
- [ ] Uygula → log'da `[cbs] … package(s) unlocked`, `removed Microsoft-Windows-OneDrive-Setup-Package…`; imajda
      `OneDriveSetup.exe` ve Edge klasörü yok.
- [ ] Temizlik seçiliyse "Bileşen deposunu temizle" aşaması güncellemelerden sonra çalışır; bir toplu güncelleme
      (LCU) eklenen çalıştırmada install.wim (yeniden paketlemeyle, P06) temizliksiz hâline göre küçülür.
- [ ] **VM:** kurulan sistemde Edge yok / OneDrive kurulmuyor / (WinRE kaldırıldıysa) `reagentc /info` devre dışı;
      Windows Update çalışıyor.
