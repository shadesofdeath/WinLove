# tokens.md — WinLove token sözlüğü

Tek kaynak: `tokens.json`. C++ tarafı `tokens.h` (namespace `wl::tokens`). Token yolu `wl.color.bg.base` ↔ `wl::tokens::color::dark::bg_base`.

## Renk

| Token | Kullanım |
|---|---|
| bg.base | Pencere ve ana içerik zemini |
| bg.panel | Title bar, nav, inspector, status bar (bir ton kalkık) |
| bg.raised | Hover satırı, aktif nav, tab zemini |
| bg.pressed | Basılı buton/satır |
| bg.overlay | Menü, flyout, dialog, toast zemini |
| bg.input | TextBox/ComboBox iç zemini (bg.base'den bir ton çukur) |
| line.subtle | Panel ayırıcıları, satır çizgileri |
| line.strong | Kontrol sınırları, kbd, splitter hover |
| text.primary | Gövde, başlık |
| text.secondary | Açıklama, ikincil sütun, pasif nav |
| text.tertiary | Sayaç, placeholder, chevron, sütun başlığı |
| text.disabled | Devre dışı metin/ikon |
| text.onAccent | Vurgu zemininde metin (primary buton, seçili checkbox tik) |
| accent.base | Seçim, odak, birincil aksiyon, aktif nav çubuğu |
| accent.hover / pressed | Primary buton hover/pressed |
| accent.subtle | Seçili satır zemini (opak, blend yok) |
| status.* | Durum ve risk: success=düşük, warning=orta, error=yüksek |
| status.*Subtle | InfoBar zemini |
| scrim | Dialog arkası karartma |
| shadow.key / ambient | Elevation iki katmanı |

## Kontrast (WCAG) — koyu / açık

| Ön | Arka | Hedef | Koyu | Açık |
|---|---|---|---|---|
| text.primary | bg.base | AA 4.5 | 14.27:1 | 14.86:1 |
| text.primary | bg.panel | AA 4.5 | 13.51:1 | 13.60:1 |
| text.primary | bg.raised | AA 4.5 | 12.23:1 | 12.40:1 |
| text.secondary | bg.base | AA 4.5 | 6.40:1 | 5.99:1 |
| text.secondary | bg.panel | AA 4.5 | 6.06:1 | 5.48:1 |
| text.tertiary | bg.base | 3.0 (ikincil ikon/etiket) | 3.20:1 | 3.63:1 |
| text.onAccent | accent.base | AA 4.5 | 6.97:1 | 4.97:1 |
| accent.base | bg.base | 3.0 (ikon/odak) | 6.63:1 | 4.46:1 |
| status.success | bg.base | 3.0 | 7.16:1 | 4.56:1 |
| status.warning | bg.base | 3.0 | 8.27:1 | 4.92:1 |
| status.error | bg.base | 3.0 | 4.89:1 | 5.08:1 |
| status.info | bg.base | 3.0 | 6.95:1 | 4.86:1 |
| line.strong | bg.base | — (1px sınır, oran gerekmiyor) | 1.64:1 | 1.67:1 |

High Contrast: tüm metin #FFFFFF / #000000 (21:1); vurgu #1AEBFF, odak #FFFFFF 2px. Windows HC teması etkinse sistem renkleri (`SystemColors`) bu tokenları ezer — eşleme accessibility.md'de.

## Tipografi

| Token | Aile | Boyut/Satır | Ağırlık (DWRITE) | Harf aralığı |
|---|---|---|---|---|
| type.title | IBM Plex Sans | 16 / 22 | 600 (DWRITE_FONT_WEIGHT_SEMI_BOLD) | 0 DIP |
| type.bodyStrong | IBM Plex Sans | 12 / 16 | 500 (DWRITE_FONT_WEIGHT_MEDIUM) | 0 DIP |
| type.body | IBM Plex Sans | 12 / 16 | 400 (DWRITE_FONT_WEIGHT_NORMAL) | 0 DIP |
| type.section | IBM Plex Sans | 11 / 16 | 500 (DWRITE_FONT_WEIGHT_MEDIUM) | 0.22 DIP |
| type.caption | IBM Plex Sans | 11 / 16 | 400 (DWRITE_FONT_WEIGHT_NORMAL) | 0 DIP |
| type.mono | JetBrains Mono | 11 / 16 | 400 (DWRITE_FONT_WEIGHT_NORMAL) | 0 DIP |
| type.kbd | JetBrains Mono | 10 / 14 | 400 (DWRITE_FONT_WEIGHT_NORMAL) | 0 DIP |

Fallback: Segoe UI (ui), Cascadia Mono (mono). Font dosyaları OFL; paket TODO: `00_brand/fonts/` altına .ttf ekle.

## Boşluk / Radius / Ölçü

- spacing: 2 / 4 / 6 / 8 / 12 / 16 / 24 (s1…s7). Bölüm arası ≤ 24.
- radius: r1=2 (checkbox, kbd, badge), r2=3 (buton, input, seçili satır), r3=4 (menü, dialog, toast). Odak halkası: kontrol radius + 1, offset 1.
- size: control 24 (small 20, large 28), row 24 (comfortable 28, densityDelta 4), icon 16 (24 yalnızca boş durum/karşılama), titleBar 32, statusBar 24, nav 200/44, inspector 280, captionButton 46×32, checkbox 12, toggle 24×12, scrollbar 4→8.

## Elevation (iki katman, D2D drop shadow)

| Seviye | Katman 1 (key) | Katman 2 (ambient) |
|---|---|---|
| menu / flyout / tooltip | 0,1 blur 2 shadow.key | 0,4 blur 12 shadow.ambient |
| dialog / command palette | 0,2 blur 4 | 0,12 blur 32 |
| toast | 0,1 blur 2 | 0,6 blur 16 |

Paneller, kartlar, satırlar: gölge YOK. Ayrım line.subtle ve bg tonlarıyla.

## Motion

- Süreler: fast 80 (hover/press rengi), base 140 (checkbox, toggle, tree expand), slow 220 (panel aç/kapa, nav genişleme), enter 180 / exit 120 (dialog, toast, sayfa).
- Easing: standard cubic-bezier(0.2,0,0,1), decelerate (0,0,0,1) girişler, accelerate (0.4,0,1,1) çıkışlar. Spring (nav rail, splitter snap): stiffness 380, damping 34, mass 1.
- Reduce motion: tüm süreler → 0, opacity geçişleri 80ms'de kalır.

## Opacity / Z-order

- disabled 0.45 (metin ve ikon birlikte), hover overlay yerine doğrudan bg.raised tokenı kullan (opak zemin şartı).
- z: content 0 < panels 10 < splitter 20 < statusBar 30 < titleBar 40 < flyout 100 < menu 110 < tooltip 120 < dialogScrim 200 < dialog 210 < commandPalette 220 < toast 300.
