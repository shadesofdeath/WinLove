# P23 — Grup İlkesi

**Durum:** 🟨 test (2026-11-24, D-154) · **Tasarım:** D-139 çalışma sayfası (Servisler'in düzeni) · **Yer:** Yapılandır › Ayarlar sekmesi, "Ayarlar"dan sonra

## 1. Amaç
İmajın kendi yönetim şablonlarındaki (ADMX) her ilkeyi, imajın dilinde, gpedit'teki gibi Yapılandırılmadı / Etkin / Devre
dışı yapmak. Elle tutulan liste yok: Windows'un yeni build'i yeni ilkelerini kendisi getirir.

## 2. Görünüm bölgeleri
| Bölge | İçerik |
|---|---|
| Şerit | `Sıfırla` (yalnız ilkeler, tek Geri al) |
| Süzgeç satırı | Ara ("/") · Kapsam (Bilgisayar ve kullanıcı / Bilgisayar / Kullanıcı) · Kategori (kökler) · Yalnızca değişenler · özet ("4.644 ilke · 3 ayarlanacak") |
| Tablo | İlke · iz (kuyrukta) · Kategori (yaprak; ipucunda tam yol) · Kapsam · Durum (menü) |
| Kart | açıklama metni; Kapsam, Kategori yolu, Uygula'da, Desteklenen, Şablon, Kayıt; Etkin'in seçenekleri; gpedit notu; Durumu seç · Seçenekler… · Yapılandırılmadı'ya döndür |
| Seçenekler penceresi | sunumun denetimleri sırayla (açılır liste, onay kutusu, sayı / metin; liste ve çok satırlı metin `;` ile) · Vazgeç · Etkinleştir |

## 3. Veri
| Veri | Kaynak | Ne zaman | Önbellek |
|---|---|---|---|
| Şablonlar | `core::policy::readPolicyDefinitions(<mount>\Windows\PolicyDefinitions, imajın dili)` | sayfa ilk açılınca, motor iş parçacığında | `AppState::PolicyList` (bağlama başına) |
| Durum | kuyruktaki `SetPolicy` (`machine\<id>` / `user\<id>`) | her çizimde (satır başına revizyonla) | — |

## 4. Aksiyonlar
| Aksiyon | Tetikleyici | Etki | Geri alınır |
|---|---|---|---|
| Durum seç | Durum hücresi, Enter, kart | `SetPolicy` (Yapılandırılmadı: işlem çıkar) | Ctrl+Z |
| Etkin (seçenekli) | menü / kart | seçenekler penceresi → `SetPolicy` | Ctrl+Z |
| Sıfırla | şerit | bütün `SetPolicy` çıkar | tek Ctrl+Z |

## 5. Durumlar
- Bağlı imaj yok: boş durum + "Görüntü'ye git".
- Okunuyor: "İlkeler okunuyor".
- Okunamadı (PolicyDefinitions yok): hata + Yeniden dene.
- Geçersiz değer: uyarı bildirimi, pencere açık kalır.

## 6. Motor API'si ve `wlcli`
- [x] `core/policy/Admx.h` — `readPolicyDefinitions`, `PolicyCatalog::add / finish / path / rootOf`
- [x] `core/policy/PolicyWrites.h` — `defaultValues`, `policyWrites`, `policyOperationJson`, `parsePol` / `formatPol` / `mergeIntoPol`, `recordInGroupPolicy`
- [x] `wlcli policies <mount|PolicyDefinitions> [ll-CC] [--find=metin|id --set=enabled|disabled[:user]] [--pol=Registry.pol]`

## 7. Kabul kriterleri
- [x] Gerçek imajın şablonları okunur (lab: 3532 ilke, 662 ms, Türkçe).
- [x] Etkin / Devre dışı yazımları kovanlara ve Registry.pol'e girer, geri okunur (lab).
- [ ] Kurulan Windows'ta gpedit.msc ilkeyi ayarlanmış gösterir (VM).
- [ ] Canlı pencerede: ara, süzgeçler, menü, seçenekler penceresi.

## 8. Kullanıcı test senaryosu
1. İmajı bağla → Ayarlar › Grup İlkesi → "otomatik güncelleştirme" ara.
2. "Otomatik Güncelleştirmeleri Yapılandır" → Durum → Etkin → seçenekler: 2 – İndirme ve yükleme için bildir → Etkinleştir.
3. "Denetim Masası'na … erişimi yasakla" (Kullanıcı) → Etkin.
4. Uygula, ISO, VM'e kur → gpedit.msc'de ikisi ayarlanmış; Denetim Masası açılmıyor.
