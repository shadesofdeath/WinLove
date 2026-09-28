# İkon stil kuralları

- **Grid:** 16×16, 1px iç güvenlik alanı (çizim 2…14). Stroke **1.25**, `stroke-linecap="round"`, `stroke-linejoin="round"`, renk `currentColor`, fill none.
- **Karakter:** düz kenarlar ve 90°/45° açılar; kavisler yalnızca daire/yay. Köşe yarıçapı yok — kalp dışında hiçbir yumuşatma. Bu, Fluent (yuvarlatılmış 1.5 stroke) ve Lucide (2 stroke, 24 grid) ile ayrışmayı sağlar.
- **Optik denge:** yatay çizgiler tam sayı + .5 y'de değil; 1.25 stroke ile tam sayı koordinat 125/150%'de en keskin sonucu verir. Küçük noktalar (more-*, drag-handle) 1px uzunluğunda round-cap çizgi.
- **Varyantlar:** `regular` (stroke) ve `filled` (seçili nav öğesi, aktif tab): kapalı şekli olan ikonlarda fill=currentColor + stroke, `fill-rule="evenodd"`; yalnızca çizgiden oluşanlarda stroke 1.75.
- **24 set:** koordinatlar ×1.5, stroke 1.5. Boş durum ve karşılama ekranı dışında kullanılmaz. (TODO: elle optik düzeltme.)
- **Yasaklar:** filter, mask, clipPath, text, image, style/class, use/defs. Yalnızca path (ve gerekirse circle/rect/line/polyline/polygon/g).
- **Renk:** ikon rengi bağlamdan gelir — text.secondary (rest), text.primary (hover/aktif), status.* (durum ikonları), accent (spinner, odak). İkon tek başına renkle anlam taşımaz; yanında metin ya da tooltip vardır.
- **D2D:** `icons-paths.json` → her alt yol ayrı `ID2D1PathGeometry` (veya `ID2D1SvgDocument`). Stroke style: width 1.25, round cap/join. Ölçek: 16 → hedef DIP, geometri transform ile.
- **Adlandırma:** kebab-case; nav ikonları işlev adı (components-remove), dosya ikonları `*-file`, caption ikonları `caption-*`.
- **Spinner:** 8 dilim, 45° adım, 800 ms/tur (steps(8)); reduce motion: 1.4 s opacity nabzı.
