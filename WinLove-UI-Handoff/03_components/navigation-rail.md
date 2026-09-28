# NavigationRail
- Genişlik 200 (genişletilmiş) / 44 (daraltılmış); zemin `bg.panel`, sağda 1px `line.subtle`. Gruplar 1px line.subtle ile ayrılır (4px padding üst/alt).
- Öğe 24px: sol padding 16, ikon 16 (text.secondary), 8 gap, etiket body, sağda rozet mono 11 `text.tertiary` (12 padding). hover: metin text.primary. active: zemin `bg.raised` (inset 4px yatay, radius 3) + 2×12 `accent.base` çubuk x=4, ikon filled varyant, metin text.primary.
- Daraltılmış: yalnızca ikon (44 içinde ortalı), rozet 6px accent nokta (sağ üst), tooltip etiket + rozet.
- Alt: "Daralt" 24px (sidebar-toggle ikonu + kbd Ctrl B).
- Bekleyen değişiklik rozeti: sayfa kuyruğundaki değişiklik sayısı; 0 ise gizli.
- Klavye: ↑↓ gezinme, Enter/Space seçim, Ctrl+1…9 doğrudan. Genişleme: spring (motion.md).
