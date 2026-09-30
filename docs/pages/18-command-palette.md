# P18 — Komut Paleti

> Durum: 🟨 geliştirme bitti, kullanıcı testi bekliyor (2026-09-30). Tasarım: 21 +
> `03_components/log-console-inspector-diff-palette.md`. Erişim: `Ctrl+K` veya başlık çubuğundaki arama kutusu.

## 1. Amaç
Klavyeden tek yerden: bir sayfaya git, bir ayarı / bileşeni / özelliği / servisi bul ve satırına atla, sık
kullanılan komutu çalıştır. Palet yalnız **bulur ve götürür**; hiçbir şeyi kendisi kuyruğa eklemez.

## 2. Ekran
- Scrim + 560 px panel, üstten 120 px (kısa pencerede yukarı kayar); bg.overlay, 1 px line.strong, r3, elevation.dialog.
- **Arama satırı** 40 px: ikon, kutusuz giriş (vurgu renkli imleç), sağda `Esc`. Yazılan metin seçili sonucun adının
  başıysa devamı soluk gösterilir; `→` / `End` tamamlar.
- **SONUÇLAR · n** (32 px satır): ikon + ad + alt satır; eşleşen kısım accent.subtle ile işaretli; seçili satır
  bg.raised + `↵`. En çok 7 satır; başlıktaki sayı toplam eşleşmedir (daraltmak için yazmaya devam).
- **KOMUTLAR** (24 px satır): ikon + ad + kısayol. Arama boşken yalnız bu grup görünür.
- Hiçbir şey eşleşmezse: "Sonuç yok" + ipucu.
- `↑` `↓` gezinir (uçlarda döner), `PgUp` / `PgDn` ilk / son, `Enter` çalıştırır, `Esc` / `Ctrl+K` / dışarı tık kapatır.
  Fare: üzerine gelinen satır seçilir, tık çalıştırır.

## 3. Neler bulunur
| Tür | Ne zaman | Alt satır | Enter |
|---|---|---|---|
| Sayfa | her zaman (ad, `--page` anahtarı, başlık, açıklama) | Sayfa · Git | sayfayı açar |
| Ayar (P12) | imaj bağlıyken | Ayarlar / Tweaks · sekme [· Kuyrukta] | sekmeyi açar, kontrolü odaklar |
| Bileşen | AppX listesi okunduysa (ad, paket kimliği, grup) | Bileşenler · Kaldırma kuyruğunda \| risk · boyut | grubu açar, satırı seçer |
| Özellik | özellik listesi okunduysa (ad, teknik ad) | Özellikler · durum | satırı seçer |
| Servis | servis listesi okunduysa | Servisler · hedef başlangıç [· Kuyrukta] | satırı seçer |

Satıra atlarken o sayfanın arama / süzgeçleri temizlenir (satır gizli kalmasın).

**Komutlar** (yalnız o an çalışabilenler listelenir):
| Komut | Kısayol | Koşul |
|---|---|---|
| Kuyruğu uygula | `Ctrl+Enter` | bağlı imaj + dolu kuyruk + süren iş yok |
| Kuyruğu preset olarak kaydet… | `Ctrl+S` | kuyruk dolu |
| Dosyadan preset yükle… | `Ctrl+O` | süren iş yok (imaj bağlı değilse nedenini söyler) |
| Kaynak aç… | — | bağlı imaj ve süren iş yok |
| İmajı çöz… | — | bağlı imaj var, süren iş yok |
| Tema: Koyu / Açık | `Ctrl+Shift+T` | — |
| Navigasyonu daralt / genişlet | `Ctrl+B` | — |
| Log klasörünü aç | — | — |

"Kuyruğu uygula" başka bir sayfadayken **Uygula sayfasını açar** (özet + riskler görülsün); Uygula sayfasındayken
çalıştırmayı başlatır (yüksek riskte onay dialogu). Tek tuşla, özeti görmeden imaj değiştirilmez.

Bu sayfayla gelen kısayollar: `Ctrl+K`, `Ctrl+Enter`, `Ctrl+S`, `Ctrl+O` (interaction.md "Kısayollar"). Bir dialog /
menü açıkken uygulama kısayolları çalışmaz (önceden `Ctrl+B`, `F1` dialogun altındaki arayüzü değiştirebiliyordu).

