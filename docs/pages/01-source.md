# P01 — Kaynak

**Durum:** ✅ Tamamlandı (2026-09-28). Canlı sistem kartı kullanıcı isteğiyle kaldırıldı (D-021). · **Tasarım:** `04_screens/01-welcome-source-*.svg`, `s1-empty-no-source`, `s4-admin-required` · **Prototip:** `06_prototype/index.html` (source)

## 1. Amaç
Kullanıcı düzenleyeceği Windows imajını seçer: ISO, WIM/ESD/SWM ya da çıkarılmış ISO klasörü. Seçim anında (yönetici gerekmeden) sürümler okunur, kaynak son kullanılanlara eklenir ve İmajlar sayfasına (P02) geçilir. (Canlı sistem düzenleme kapsam dışı — D-021.)

## 2. Görünüm bölgeleri (1440×900, içerik x=216..1424)
| Bölge | İçerik | Ölçü |
|---|---|---|
| Başlık | "Kaynak seç" + açıklama; sağda secondary "Dosya aç…", "Klasör…" (4 gap) | butonlar y=49, h24, sağ kenar 1424 |
| DropZone | download 24, "İmaj dosyasını buraya bırak" (bodyStrong), uzantı listesi (caption) | x216..1424 (tam genişlik), y108..268 |
| Son kullanılanlar | section başlık y≈292; sütun başlığı 24 (caption 500 text.tertiary); satırlar 24 | sütunlar: Ad esnek (ikon 16 + 6) · Tür 96 · Sürüm 208 · Boyut 80 sağa · 8 · Son açılış 140 |

