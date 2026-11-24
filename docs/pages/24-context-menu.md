# P24 — Sağ tık menüsü

**Durum:** 🟨 test (2026-11-24, D-155) · **Tasarım:** D-139 çalışma sayfası · **Yer:** Yapılandır › Ayarlar sekmesi, "Servisler"den sonra

## 1. Amaç
İmajın Gezgin sağ tık menüsündeki öğeleri (komutlar, kabuk uzantıları, Windows 11 uygulama öğeleri) imajın kendi
kayıtlarından görmek; istenmeyeni gizlemek, imajın gizlediğini göstermek, kendi komutunu eklemek.

## 2. Görünüm bölgeleri
| Bölge | İçerik |
|---|---|
| Şerit | `Öğe ekle…` · `Sıfırla` |
| Süzgeç satırı | Ara ("/") · Yalnızca değişenler · özet ("54 öğe · 2 değişecek") |
| Tablo | Öğe · iz · Nerede (Dosyalar, Klasörler, Klasör arka planı, Sürücüler, Masaüstü …) · Tür · Durum (göz; tık = gizle / göster) |
| Kart | kayıt yolu / CLSID; Nerede, Tür, Uygula'da, Komut, Kaynak, Shift ile; not; Gizle / Göster · Değişikliği geri al · (kendi öğe) Kaldır |
| Öğe ekle penceresi | Ad · Komut (%1; arka planda %V) · Simge · Nerede (6 seçenek) · Yalnız Shift ile · Vazgeç · Ekle |

## 3. Veri
| Veri | Kaynak | Ne zaman | Önbellek |
|---|---|---|---|
| Öğeler | `core::readContextMenu(mount)` (offreg + WindowsApps manifestleri, yedekleme yetkisiyle) | sayfa ilk açılınca, motor iş parçacığında | `AppState::ContextMenuList` |
| Değişiklikler | kuyruktaki `SetContextMenu` (`entry\<id>`, `add\<id>`) | her çizimde | — |

## 4. Aksiyonlar
| Aksiyon | Tetikleyici | Etki | Geri alınır |
|---|---|---|---|
| Gizle / göster | Durum hücresi, Enter, kart | `SetContextMenu entry\…` (imajın durumuna dönmek işlemi çıkarır) | Ctrl+Z |
| Öğe ekle | şerit | `SetContextMenu add\<id>` | Ctrl+Z |
| Kaldır (kendi öğe) | kart | işlem çıkar | Ctrl+Z |
| Sıfırla | şerit | bütün `SetContextMenu` çıkar | tek Ctrl+Z |

## 5. Durumlar
- Bağlı imaj yok / okunuyor / okunamadı: Servisler'deki gibi.

## 6. Motor API'si ve `wlcli`
- [x] `core/image/ContextMenu.h` — `readContextMenu`, `contextMenuHideWrites` / `ShowWrites` / `AddWrites`, `contextMenuItemId`, JSON
- [x] `wlcli context-menu <mount> [--set=hide:<id>|add:<ad>|<komut>]`

## 7. Kabul kriterleri
- [x] Gerçek imajın öğeleri okunur, Türkçe adlarıyla (lab: 54 öğe, 734 ms).
- [x] Gizle (fiil / uzantı) ve ekle yazımları kovana girer, geri okumada görünür (lab).
- [x] Windows 11 uygulama öğeleri (yedekleme yetkisiyle): lab 2, 4 öğe (Paint, Clipchamp, Not Defteri, Terminal).
- [x] Kurulan Windows'ta menüde görülür / görülmez (VM d156: kabuğun fiil listesi + Windows 11 menüsünün görüntüsü).

## 8. Kullanıcı test senaryosu
1. İmajı bağla → Ayarlar › Sağ tık menüsü → "Paylaş" ve "Önceki sürümleri geri yükle" → gizle.
2. Öğe ekle: "Not Defteri ile aç", `notepad.exe "%1"`, Yalnız dosyalar.
3. Uygula, kur → Gezgin'de sağ tık (Windows 11: "Daha fazla seçenek göster") → ikisi yok, yeni öğe var.
