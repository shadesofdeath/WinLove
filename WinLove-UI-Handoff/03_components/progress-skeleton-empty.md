# ProgressBar · ProgressRing · Adım listesi · Skeleton · EmptyState · Badge/Chip · Splitter · ScrollBar
- **ProgressBar:** 2px, zemin line.strong, dolgu accent.base (status bar'da text.secondary). Indeterminate: %25 segment 1400 ms. Etiketler: solda açıklama 11px, sağda mono % / boyut.
- **ProgressRing / spinner:** 16 (24 boş durum), 8 dilim, 1.25 stroke accent; 800 ms steps(8).
- **Adım listesi:** 32px satır (2 satır metin: body + caption text.tertiary), ikon 16: success-circle (success) / spinner (accent) / queue-clock (text.tertiary) / error-octagon (error); aktif satır accent.subtle zemin r2.
- **Skeleton:** bg.raised bloklar, radius 2, 10px yüksek metin yerine, 12 checkbox yerine; opacity 0.6↔1 1200 ms.
- **EmptyState:** ortalı; ikon 24 text.tertiary, 16 gap, bodyStrong başlık, caption açıklama (text.secondary), 12 gap, secondary buton. Toplam yükseklik ≤ 140.
- **Badge/Counter:** nav'da mono 11 text.tertiary düz metin; CTA'da 12px bodyStrong etiket içinde ("Uygula · 35"); daraltılmış nav'da 6px accent nokta. Pill yok.
- **Chip/Tag:** chip 24px, 1px line.strong (aktif filtre: accent), r2, padding 0 8, kapatılabilir: close 16 sağda (4 padding). Tag 16px, 11px, 1px line.strong, r1, padding 0 4.
- **Splitter:** 1px line.subtle görünür, 6px hit; hover line.strong; sürüklerken accent; çift tık = varsayılan genişlik. Inspector 240–480, nav sabit.
- **ScrollBar:** overlay, 4px rest (thumb line.strong, r1), hover/scroll 8px (thumb text.tertiary), ok butonu yok, 2px kenar boşluğu; 600 ms sonra 4px'e döner.
