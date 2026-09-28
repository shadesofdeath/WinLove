# Direct2D çizim rehberi

## Genel
- Tek `ID2D1DeviceContext` per pencere; swap chain DXGI_FORMAT_B8G8R8A8_UNORM, opak arka plan (`bg.base`), DWM backdrop yok. Alpha yalnızca gölge ve scrim katmanlarında.
- Fırçalar: token başına bir `ID2D1SolidColorBrush` cache (tema değişince yeniden oluştur). Gradyan yok (logo hariç: `ID2D1LinearGradientBrush` yalnızca app icon).
- Metin: `IDWriteTextFormat` per type token (family, weight, size, lineHeight → `SetLineSpacing(UNIFORM, lineHeight, baseline)`; baseline = 0.78×lineHeight yaklaşımı yerine font metrics'ten hesapla). `DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC`, ClearType kapalı (grayscale AA) — koyu temada renk saçağı önlenir. Tabular rakam: `IDWriteTypography` `DWRITE_FONT_FEATURE_TAG_TABULAR_FIGURES` sayısal sütunlarda.
- 1px çizgi: `DrawLine` yerine `FillRectangle` (x, y, w, 1/scale) — her DPI'da 1 fiziksel piksel. Sınır çizgileri rect'in içine (inset 0.5).
- Yuvarlatılmış köşe: `FillRoundedRectangle` r = 2/3/4; radius ölçeklenir ama 1px altına inmez.
- Kırpma: `PushAxisAlignedClip` (liste yüzeyleri), `PushLayer` yalnızca ellipsis metin ve yuvarlatılmış clip için.

## DirectComposition görsel ağacı (öneri)
```
Root (opaque, bg.base)
├─ TitleBarVisual (32)            — nadiren yeniden çizilir
├─ NavVisual (200/44)             — width animasyonu: Clip + Offset animasyonu
├─ ContentVisual
│   ├─ HeaderVisual (başlık+araç çubuğu)
│   ├─ ListVisual (sanal liste, kendi swapchain veya surface)  — kaydırmada Offset animasyonu
│   └─ InspectorVisual (280)      — Clip/Offset animasyonu
├─ StatusBarVisual (24)
├─ OverlayVisual (flyout/menu/tooltip, elevation.menu gölgesi ayrı visual)
├─ ScrimVisual (opacity 0→.6)
├─ DialogVisual (dialog / command palette)
└─ ToastVisual
```
Animasyonlar `IDCompositionAnimation` ile (opacity, offset, clip, scale). Renk geçişleri (80 ms) D2D'de her karede lerp — sadece hover'daki tek eleman için yeniden çizim (dirty rect).

## Bileşen çizim adımları
**Button** — 1) FillRoundedRectangle zemin (primary/danger) 2) sınır (secondary): DrawRoundedRectangle inset .5, 1px 3) ikon: path geometry translate((h−16)/2) 4) metin: DrawTextLayout ortalanmış (dikey: (h−lineHeight)/2) 5) focus: DrawRoundedRectangle rect+1, r+1, accent.
**Checkbox** — 12×12 kutu; on: FillRoundedRectangle accent + tik path geometry stroke 1.5 round; ind: sınır accent + FillRectangle 6×2. Hit alanı 16 (16×16 şeffaf rect).
**Toggle** — 24×12 ray + 8×8 knob; knob x animasyonu 140 ms.
**TextBox** — bg.input FillRoundedRectangle, sınır 1px (state rengi), ikon, metin (DWrite layout, tek satır, TRIMMING_CHARACTER yok — kaydırılabilir), imleç 1px accent 500 ms yanıp sönme, seçim accent.subtle.
**Menu** — 1) gölge: ayrı visual, `ID2D1Effect` Shadow (blur = blurRadius/2 std sapma) iki katman (elevation tokenları) 2) FillRoundedRectangle bg.overlay 3) sınır line.strong 4) öğeler 24px; hover bg.raised r3.
**Tree/DataGrid satırı** — arka plan (hover/selected) → girinti → chevron → checkbox → ikon → metin (ellipsis: `DWRITE_TRIMMING_GRANULARITY_CHARACTER` + ellipsis sign) → sağ sütunlar (sağa hizalı TextLayout). Sanal: yalnızca görünür + 8 satır; satır yüksekliği sabit → offset = index×row.
**InfoBar** — FillRectangle status.*Subtle (opak), üst/alt 1px line.subtle, ikon status rengi, strong + body layout tek satır.
**Progress** — 2px ray line.strong + dolgu accent; indeterminate segment offset animasyonu.
**Spinner** — 8 çizgi path; her 100 ms rotate 45° (steps) — DComp Rotation3D yerine 8 önceden dönmüş geometri.
**Scrim + Dialog** — ScrimVisual opacity anim; dialog visual offset −8→0 + opacity; içeriği D2D.
**Title bar caption** — 46×32 rect hover bg.raised / close status.error; ikon 10px path 1px stroke ortalı (offset 18,11). HitTest: HTMINBUTTON/HTMAXBUTTON/HTCLOSE döndür → Snap Layouts hover çalışır.
**Toast** — elevation.toast gölge + kutu; 4 s timer; hover'da timer durur.
**Log** — mono TextLayout satır başına cache (renkli aralıklar `SetDrawingEffect` ile fırça); yalnızca görünür satırlar.

## Gölge (elevation) uygulaması
Her seviye iki katman: {offsetX, offsetY, blurRadius, color}. D2D: kutuyu siyah doldurulmuş bitmap'e çiz → `CLSID_D2D1Shadow` (BlurStandardDeviation = blurRadius/3) → color matrix ile renk → offset ile çiz; iki kez (key, ambient). Cache: boyut değişmedikçe bitmap yeniden üretilmez.

## Tema değişimi
Tema = fırça tablosu; tüm visual'lar invalidate. Geçiş animasyonu yok (anlık). High Contrast: sistem HC açıksa `tokens.color.hc` + sistem renkleri eşlemesi (accessibility.md), tüm 1px çizgiler 2px.

## Performans hedefleri
İlk kare < 150 ms; kaydırma 60/120 Hz'de düşürmesiz (satır cache); hover yeniden çizimi ≤ 1 ms (dirty rect); bellek: metin layout cache LRU 2000 satır.