## 3. Veri
| Veri | Kaynak | Ne zaman | Önbellek |
|---|---|---|---|
| Son kullanılanlar (≤10) | `%LOCALAPPDATA%\WinLove\recent.json` (`app/state/RecentSources`) | açılışta | bellekte; her açmada yazılır |
| Kaynak bilgisi | `core::openSource` (motor thread'i) | dosya seçilince | `AppState.source` |

## 4. Aksiyonlar
| Aksiyon | Tetikleyici | Etki | Admin? |
|---|---|---|---|
| Dosya aç | "Dosya aç…", DropZone tık, Ctrl+O yok (O = preset, interaction.md) | IFileOpenDialog (.iso .wim .esd .swm) → aç | Hayır |
| Klasör aç | "Klasör…" | IFileOpenDialog (klasör) → `sources\install.*` aranır | Hayır |
| Sürükle-bırak | pencerenin herhangi bir yeri | kaynak türü → aç; değilse DropZone hata durumu | Hayır |
| Son kullanılanı aç | çift tık / Enter | aç; dosya yoksa satır soluk, açarken hata | Hayır |

Açma sonrası: `AppState.source` dolar, başlıkta breadcrumb = dosya adı, P02'ye geçilir, son kullanılanlar güncellenir.

## 5. Durumlar
- **Boş:** son kullanılanlar yoksa bölüm gizlenir (yerine hiçbir şey; DropZone yeterli).
- **Yükleniyor:** ISO ~70 ms; bekleme göstergesi yok. Uzun sürerse (ağ yolu) DropZone metni "Yükleniyor…".
- **Hata:** DropZone altında error InfoBar ("Açılamadı: …", kapatılabilir) + log.
- **Sürükleme:** geçerli → DropZone accent + accent.subtle; geçersiz → status.error sınır + "Desteklenmeyen dosya".
- **Admin gerekli:** bu sayfada yok (s4 dialogu P02'de bağlarken kullanılır).

## 6. Motor / wlcli
- [x] `openSource` ISO/WIM/ESD/SWM (P00/Faz 2)
- [x] `openSource` klasör (çıkarılmış ISO) → `wlcli info <klasör>`
- [x] `releaseLabel(build)` / `releaseSummary` → "11 25H2 · 26200.8037"

## 7. Widget'lar
- [x] `DropZone` (rest / hover / drag-valid / drag-invalid / loading)
- [x] `InfoBar` (error, warning, success, info; kapatılabilir)
- [x] `Dialog` + Host modal katmanı (scrim, odak kapanı, Esc/Enter, scrim tıklaması)
- [x] Çok satırlı metin (Canvas `drawTextWrapped`, `TextStyles::measureWrapped`)
- [x] Sayfaya özel: RecentList (satır seçimi, çift tık, ↑↓ Home End Enter)
- [x] Platform: `DropTarget` (OLE), `FileDialog` (IFileOpenDialog)

## 8. String anahtarları
Mevcut: `source.*`, `dialogs.admin*`, `common.cancel|name|type|size`. Eklenecek: `source.version`, `source.dropUnsupported`, `source.openFailed`, `source.liveLater`, `source.adminYes`, `source.fileMissing`, `source.filterImages`, `source.filterAll`, `source.pickFolder`. Düzeltme: `common.size` TR "Size" → "Boyut" (handoff çeviri hatası).

## 9. Kabul kriterleri
- [x] Test ISO'su açılır (komut satırı + birim testi: Shell→motor→UI akışı), 6 sürüm okunur, P02'ye geçilir, breadcrumb dosya adını gösterir — dosya diyaloğu ile: kullanıcı testi
- [ ] Aynı ISO sürükle-bırakla açılır; .txt bırakınca hata durumu, hiçbir şey açılmaz — kurallar test edildi, gerçek OLE sürüklemesi kullanıcı testinde
- [x] Çıkarılmış klasör (`C:\WinLoveLab\iso`) açılır (`wlcli info`); "Klasör…" diyaloğu kullanıcı testinde
- [x] Son kullanılanlar kalıcı, en yeni üstte, en fazla 10, büyük/küçük harf farkı olmadan tekil, bozuk dosya boş sayılır, silinmiş dosya soluk + tooltip
- [x] Koyu/açık/HC ve %100/%150/%200 render tasarımla uyumlu (bkz. §11)
- [x] Klavye: Tab ile başlık → menü → Dosya aç/Klasör → DropZone → liste; ↑↓ Enter liste içinde; dialog'da odak kapanı (Host testleri)
- [x] TR/EN eksiksiz (üretici denetimi; `common.size` TR düzeltildi)

## 10. Kullanıcı test senaryosu
1. WinLove'u normal (yönetici olmayan) başlat → Kaynak sayfası.
2. "Dosya aç…" → `Downloads\Win11_25H2_Turkish_x64_v2.iso` → İmajlar sayfasına geçmeli, başlıkta ISO adı.
3. Kaynak'a dön → listede ISO "bugün HH:MM", "ISO", "11 25H2 · 26200.8037", "6,72 GB" görünmeli.
4. Masaüstünden bir .txt dosyasını pencereye sürükle → kırmızı "Desteklenmeyen dosya"; bırak → hiçbir şey olmamalı.
5. ISO'yu Explorer'dan sürükle-bırak → açılmalı.
7. Uygulamayı kapatıp aç → son kullanılanlar duruyor olmalı.

## 11. Görsel doğrulama notları / bilinçli sapmalar
Karşılaştırma: `python tools/compare_design.py 01-welcome-source -- --recent-file=tests/integration/fixtures/recent-sample.json`
- Sütun başlıkları caption 400 (tasarım 500): 11px medium stili yok; ayırt edici değil, stil eklemeye değmez.
- Admin dialogu içerik kadar uzun (tasarımda gövde ile butonlar arasında ~64px boşluk var; spec'teki 24'ü uyguladık).
- Satır seçimi accent.subtle yalnızca liste odaktayken; odak yokken yalnızca hover (bg.raised) — tasarım karesindeki vurgu hover.
- Section başlığı Türkçe büyük harf: `LCMapStringEx(tr-TR, LINGUISTIC_CASING)` → "SON KULLANILANLAR" (önceden "KULLANıLANLAR" hatası vardı).
- Birden fazla dosya bırakılırsa ilk kaynak türündeki açılır (diğerleri yok sayılır).
