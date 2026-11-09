# WinLove — Yerleşim ve davranış

Tüm ölçüler DIP. Renk adları `Theme.h` alanlarıdır. Sabitler `wl::m::` altında.

## Ortak iskelet (her ekran)

```
┌ Başlık çubuğu 44 ───────────────────────────────────────────────────────────┐
│ [logo] WinLove  [Görüntü][Ön Ayar][Araçlar]   [ 🔍 Ara…        Ctrl K ]   ☾ – □ × │
├ Görüntü başlığı (üst boşluk 26, yan boşluk 40) ──────────────────────────────┤
│ Windows 11 Pro  [Bağlı]                          17,82 GB → 16,96 GB  −0,86 GB │
│ 24H2 · 26100.1742  x64  tr-TR  D:\ISO\…iso  Değiştir           ▬▬▬▬▬▬▬▬▬ 180×4 │
├ Sekmeler (üst boşluk 16) ────────────────────────────────────────────────────┤
│ BAŞLAT │ YÜKLE                                              │ KALDIR     │ YAPILANDIR                          │ BİTİR │
│ Görüntü│ Güncellemeler 2  Sürücüler 3  Kayıt Defteri 0  Kurulum Sonrası 1 │ Bileşenler 9│ Özellikler 0  Ayarlar 8  Zamanlanmış Görevler 0 │ Uygula │
├──────────────────────────────────────────────────────────────┬──────────────┤ 1px line
│ İçerik (yan boşluk 40, üst 24)                                │ Sağ panel    │
│                                                              │ 320 (340)    │
│                                                              │ surface      │
├ Durum çubuğu 30 ─────────────────────────────────────────────┴──────────────┤
│ ● Bağlı · Windows 11 Pro · D:\WinLove\mount        DISM 10.0.26100.1  Boş alan… │
└──────────────────────────────────────────────────────────────────────────────┘
```

| Öğe | Ölçü / stil |
|---|---|
| Başlık çubuğu | 44 yükseklik, zemin `bg`. Logo 18×18 (5 yarıçaplı çerçeve, `text`) + "WinLove" BodySemi |
| Üst menü | 30 yükseklik, yatay iç boşluk 10, yarıçap 7. Seçili: `sunk` zemin + BodySemi `text`. Diğerleri: BodyMedium `text2` |
| Arama | Ortada, en fazla 440×34, yarıçap 9, `surface` + `line` kenar. Kısayol kutusu: `line` kenar, yarıçap 5 |
| Pencere düğmeleri | 46×44. Kapat üzerine gelince `closeHover` + beyaz glif |
| Görüntü adı | Title (28 Bold, −0.025em). Rozet 12 SemiBold, 22 yükseklik, yarıçap 6, `sunk`/`text2`. Bağlıyken `acSoft`/`acFg` |
| Boyut | "önce" Body `text3` → ok `text3` → "sonra" Heading `text` → fark BodySemi `acFg`. Altında 180×4 çubuk: `line` zemin, `ac` dolgu = sonra/önce |
| Grup başlığı | GroupCap (11 SemiBold, +0.06em, büyük harf) `text3`, 16 yükseklik |
| Sekme | 40 yükseklik, aralarında 18. Seçili: BodySemi `text` + altta 2px `ac` çizgi. Diğerleri: BodyMedium `text2`. Sayaç: Count (12 Medium), >0 ise `acFg`, 0 ise `text3` |
| Grup ayracı | 1px `line`, iki yanında 18 boşluk |
| Sağ panel | `surface` zemin, solda 1px `line`, iç boşluk 24 |
| Durum çubuğu | 30 yükseklik, `surface`, üstte 1px `line`, Caption `text2`, yan boşluk 20. Nokta 7px |

## 1 · Görüntü (Başlat › Görüntü)

**İçerik, yukarıdan aşağı, bölümler arası 28:**

1. **KAYNAK** (SectionCap `text3`, alt boşluk 10). Kartlardan oluşan ızgara: hücre en az 180 genişlik, 1px `line` ızgara çizgisi, dış yarıçap 12, hücre iç boşluğu 14×16. Her hücrede etiket (Caption `text3`) ve değer (Body):
   Tür `ISO · UDF` · Birim etiketi (Mono) · Görüntü dosyası `sources\install.wim` (Mono) · Sıkıştırma `LZX` · Önyükleme `UEFI + BIOS` · Bütünlük `● Doğrulandı` (`ok`)
2. **INSTALL.WIM · SÜRÜMLER.** Tablo kartı (`surface`, yarıçap 12, `line` kenar).
   Sütunlar: Dizin 44 (Mono `text3`) · Ad (esnek: ad BodyMedium + Edition ID Mono 11.5 `text3`) · Mimari 60 · Derleme 130 (Mono) · Boyut 96 (sağa yaslı, tabular) · Durum 130.
   Başlık satırı 34 (Caption `text3`). Satırlar 52, her satırın üstünde 1px `line`, sütun aralığı 12, yan boşluk 14. Seçili satır: `acSoft` + soldan 2px `ac` şerit (üst/alt 10 boşluk).
   Durum: `—` (`text3`) / `● Yükleniyor 42%` (`warn`) / `● Bağlı` (`ac`)
3. **DİĞER GÖRÜNTÜLER:** boot.wim (WindowsPE, 2 dizin), winre.wim. Satırlar 52.

**Sağ panel 340 — WIM bilgisi:**

