# P07 — Bileşenler

> Durum: 🟨 v1 (önceden yüklü uygulamalar) bitti, kullanıcı testi bekliyor (2026-09-28). Tasarım: 04, 04b.

## 1. Amaç
Bağlı imajdan kaldırılacak bileşenleri seçmek; seçimler RemoveAppx işlemleri olarak kuyruğa girer (P05'te uygulanır).

## 2. Kapsam (v1) ve dürüst sınırlar
- **Var:** önceden yüklü (provisioned) AppX uygulamaları — `DismGetProvisionedAppxPackages` /
  `DismRemoveProvisionedAppxPackage` (desteklenen, güvenilir yol). Boyut: `Program Files\WindowsApps\<Ad>_*`
  klasörleri, yedekleme semantiğiyle okunur (ACL yöneticiye kapalı; sahiplik değiştirilmez).
- **Yok (bilerek):** CBS sistem paketleri (Defender, WinRE, …). Win11'de bunların çoğu "kalıcı" işaretli, DISM ile
  kaldırılamaz (0x800F0825). NTLite dosya düzeyinde söküm yapar; bu ayrı ve riskli bir motordur, ileride ayrı bir
  karar (DECISIONS) ile ele alınacak. Features on Demand kaldırma P04 Özellikler'de.
- Uygulama kaldırma imajda geri alınamaz (Store'dan kullanıcı yeniden kurabilir, imaja geri eklenmez) → Inspector
  "Geri alınabilir: Hayır".

## 3. Katalog
`resources/catalog/appx.json` (exe'ye gömülü, IDR_CATALOG_APPX): grup (Xbox ve Oyun, Medya, Ofis, İletişim,
Haberler/Hava/Arama, Araçlar, Codec, Sistem, Çalışma zamanları, Diğer), Türkçe/İngilizce ad, risk
(Store, App Installer, Windows Güvenliği arayüzü, VCLibs/UI.Xaml/.NET Native/App SDK = yüksek), notlar.
Eşleşme paket kimliğinin önekiyle (en uzun kazanır). Bilinmeyen uygulama → "Diğer", orta risk.

## 4. Ekran
- Başlık: Preset yükle (ChangeSet JSON → kuyruğa ekler), Tümünü daralt/genişlet.
- Araç çubuğu: arama (`/`, Ctrl+F; eşleşme vurgusu, yalnız eşleşen dallar, "n sonuç · Esc temizler"), Kategori,
  Risk, "Yalnızca seçili"; sağda "Tahmini kazanç X · n bileşen kuyrukta".
- Yüksek riskli seçim varsa uyarı InfoBar'ı (katalog notu).
- Ağaç (TableView üstünde iki seviye): grup satırı (chevron, üç durumlu kutu, "n öğe", toplam boyut) → uygulama
  (kutu, ad, risk kare + etiket, boyut). Space/Enter/çift tık değiştirir; chevron açar/kapatır.
- Inspector (sağ sütun 280): ad + kimlik (mono), Kategori, Boyut, Risk, Durum, Geri alınabilir; UYUMLULUK notu;
  İÇERİK (paket tam adı); "Kuyruğa ekle / Kuyruktan çıkar".
- CLI: `wlcli appx <mount> [--json]`.

## 5. Kabul
- [ ] Bağlı sürümde Bileşenler → "Bileşenler okunuyor" → gruplar, Türkçe adlar, boyutlar.
- [ ] Grup kutusu → hepsi; tekil kutu → kısmi; rozet ve "Uygula · n" güncel.
- [ ] Store/Windows Güvenliği seçilince uyarı; Uygula'da onay dialogunda listelenir.
- [ ] Uygula → uygulamalar imajdan kalkar (yeni kullanıcıda yok).
