# Checkbox (tri-state) · ToggleSwitch · RadioButton
## Checkbox
- Görsel kutu 12×12, radius r1 (2); hit alanı 16×16 (satır içinde tüm satır tıklanabilir, checkbox kısmı toggle).
- off: 1px `line.strong` sınır, şeffaf. hover: sınır text.tertiary. on: zemin `accent.base`, tik 1.5 stroke `text.onAccent` path `M2.5 6l2.5 2.5 4.5-5` (12 grid). indeterminate: 1px `accent.base` sınır + içte 6×2 `accent.base` çubuk. disabled: opacity .45. Focus: 1px `accent.focus` çerçeve, kontrolün dışına 1px offset, radius = kontrol radius + 1. Yalnızca klavye odağında (FocusVisible).
- Tri-state kuralı: ebeveyn = çocukların türevi (hepsi on → on; hiçbiri → off; karışık → ind). Ebeveyne tık: ind/off → tüm çocuklar on; on → tüm çocuklar off.
- Motion: zemin 80 ms; tik dashoffset 140 ms decelerate.
## ToggleSwitch
- 24×12, radius r1; knob 8×8 radius 1. off: sınır line.strong, knob text.secondary, x=1. on: zemin accent.base, knob text.onAccent, x=13. Geçiş 140 ms standard. Etiket solda (form) veya sağda (araç çubuğu), 8px gap.
## RadioButton
- 12 çap daire, 1px line.strong; seçili: 1px accent.base + 6 çap accent.base iç nokta. Seçenekler yatay 16px gap veya dikey 24px satır.
