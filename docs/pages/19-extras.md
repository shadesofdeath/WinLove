# P19 — Ek sayfalar: Uygulamalar, Diller, Görevler, Hosts, Dosyalar (+ Uygula'da diğer sürümler)

**Durum:** 🟨 geliştirme bitti, motor kanıtlandı (`tools\lab_features.ps1`, 2026-10-01 ALL PASSED), kullanıcı testi bekliyor ·
**Tasarım:** handoff'ta karşılığı yok — mevcut liste sayfalarının dili (P07 / P09 / P10 düzeni: başlık + eylemler,
sekme, tablo, sağda ayrıntı paneli, `PageBits` risk / ayrıntı satırları). Kararlar: D-048 … D-055.

## 1. Amaç
NTLite'ın kalan temel alanları: imaja uygulama ve dil eklemek, bölge ayarları, varsayılan uygulamalar, kurulumdan sonra
zamanlanmış görevleri kapatmak, hosts / DNS, imaja dosya koymak, imajdaki sürücüleri yönetmek ve aynı kuyruğu bir
WIM'in birden çok sürümüne uygulamak.

## 2. Sayfalar ve bölgeler
| Sayfa (sol menü) | Bölgeler |
|---|---|
| Uygulamalar (Bileşenler'in altı) | Sekme 1 "Uygulama yükle": paket tablosu (ad, yayıncı, sürüm, mimari, bağımlılık durumu), sürükle-bırak; ayrıntı: bağımlılıklar, lisans. Sekme 2 "Varsayılan uygulamalar": tarayıcı düğmeleri, ilişkilendirme tablosu, XML içe aktar / bu bilgisayardan al |
| Diller (Güncellemeler'in altı) | Üstte imajın dilleri + beş açılır menü (arayüz dili, sistem yereli, kullanıcı yereli, klavye, saat dilimi); altta "Klasör tara" ile bulunan dil paketleri / özellikleri |
| Sürücüler › İmajdaki sürücüler | oemN.inf tablosu (sınıf, sağlayıcı, sürüm, tarih, imza, önyükleme kritik), kaldır işareti; "Bu bilgisayarın sürücüleri" |
| Görevler (Servisler'in altı) | Kategori sekmeleri (telemetri, bakım, özellikler, güncelleme), görev tablosu (durum: imajda / kuyrukta), "Önerilenleri kapat", "Görev ekle" |
| Hosts (Ayarlar / Tweaks'in altı) | Hazır listeler (açık / kapalı, girdi sayısı, imajda), özel girdiler (dosyadan / metinden); DNS ayarları Ayarlar › Ağ'da |
| Dosyalar (Kurulum Sonrası'nın altı) | Kuyruktaki kopyalar (kaynak → imajdaki yer, boyut, risk); "İmajda nereye?" dialogu |
| Uygula › özet | "Diğer sürümlere de uygula" onay kutuları; çalışırken "Sürüm k / n · ad"; bitince sürüm başına sonuç |

## 3. Veri
| Veri | Kaynak | Ne zaman | Önbellek |
|---|---|---|---|
| Görev kataloğu, hosts listeleri | `resources/catalog/{tasks,hosts}.json` (RCDATA) | açılışta | uygulama ömrü |
| İmajdaki görevler / hosts bölümleri | `tasks.cmd` / `hosts` dosyası okunur | bağlanınca (`ImageValues`) | bağlama başına |
| İmajdaki sürücüler | `DismGetDrivers` | sekme açılınca (motor iş parçacığı) | `ImageDrivers`, bağlama başına |
| Dil / bölge | `dism /Get-Intl` | sayfa açılınca | `ImageIntl`, bağlama başına |
| Paket bilgisi | AppxPackaging COM | eklenince (okuyucu iş parçacığı) | kuyruk işleminin değerinde (JSON) |
| Seçenek listeleri (yerel, klavye, saat dilimi) | bu bilgisayar | ilk açılışta | uygulama ömrü |

## 4. Aksiyonlar
| Aksiyon | Etki | Admin? | Geri alınabilir? |
|---|---|---|---|
| Uygulama ekle | `AddAppx` (JSON) | Uygula'da | kuyruktan çıkar |
| İlişkilendirme / tarayıcı | tek `SetDefaultApps` (XML) | Uygula'da; "bu bilgisayardan al" anında | kuyruktan çıkar |
| Dil ayarı / dil paketi | `SetIntl` (JSON) / `AddPackage` + `language` | Uygula'da | kuyruktan çıkar |
| Sürücü kaldır | `RemoveDriver` (önyükleme kritikse yüksek risk) | Uygula'da | kuyruktan çıkar |
| Bu bilgisayarın sürücüleri | `pnputil /export-driver` anında → klasör ekleme | evet | — |
| Görev kapat / aç | `SetTaskState` | Uygula'da | kuyruktan çıkar; imajdakini açmak ters işlem |
| Hosts listesi / özel girdiler | `SetHosts` (bölüm başına) | Uygula'da | boş bölüm = sil |
| Dosya / klasör ekle | `CopyTree` | Uygula'da | kuyruktan çıkar (imaja yazılan geri alınmaz) |
| Diğer sürümler | Uygula işinin devamı (D-055) | evet | commit edilen sürüm geri alınmaz |

## 5. Durumlar
- Boş: bağlı imaj yok → EmptyState "İmajlar'a git"; kuyrukta öğe yok → açıklama + eylem.
- Yükleniyor: İmajdaki sürücüler EmptyState'te "okunuyor"; Diller üst satırda okuma durumu.
- Hata: İmajdaki sürücüler → EmptyState'te hata + "Tekrar dene"; Diller → üst satırda hata metni; okunamayan paket → uyarı.
- Salt okunur bağlama: yeni sayfalar kuyruğa almayı ayrıca engellemiyor (açık konu).
- Diğer sürümler: bağlı sürüm kaydedilmezse diğerleri hiç başlamaz; salt okunur bağlamada seçenek görünmez.

## 6. Motor ve `wlcli`
- [x] `drivers [--remove=]`, `export-host-drivers`, `intl [--set=json|@dosya]`, `associations`, `export-host-associations`,
  `appx-info`, `appx-add`, `languages`, `apply --also= --wim=`
- [x] Gerçek imajda kanıt: `tools\lab_features.ps1` (yönetici, 2026-10-01 ALL PASSED; dil paketi medyasız atlandı)

## 7. Widget'lar
Mevcutlar (TableView, Tabs, Dropdown, CheckField, InfoBar, EmptyState, dialoglar). Dropdown menüsü kaydırma kazandı
(uzun listeler: tekerlek, PageUp / PageDown, yazarak atlama, 2 px kaydırma çubuğu).

## 8. String anahtarları
`apps.*`, `languages.*`, `tasks.*`, `hosts.*`, `files.*`, `drivers.image*`, `nav.{apps,languages,tasks,hosts,files}`, `apply.edition*`,
`apply.otherEditions*`.

## 9. Kabul kriterleri
- [x] `tools\lab_features.ps1` ALL PASSED (yönetici, 2026-10-01).
- [ ] Her sayfa koyu / açık, TR / EN render'da taşmasız (`--demo-*`).
- [ ] Kurulan sistemde (VM): kapatılan görevler Disabled, hosts bölümü, DNS ilkesi, varsayılan tarayıcı, provision
  edilen uygulama yeni kullanıcıda, arayüz dili / saat dilimi.
- [ ] Kullanıcı onayı.

## 10. Kullanıcı test senaryosu
1. Test ISO'sunun Pro sürümünü bağla. Görevler › "Önerilenleri kapat"; Hosts › telemetri; Ayarlar › Ağ › DNS Cloudflare.
2. Uygulamalar › Windows Terminal paketini sürükle (bağımlılıklar bulunmalı); Varsayılan › Firefox (yalnız ilişkilendirme).
3. Diller › saat dilimi, klavye. Dosyalar › bir klasörü "Public masaüstü"ne.
4. Sürücüler › İmajdaki sürücüler (liste gelmeli; bu bilgisayarınkini al).
5. Uygula › "Diğer sürümlere de uygula" → Home; çalıştır. Bitince iki sürüm de kaydedilmiş olmalı.
6. ISO oluştur → VM'de kur → 5. maddeleri doğrula.
