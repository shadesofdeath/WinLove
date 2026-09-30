# P17 — Hakkında

> Durum: 🟨 geliştirme bitti, kullanıcı testi bekliyor (2026-09-30). Tasarım: 20. Erişim: `F1`.

## 1. Amaç
Hangi WinLove sürümünün, hangi DISM ile çalıştığını göstermek; paketlenen bileşenlerin lisanslarına ve log
klasörüne tek tıkla ulaşmak. Sayfada kuyruğa eklenen ya da uygulanan bir şey yok.

## 2. Ekran
- Marka işareti (24 px, vurgu rengi) + **WinLove** + sürüm satırı: `Sürüm 0.1.0 (build 2026.09.30) · x64`.
  Sürüm `WL_VERSION_STRING`'den, tarih derleme tarihinden (`__DATE__`), mimari derleme hedefinden gelir.
- Satırlar (etiket 160 px, satır 32 px):
  - **DISM** — yüklenen `dismapi.dll` sürümü · yolu (mono). Sürüm okunamazsa yalnız yol.
  - **Çalışma dizini** — `AppSettings::workRoot` (mono); ayar değişince satır güncellenir.
  - **Fontlar** — paketlenen yazı tipleri ve lisansları.
  - **Üçüncü taraf** — gerçekten vendored olanlar (`third_party/`: doctest, nlohmann/json, pugixml).
- Düğmeler: **Lisanslar** (paketlenenlerin lisans özeti, dialog) · **Log klasörünü aç** (Explorer'da
  `log::defaultDirectory()`).

## 3. Model
- `app/SystemInfo`: `dismLibraryPath()`, `dismLibraryVersion()` (dosya sürüm kaynağı), `buildDate()`,
  `buildArchitecture()`. Uygulama Ayarları'ndaki DISM satırı da buradan okur (tek kaynak).
- `AboutPage` yalnız `Change::Settings`'i dinler.

## 4. Bu sürümde olmayanlar (tasarım 20'de var)
- **Lisans satırı**: depoda proje lisans dosyası yok; olmayan bir lisansı yazmak yanlış bilgi olur. Lisans
  seçilince satır eklenir.
- **Güncellemeleri denetle**: güncelleme servisi / sürüm kanalı yok; çalışmayan bir düğme konmadı.
- Üçüncü taraf satırı tasarımdaki örnek listeyi değil, depoda gerçekten bulunanları sayar.

## 5. Kabul
- [ ] `F1` Hakkında'yı açar; sürüm / build / mimari doğru.
- [ ] DISM satırı sistemdeki `dismapi.dll` sürümünü ve yolunu gösterir.
- [ ] Çalışma klasörü Ayarlar'dan değiştirilince satır değişir.
- [ ] Lisanslar dialogu açılır, Tamam / Esc ile kapanır.
- [ ] Log klasörünü aç → Explorer log klasöründe açılır.

## 6. Görsel doğrulama
Render: `--page=about` (dark) ve lisans dialogu (light). İşaret / başlık / satır ölçüleri tasarım 20 ile örtüşüyor.