- "Dizin 5 · WIM bilgisi" Caption `text3`, altında ad Heading
- Anahtar-değer listesi (etiket sütunu 118, satır aralığı 9, Small): Açıklama, Edition ID (Mono), Kurulum türü, Sürüm, Mimari, Dil, HAL
- 1px ayraç, ardından ikinci liste: Açılmış boyut, Dosya, Klasör, Oluşturma, Değişiklik
- Alt bölüm (üstte 1px `line`, 18 boşluk) duruma göre değişir:

| Durum | İçerik |
|---|---|
| Bağlı değil | "Bağlama klasörü D:\WinLove\mount" · **Yükle** (Primary, tam genişlik, 40) · Salt okunur yükle (Secondary) |
| Yükleniyor | "Yükleniyor" + `%` (`acFg`) · ilerleme çubuğu 6 · "48.211 / 103.205 dosya" ve "0:41 geçti" (Caption `text3`) · 5 adımlık liste · İptal et |
| Bağlı (seçili = bağlı) | ● Bağlı + yol · **Düzenlemeye başla** · Kaydet / Çıkar (yan yana) |
| Başka sürüm bağlı | "Şu an X bağlı. Önce onu çıkar." · Bağlı görüntüyü çıkar |

**Yükleme adımları** (eşik = ilerleme yüzdesi):
`0` install.wim okunuyor · `6` Dizin meta verisi doğrulanıyor · `12` Dosyalar çıkarılıyor · `82` Kayıt defteri kovanları yükleniyor · `92` Paket ve bileşen listesi okunuyor.
Adım simgesi 16'lık daire: bitti = `text` dolgu + onay, şimdi = 2px `ac` halka, bekliyor = `line2` halka ve `text3` metin.
Gerçek uygulamada yüzde ve dosya sayısı DISM ilerleme geri çağrısından (`DismMountImage` progress callback) gelir.

## 2 · Bileşenler (Kaldır › Bileşenler)

**Araç satırı:** liste içi arama (en fazla 360×36, yarıçap 9) · haplar (30 yükseklik, aralarında 6): Tümü / Seçilenler / Dikkat gerektiren · sağda "Önerilenleri seç" (Ghost, `acFg`).

**Liste:**

- Kategori başlığı: SectionCap `text3`, üst boşluk 22, alt 8; sağda "3 seçili".
- Satır 56 yükseklik, üstünde 1px `line`. Sütunlar: onay kutusu 22 · ad+açıklama (esnek; ad BodyMedium, açıklama Small `text3`) · risk (otomatik) · boyut 84 (sağa yaslı). Aralık 14.
- Risk: güvenli öğede **hiçbir şey gösterilmez**. Dikkat → `● Dikkat` (`warn`), Riskli → `● Riskli` (`risk`), nokta 6.
- Ada tıklayınca satır altında açılır (sol girinti 36, en fazla 640 genişlik): açıklama (Body `text2`, satır yüksekliği 1.6) · "Paket" (Mono) + "Etkilenenler" · not varsa solda 2px `warn` çizgili, `warn` renkli paragraf.

**Sağ panel 320 — Değişiklikler:**

- "Değişiklikler" Panel + sağda "23 işlem" (Small `text3`).
- "Kaldırılacak bileşenler" (BodySemi, sayaç `text3`, tıklayınca açılır/kapanır). Altında her öğe 30 yükseklik: ad (Body `text2`) · boyut (Caption `text3`) · × düğmesi 26 (işareti kaldırır).
- 1px ayraç · Ayarlar 8 · Güncellemeler 2 · Sürücüler 3 · Kurulum Sonrası 1 (her biri BodySemi, ilgili sekmeye gider).
- Alt bölüm (üstte 1px `line`): "Çıktı · ESD · ISO oluşturulacak", "Konum · D:\ISO\WinLove_24H2.iso" (Mono) · **Uygula** (Primary, 44).
- Uygula'ya basınca aynı yer ilerleme görünümüne döner: aşama adı + `%` · çubuk 4 · 6 adım (eşikler: 0 doğrulama, 10 bileşenler, 40 güncellemeler, 62 ayarlar+sürücüler, 74 kaydetme, 90 ISO) · İptal et. Bitince "ISO hazır" + Klasörde göster / Kapat.
- Görüntü başlığındaki "sonra" boyutu ve sekme sayacı seçimle anında güncellenir.

## 3 · Ayarlar (Yapılandır › Ayarlar)

- İçerik en fazla 980 genişlik. Gruplar: Gizlilik · Başlat ve görev çubuğu · Dosya Gezgini (SectionCap, üst boşluk 26).
- Satır en az 64, üstünde 1px `line`. Sütunlar: ad (BodyMedium) + açıklama (Small `text3`) · denetim · 32'lik geri al alanı.
- Denetim: `Switch` (40×22, düğme 16; kapalı `line2`, açık `ac`) ya da `Segmented` (34 yükseklik, dış yarıçap 9, iç 7; seçili `surface` + `line2` kenar + SmallSemi).
- Varsayılandan farklıysa adın yanında 6px `ac` nokta ve sağda varsayılana dön düğmesi (`DrawIcon(Icon::Reset)`, 30×30).
- Sağ panelde "Ayarlar" altında her değişiklik "Ad: değer" olarak listelenir; × varsayılana döndürür.

## Tema

- Her ekranda sağ üstteki güneş/ay düğmesi açık ↔ koyu geçişi yapar. `Painter::Begin`'e diğer `Theme`'i vermen yeterli.
- Vurgu her iki temada `#2563EB` (dolgu, üstünde beyaz metin, 5.2:1). Vurgu metni açıkta `#1D4ED8`, koyuda `#7AA7FF`.
- Renkli ikon kullanılmaz. Renk yalnızca durum için: `ac` seçim/bağlı, `warn` dikkat/yükleniyor, `risk` riskli, `ok` doğrulandı.
