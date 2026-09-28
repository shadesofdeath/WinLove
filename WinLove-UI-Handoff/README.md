# WinLove — UI Handoff Paketi (v0.1.0, palet 1a "Bakır")

Bu paket, WinLove masaüstü uygulamasının arayüzünü **Direct2D + DirectWrite + DirectComposition** ile sıfırdan çizecek C++ geliştiricisine yöneliktir. Tüm ölçüler 96 DPI (100%) baz pikseldir; 1px çizgiler her ölçekte .5 offset ile keskin tutulur.

## Tasarım ilkeleri (her kararın ölçütü)
1. **Çizgi, kutu değil.** Ayrım 1px `line.subtle` ve ton farkıyla; kart/çerçeve/gölge yok (yüzen katmanlar hariç).
2. **Ağırlıkla hiyerarşi.** Boyut sabit (12 / 11 / 16); önem yazı ağırlığı, ton ve hizalama ile.
3. **24px ritim.** Kontrol, satır ve başlık 24px hatta; boşluklar 4px ızgarada.
4. **Tek vurgu, tek aksiyon.** Vurgu yalnızca seçim, odak ve tek birincil aksiyonda ("Uygula", durum çubuğunda sabit).
5. **Önce yüzey, sonra derinlik.** Liste karar için yeterli (ad, risk, boyut); geri kalanı inspector'da.
6. **Renk asla tek başına.** Risk/durum = ikon + metin + renk; High Contrast'ta renk düşse anlam kalır.

## Geliştirici bu paketi hangi sırayla okumalı
1. `01_tokens/tokens.md` → sonra `tokens.json` ve `tokens.h` (tek kaynak; her renk/ölçü buradan).
2. `08_implementation/layout-system.md` → pencere iskeleti, paneller, splitter, DPI.
3. `08_implementation/d2d-rendering-guide.md` → bileşen çizim adımları, katman sırası, DirectComposition ağacı.
4. `03_components/*.md` + `components-gallery.html` (tarayıcıda aç, tema/yoğunluk değiştir).
5. `02_icons/icons.md` + `icons-paths.json` → path geometry olarak yükle; `icons-preview.html` ile karşılaştır.
6. `04_screens/screens.md` + SVG'ler (1440×900, koyu/açık) — `screens-preview.html` hepsini listeler.
7. `06_prototype/index.html` → etkileşimleri deneyimle (Ctrl+K, tri-state ağaç, tema, yoğunluk, TR/EN).
8. `05_motion/motion.md`, `08_implementation/interaction.md`, `accessibility.md`.
9. `07_copy/strings.*.json` → tüm metinler anahtarla; UI'da sabit metin yok.

## Klasörler
| Klasör | İçerik |
|---|---|
| 00_brand | logo (mark/full/mono), app-icon 10 boyut, splash, marka kuralları |
| 01_tokens | tokens.json / tokens.h / tokens.md (renk ×3 tema, tipografi, spacing, radius, elevation, motion, opacity, z) |
| 02_icons | 115 ikon × (16 regular, 16 filled, 24 türetilmiş), icons-paths.json, önizleme, stil kuralları |
| 03_components | bileşen spec'leri (.md), state redline SVG'leri, canlı galeri |
| 04_screens | 27 ekran × 2 tema = 54 SVG + screens.md |
| 05_motion | tüm geçişlerin sayısal tanımı |
| 06_prototype | tek dosya tıklanabilir prototip |
| 07_copy | strings.tr.json / strings.en.json (aynı anahtarlar) |
| 08_implementation | D2D çizim rehberi, layout, etkileşim, erişilebilirlik |

## Denetimler
- Boyut: hiçbir kontrol 28px'i, başlık 16px'i, radius 4px'i aşmaz (prototip ve SVG'ler tokenlardan üretildi).
- Kontrast: tokens.md'deki tablo; koyu ve açık temada metin ≥ 4.5:1, ikon/odak ≥ 3:1.
- SVG ikonlar: yalnızca `path`, `currentColor`; filter/mask/clipPath/text/style/defs yok.

## TODO / Sonraki adımlar (dürüst liste)
- **Fontlar:** IBM Plex Sans ve JetBrains Mono .ttf dosyaları pakete eklenmedi (OFL; `00_brand/fonts/` altına indirilecek). Wordmark (`logo-full.svg`) hâlâ `<text>` kullanıyor — outline'a çevrilecek.
- **İkonlar:** 24px set 16px'ten 1.5× türetildi, optik olarak elle düzenlenmedi. Stroke → outline path dönüşümü yapılmadı; D2D'de `ID2D1PathGeometry` + `DrawGeometry(stroke 1.25)` ile doğrudan çizin (önerilen), outline gerekiyorsa SVGO/Inkscape "Stroke to Path" ile üretin. Filled varyant: kapalı şekiller fill+stroke; çizgi ikonlarında stroke 1.75.
- **App icon:** sizes 16–40 için heart iç detayı otomatik sadeleştirildi (32 altında iç çevron yok); gerçek .ico rasterizasyonu 20/40 için piksel kontrolü ister.
- **Bileşen redline SVG'leri:** button, checkbox, tree-row, textbox için üretildi; diğer bileşenlerin ölçüleri .md spec'lerde ve galeride — ayrı redline çizimleri yapılmadı.
- **Ekranlar:** hover/focus varyantları SVG'lerde değil, galeri ve prototipte gösterildi. Slider, NumberBox ekran içinde kullanılmadı (spec'te var).
- **Prototip:** ContextMenu sağ tık, sütun yeniden boyutlandırma ve drag&drop sıralama simüle edilmedi (spec: interaction.md).
- **Copy:** strings.*.json çekirdek anahtarlar (≈180); ekran içi tüm örnek veriler (paket adları vb.) içerik olduğu için anahtarlanmadı.
