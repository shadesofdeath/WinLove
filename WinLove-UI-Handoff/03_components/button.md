# Button
Varyantlar: **primary (accent)**, secondary, subtle, danger, icon-only, split-button. State'ler: rest · hover · pressed · focused · disabled · loading.

## Anatomi / ölçü
- Yükseklik `size.control` 24 (small 20, large 28 — yalnızca dialog birincil butonu). Genişlik içerikten; min 56.
- Padding 0 `space.s4` (8); ikon varsa ikon 16 + gap 6, sol padding 8.
- Radius `radius.r2` (3). Metin `type.body`; primary/danger `type.bodyStrong`.
- Icon-only: 24×24, ikon ortada, **tooltip zorunlu**.
- Split: ana 24 + ok bölümü 20 genişlik, ortada 1px `line.strong` (aynı border), ok ikonu chevron-down.

## Renk tokenları
| Varyant | rest | hover | pressed | metin | sınır |
|---|---|---|---|---|---|
| primary | accent.base | accent.hover | accent.pressed | text.onAccent | yok |
| secondary | şeffaf | bg.raised | bg.pressed | text.primary | line.strong |
| subtle | şeffaf | bg.raised | bg.pressed | text.secondary → hover text.primary | yok |
| danger | status.error | status.error (+hoverOverlay eşdeğeri: %6 beyaz karışımı, opak hesapla) | koyu %10 | #FFFFFF | yok |
| disabled | opacity 0.45 (tümü) | — | — | — | — |

Focus: 1px `accent.focus` çerçeve, kontrolün dışına 1px offset, radius = kontrol radius + 1. Yalnızca klavye odağında (FocusVisible).
Loading: metin yerine 16 spinner + etiket ("Bağlanıyor…"), tıklanamaz, opacity 1, renk text.secondary.
Ekranda tek primary; uygulama CTA'sı status bar'da 20px yüksek primary ("Uygula · n").
Klavye: Space/Enter tetikler; split-button Alt+↓ menüyü açar.
