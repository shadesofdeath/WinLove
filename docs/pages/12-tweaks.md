# P12 — Ayarlar / Tweaks

> Durum: 🟨 geliştirme bitti, kullanıcı testi bekliyor (2026-09-30). Tasarım: 10.

## 1. Amaç
Windows ayarlarını "ayar" olarak göstermek (Reklam kimliği: açık/kapalı, Telemetri seviyesi: …) ve seçimi bağlı
imajın kayıt defteri / servis değişikliklerine çevirmek (kuyruk → P05 Uygula). P11 aynı değerleri "tweak listesi"
olarak gösterir; P12 aynı işlemleri form olarak sunar.

## 2. Ekran
- Başlık eylemi: **Önerilenleri uygula** (katalogda önerilen seçeneği olan her ayarı toplu olarak o seçeneğe alır;
  kaç ayarın değiştiği toast ile bildirilir).
- Sekmeler: Gizlilik · Performans · Görünüm · Gezgin · Başlat menüsü (←/→).
- Bölüm başlığı (büyük harf, alt çizgi) + 32 px satırlar: 240 px etiket sütunu, sonra kontrol:
  **toggle** (yanında ipucu metni), **dropdown** (280 px), **radio** grubu.
- İçerik pencereye sığmazsa form kayar (tekerlek, kaydırma çubuğu; klavye odağı görünür alana getirilir).
- Bağlı imaj yoksa boş durum + "İmajlar'a git".
- Nav rozeti: varsayılandan farklı ayar sayısı.

## 3. Veri ve model
- Katalog: `resources/catalog/settings.json` (gömülü `IDR_CATALOG_SETTINGS`), `app/catalog/ImageSettingsCatalog`.
  Sekme → bölüm → ayar → seçenekler. Bir seçenek = kayıt defteri yazımları (+ servis başlangıçları).
  Tam olarak bir seçenek "Windows varsayılanı"dır: **hiçbir şey yazmaz**.
- Durum ayrı tutulmaz; **kuyruktan türetilir** (`ImageSettingsController::current`): bir seçeneğin tüm işlemleri
  kendi değerleriyle kuyruktaysa o seçenek seçilidir, yoksa varsayılan. Böylece P11'de işaretlenen bir tweak P12'de,
  P12'de seçilen bir ayar P11'de ve presetlerde kendiliğinden görünür.
- Seçim: ayarın diğer seçeneklerinin kuyruktaki işlemleri çıkarılır, yeni seçeneğinkiler eklenir.
- `"apply": "firstLogon"` ayarları `SetRegistryFirstLogon` (D-026), diğerleri `SetRegistryValue`; servisler
  `SetServiceStart` (P10 ile aynı işlem → Servisler sayfasında da görünür).

## 4. Sınırlar
- İmajdaki mevcut değer okunmuyor: form Windows varsayılanını gösterir (P11 ile aynı sınır). "Varsayılan" seçeneği
  değeri değiştirmez, varsayılana *geri yazmaz*.
- HKCU ayarları Default profile yazılır (kurulumdan sonra açılan hesaplar).
- "Güvenlik" telemetri seviyesi yalnız Enterprise / Education'da etkilidir (diğerlerinde "Gerekli" gibi davranır).

## 5. Kabul
- [ ] Ayar değiştir → kuyrukta ilgili işlemler, nav rozeti artar; varsayılana dönünce işlemler çıkar.
- [ ] P11'de karşılığı olan tweak işaretli görünür (ve tersi).
- [ ] "Önerilenleri uygula" → önerilen ayarlar seçilir; ikinci kez basınca "hepsi zaten seçili".
- [ ] Uygula → değerler hive'da (`wlcli reg` / yeniden bağlama ile), servis başlangıçları Servisler sayfasında.
- [ ] Klavye: Tab ile kontroller, ←/→ sekme ve radio, Space toggle, Enter dropdown.

## 6. Görsel doğrulama
Render: `--demo-tweaks` (tasarım 10'daki seçimlerle); `tools/compare_design.py 10-tweaks -- --demo-tweaks`.
Sekme çubuğu, bölüm başlığı, çizgi ve satır dikey ölçüleri tasarımla piksel olarak örtüşüyor (2026-09-30; dark,
light/EN, 1280×520'de kaydırma, tıklama ile toggle / radio / dropdown). `TabBar` seçili sekme zemini (bg.raised)
ve 8 px içerden alt çizgi tasarım 10 / 16'ya göre düzeltildi.

## 7. Bilinçli sapmalar
- İçerik tasarımdaki örnek 7 satır yerine gerçek katalog (39 ayar); "Cortana ve arama" bölümü "Arama" oldu
  (Windows 11'de Cortana yok), konumun üçüncü seçeneği "Uygulama bazlı" yerine "Yalnız sistem" (HKCU izni kapalı).
- Her satırın yanında durumu yazar (D-032): toggle'da "Açık · Windows varsayılanı" / "Kapalı · değiştirilecek"
  (+ katalog ipucu), dropdown / radio'da yalnız işaret; değişenler vurgu renginde. Toggle, özelliğin kurulan
  Windows'taki durumudur (açık = özellik açık kalır) — sayfa açıklaması da bunu söyler.
- Sayfa içeriği diğer sayfalar gibi x=216'da başlar (tasarım 217).