## 4. Model
- `app/shell/PaletteIndex`: `search(query)` → `PaletteResults{results, total, commands}`. Önbellek yok: her aramada
  durum yeniden okunur (liste palet açıkken yüklenirse kendiliğinden görünür; birkaç yüz öğe, anlık).
- Eşleşme (`matchPalette`): 0 ad sorguyla başlar · 1 adın bir kelimesi başlar · 2 kelime içinde · 3 sorgu kelimeleri
  adın farklı yerlerinde · 4 yalnız ek metinde (paket kimliği, sayfa açıklaması…). Eşit puanda tür sırası: Sayfa,
  Ayar, Bileşen, Özellik, Servis.
- Arama katlaması (`foldForSearch`): küçük harf; `I` / `İ` / `ı` → `i`; `ç ğ ö ş ü` → `c g o s u`. "guncelleme",
  "WINDOWS", "wındows" beklenen şeyi bulur. Karakter sayısı korunur (işaret konumu için).
- `app/shell/CommandPalette`: modal widget; `ui::SearchBox`'ın "bare" hali (kutusuz, `setCompletion`).
  Seçim → önce kapanır, sonra `Shell::runPaletteItem` (sayfa + `reveal`).
- Sayfalara eklenen: `ComponentsPage / FeaturesPage / ServicesPage / TweaksPage::reveal(id)`.
- `docs/ARCHITECTURE.md`'deki "her sayfanın `commands()`'ı" fikri uygulanmadı: komutlar `Shell`'in zaten sahip olduğu
  eylemler, tek yerde (`PaletteCommand`) toplandı.

## 5. Tasarımdan farklar
- 60 ms debounce yok: arama bellekte ve anlık, geciktirmek yalnız yavaşlatırdı.
- Tuş etiketleri (`Esc`, `↵`, kısayollar) satırda dikey ortalı ve sağdan 16 px hizalı (SVG'de 16 / 20 / 27 px ve farklı
  dikey kaymalar var — dışa aktarma artığı).
- Panel son satırdan 8 px sonra biter (SVG'de altta 32 px boşluk).
- Tasarımdaki "Yoğunluk değiştir" (yoğunluk yok, P16 §4) ve "Seçili bileşenleri kaldırma kuyruğuna ekle" (bizde
  işaretlemek = kuyruğa eklemek) komutları yok. Güncelleme paketleri, sürücüler ve ham kayıt defteri tweak'leri
  aranmıyor (sayfaları bulunuyor).
- Sonuç listesi kaydırılmaz; 7 satırdan fazlası için sorgu daraltılır.

## 6. Kabul
- [ ] `Ctrl+K` ve başlıktaki kutu paleti açar; `Esc`, `Ctrl+K`, dışarı tık kapatır; odak eski yerine döner.
- [ ] "serv" → Servisler sayfası ilk sırada; Enter sayfayı açar.
- [ ] İmaj bağlı + listeler okunmuşken bir uygulama / servis / özellik adı yaz → Enter ilgili sayfada satırı seçer.
- [ ] Bir P12 ayarının adını yaz → doğru sekme açılır, kontrol odakta.
- [ ] Boş aramada komutlar; "Kuyruğu uygula" yalnız kuyruk doluyken görünür ve Uygula sayfasına götürür.
- [ ] Türkçe: "guncel" → Güncellemeler; büyük / küçük harf ve ı / i fark etmez.
- [ ] Bir dialog açıkken `Ctrl+K`, `Ctrl+B`, `F1` hiçbir şey yapmaz.

## 7. Doğrulama
Unit: `tests/app/PaletteTests.cpp` (katlama, eşleşme puanları, duruma göre bulunanlar, kuyruk → alt satır, widget:
klavye, tamamlama, kapanma, tık). Render: `--palette[=sorgu]` + `--keys=down,enter,…` ile açık palet (koyu / açık,
TR / EN, boş sonuç) ve her tür için Enter sonrası varılan sayfa.
