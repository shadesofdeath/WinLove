# TreeView — bileşen kaldırma ekranının kalbi
## Satır (24, comfortable 28)
| x | Öğe | Genişlik | Not |
|---|---|---|---|
| 16 + d·16 | chevron | 16 | yalnızca çocuklu düğüm; chevron-right/down text.tertiary; tık = expand |
| +4 | checkbox | 16 (12 görsel) | tri-state, bkz. checkbox.md |
| +6 | ikon | 16 | folder (grup) / appx-package / özel (defender-shield…) text.secondary |
| +6 | ad | esnek | body; kök düğümler bodyStrong; ellipsis + tooltip |
| sağdan 96+72+16 | risk | 96 | 6×6 kare (r1) status.success/warning/error + 11px etiket; grupta "n öğe" text.tertiary |
| sağdan 72+16 | boyut | 72 | mono 11 text.secondary, sağa hizalı (tabular) |
- Girinti 16/seviye. Sütun başlığı 24px, 11px medium text.tertiary, alt 1px line.subtle; "Ad" başlığı x=72.
## State
- rest şeffaf · hover `bg.raised` (radius 3, satır 16px inset) · selected `accent.subtle` (opak) · selected+hover accent.subtle · focused 1px accent inset çerçeve · disabled opacity .45 (koruma altındaki bileşen: lock ikonu + tooltip).
- Arama vurgusu: eşleşen alt dize `accent.subtle` zemin, radius 2, metin rengi değişmez; eşleşmeyen dallar gizli, ebeveynler açık; sağda "m / n eşleşme".
- Lazy-load: çocuklar okunurken satır: spinner 16 (accent) + "Alt öğeler okunuyor…" text.secondary.
- Risk rozeti tıklanınca tooltip: nedeni (bağımlılık sayısı, geri alınamaz).
## Etkileşim
- Tık satır → seç + inspector. Çift tık / → ← → expand/collapse. Space → checkbox. Shift+tık aralık, Ctrl+tık çoklu (inspector "n öğe" özet). Sağ tık → ContextMenu (Kuyruğa ekle, Bağımlılıkları göster, Kopyala, Grubu koru).
- Sanal kaydırma: 24px sabit satır; görünür + 8 satır buffer.
- Motion: expand 140 ms (motion.md).
