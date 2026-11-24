# P17 — Hakkında

> Durum: 🟨 geliştirme bitti, kullanıcı testi bekliyor (2026-10-05, D-072). Tasarım: 20 (+ bayrak, söz, bağlantılar). Erişim: gezinme çubuğu, `F1`.

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

## 3b. D-072 eklemeleri
- Türk bayrağı (kanundaki oran ve renklerle çizilir) + "Türkiye'de sevgiyle yapıldı" + söz paragrafı (ölçülerek sarılır).
- **Bağlantılar**: Geliştirici `github.com/shadesofdeath`, Proje `github.com/shadesofdeath/WinLove`, Sorun bildir (`/issues`),
  Lisans GNU GPL v3.0 (`/blob/main/LICENSE`) — tarayıcıda açılır.
- Sürüm satırı `WL_VERSION_LABEL` ("1.0 Alpha").

## 3c. Güncellemeler kartı (D-140, 2026-11-11)
- Kahraman kartının altında: durumun ikonu, başlık, tek satır olgu; sağda "Şimdi denetle" (Secondary) ve yeni sürüm
  varken sayfanın tek dolu düğmesi **Sürüm sayfasını aç** (tarayıcıda, yalnız `https://github.com/shadesofdeath/WinLove/releases/…`);
  altında sürüm notlarının ilk satırları. Durumlar ve ağa gidenin tam tanımı D-140'ta (tek HTTPS GET, hiçbir şey
  indirilmez). Açılış denetimi pencereli çalışmada, günde en çok bir kez; ayar P16'da.
- Render: `--page=about --demo-update[=available|current|failed|checking]`. Pencerede tıklanmadı.
- **D-145 (2026-11-11) — Yükle ve Değişiklikler:** yeni sürüm varken kartın düğmeleri **Yükle** (dolu) · **Değişiklikleri
  gör** · Şimdi denetle; güncelken de Değişiklikleri gör. "Sürüm sayfasını aç" yalnız yüklenemeyen durumda **İndirme
  sayfasını aç** olarak (ek yok, klasör yazılamıyor, denetim / takas / başlatma hatası; diğer hatalarda **Yeniden dene**).
  Kartın durumları: indiriliyor (ölçer + %, Vazgeç) → denetleniyor → hazır (**Yeniden başlat**) → yeni örnekte "WinLove
  güncellendi: 1.3.0 Beta → x"; hata düz sözlerle. Yükle, imaj bağlıyken ya da Uygula / ISO / motor işi / indirme sürerken
  reddeder. İndirilen ek boyut, GitHub'ın SHA-256 `digest`'i, PE (x64, GUI) ve sürümle denetlenir; exe'nin yanına `.new`,
  takas `.old` ile, başlatılamazsa geri alınır (ayrıntı D-145). Yeniden başlatma UAC istemez (`--no-elevate` yönetici
  değilse). Bildirim tostu iki düğmeli (Yükle · Değişiklikleri gör); durum çubuğu bölümü bir menü açar (Yükle ·
  Değişiklikleri gör · Hakkında'da göster).
- **Değişiklikler penceresi:** ayrı, sahipli, yeniden boyutlanan pencere ("WinLove — Değişiklikler"; Esc kapatır, tek
  örnek); çalışan sürümden yeni yayınlar en yenisi önce, "Önceki sürümler" altında son 3 eski yayın; her biri kartta, gövde
  Markdown (başlıklar, listeler, alıntı, kod, tablo, bağlantılar — yalnız `http(s)://` tarayıcıda, `#çapa` içeride; görsel
  yalnız alt metni). Gövdeler denetimin tek yanıtından: pencere ek istek yapmaz. Render `--demo-changelog[=<yanıt.json>]`,
  `--demo-update=downloading|verifying|ready|install-failed|updated`.
- **Söz paragrafı (2026-11-24, 1.4.0 Beta yayını; STATUS'taki (a) seçeneği):** "kimseye bir şey göndermez" yerine
  "yalnız günde en çok bir kez GitHub'daki sürüm listesine bakar (Uygulama ayarları'ndan kapatılabilir)" (`about.freeBody`,
  README de). Denetim varsayılan açık kaldı.

## 4. Bu sürümde olmayanlar (tasarım 20'de var)
- ~~**Güncellemeleri denetle**: güncelleme servisi / sürüm kanalı yok; çalışmayan bir düğme konmadı.~~ — 2026-11-11'den
  beri var (§3c, D-140); D-145'ten beri yalnız kullanıcı "Yükle"ye basınca indirir ve yükler.
- Fontlar satırı ve lisans metni 2026-11-11'den beri başlık markasının yazı tipini de sayar (IBM Plex Sans SemiBold, OFL
  1.1; D-147).
- Üçüncü taraf satırı tasarımdaki örnek listeyi değil, depoda gerçekten bulunanları sayar.

## 5. Kabul
- [ ] `F1` Hakkında'yı açar; sürüm / build / mimari doğru.
- [ ] DISM satırı sistemdeki `dismapi.dll` sürümünü ve yolunu gösterir.
- [ ] Çalışma klasörü Ayarlar'dan değiştirilince satır değişir.
- [ ] Lisanslar dialogu açılır, Tamam / Esc ile kapanır.
- [ ] Log klasörünü aç → Explorer log klasöründe açılır.
- [ ] D-145: Değişiklikleri gör → ayrı pencere açılır, Markdown düzgün, bağlantı tarayıcıda, Esc kapatır, ikinci tıklama
      aynı pencereyi öne getirir. Yeni bir yayın çıkınca Yükle → indiriliyor → hazır → Yeniden başlat → yeni sürüm açılır,
      "WinLove güncellendi" bildirimi, `.old` silinir. Çekirdek adımlar wlcli lab'ında kopyalarla kanıtlı; uygulamanın
      içinden ve pencerede hiç denenmedi (henüz daha yeni bir yayın yok).

## 6. Görsel doğrulama
Render: `--page=about` (dark) ve lisans dialogu (light). İşaret / başlık / satır ölçüleri tasarım 20 ile örtüşüyor.
