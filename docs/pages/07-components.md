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
- **Sistem bileşenleri** (`resources/catalog/components.json`, yalnız imajda bulunanlar listelenir):

  | Bileşen | Ne silinir | Risk |
  |---|---|---|
  | Microsoft Edge | `Program Files (x86)\Microsoft\Edge` + EdgeUpdate istemci kaydı, kaldırma girdisi, Active Setup | Orta |
  | Edge WebView2 Çalışma Zamanı | `…\Microsoft\EdgeWebView` + istemci kaydı, kaldırma girdisi | Yüksek |
  | Edge Güncelleyici ve ortak dosyalar | `…\EdgeUpdate`, `…\EdgeCore` + `EdgeUpdate` anahtarı, `edgeupdate` / `edgeupdatem` servisleri | Yüksek |
  | OneDrive kurulumu | gizli CBS paketi `Microsoft-Windows-OneDrive-Setup-(WOW64-)Package` + `System32\OneDriveSetup.exe` + varsayılan profilde `Run\OneDriveSetup` | Düşük |
  | Windows Kurtarma Ortamı | `System32\Recovery\Winre.wim` | Yüksek |

  Yol ve adlar Windows 11 25H2 (26200.8037) imajında doğrulandı. Gizli paket önce kayıt defterinde açılır
  (`Visibility = 1`, `Owners` silinir), sonra DISM ile kaldırılır; DISM reddederse dosya ve kayıtlar yine silinir,
  logda uyarı kalır (WinSxS kopyası durur).
- **Temizlik → Bileşen deposu temizliği (ResetBase)**: `dism.exe /Cleanup-Image /StartComponentCleanup /ResetBase`.
  Kuyrukta nerede olursa olsun **ilk** adım olarak çalışır; 5–20 dk sürer, başladıktan sonra durdurulamaz.
- **Yok (bilerek):**
  - **Defender kaldırma.** 24H2+ imajlarda Defender ayrı bir paket değil; DISM ile sökülemez. Kapatmak için:
    Servisler (WinDefend, Sense…) ve Ayarlar / Tweaks.
  - 865 gizli paket ailesinin ham listesi (çoğu çekirdek; sonucu öngörülemez).
  - Media Player, IE, PowerShell ISE, WMIC, VBScript, Hello Face, Wi-Fi / Ethernet sürücü paketleri, System32'deki
    WebView platformu: bunlar görünür FoD → **P04 Özellikler**'de.
- Kaldırmalar imajda geri alınamaz → Inspector "Geri alınabilir: Hayır". Windows Update bazılarını (Edge) geri
  getirebilir; paketi sökülmüş bir bileşen sonraki toplu güncellemede geri gelebilir ya da güncelleme hata verebilir.
- Edge / EdgeCore / WebView2 klasörleri aynı dosyaları içerir (WIM tek kopya saklar): "Boyut" sütunu açılmış boyuttur;
  ISO ancak üçü birden kaldırılınca belirgin küçülür.

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
  imaj sonrasında servislenebilir. Depo temizliği ve commit + VM kurulumu henüz denenmedi.

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
- [ ] Uygula → log'da `[cbs] … package(s) unlocked`, `removed Microsoft-Windows-OneDrive-Setup-Package…`; imajda
      `OneDriveSetup.exe` ve Edge klasörü yok.
- [ ] Temizlik seçiliyse ilk adım olarak çalışır, ilerleme yüzdesi akar; bitince install.wim (yeniden paketlemeyle,
      P06) küçülür.
- [ ] **VM:** kurulan sistemde Edge yok / OneDrive kurulmuyor / (WinRE kaldırıldıysa) `reagentc /info` devre dışı;
      Windows Update çalışıyor.
