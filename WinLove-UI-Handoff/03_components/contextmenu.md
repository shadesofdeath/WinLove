# ContextMenu · MenuFlyout · Tooltip · Kbd
## Menü
- Zemin `bg.overlay`, 1px `line.strong`, radius r3 (4), `elevation.menu`, padding 4, min genişlik 220, max 360.
- Öğe 24px: ikon 16 (yoksa 16 boşluk) + 8 gap + etiket + sağda kısayol (kbd) veya alt menü chevron-right. hover/klavye: `bg.raised`, radius 3. disabled: opacity .45. Ayırıcı 1px line.subtle, 4px dikey margin. Grup başlığı type.section 24px.
- Alt menü sağa 4px bindirmeli açılır, 120 ms gecikme. Açılış 140 ms opacity + translateY −4.
- Konum: imleç/tetikleyicinin sağ-altı; ekran dışına taşarsa ters çevir.
## Tooltip
- 400 ms gecikme, 20px yüksek, padding 2 6, 11px, zemin bg.overlay, 1px line.strong, radius r2, elevation.menu. Hedefin 4px altında, sola hizalı. Zengin tooltip: padding 6 8, başlık bodyStrong + caption satırları, max 280.
- Icon-only her buton ve kesilmiş her metin (ellipsis) tooltip alır.
## Kbd
- 14px yüksek, padding 0 3, 10px mono, 1px line.strong, radius r1, renk text.tertiary. Tuşlar arası 2px.
