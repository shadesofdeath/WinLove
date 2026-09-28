# TextBox · SearchBox · NumberBox · ComboBox/Dropdown
Ortak: yükseklik 24, radius r2, zemin `bg.input`, sınır 1px `line.strong`, iç padding 0 6, metin type.body `text.primary`, placeholder `text.tertiary`. hover: sınır text.tertiary. focused: sınır `accent.base` + 1px halka offset 1. error: sınır `status.error`, altında 11px `status.error` mesaj (16px satır). disabled: opacity .45. readonly: zemin bg.base, sınır line.subtle.
- **SearchBox:** solda search ikonu 16 (text.tertiary), sağda kbd ipucu ("/", "Ctrl F"); yazı varken kbd yerine close ikonu (temizle). Genişlik 240 varsayılan.
- **NumberBox:** sağda 16 genişlik dikey ok çifti (chevron-up/down 11px yüksek), 1px line.strong ayırıcı; Ctrl+tekerlek ×10.
- **ComboBox:** solda etiket (text.secondary) + değer (text.primary) + chevron-down (text.tertiary); açıkken sınır accent, menü ContextMenu ile aynı (bkz. contextmenu.md), seçili öğede check ikonu. Genişlik içerikten; formda 280 sabit.
- Tablo içi kompakt varyant: 20px yüksek, 11px metin.
Klavye: Tab odak; ↑↓ combobox açar/gezer; Esc iptal; Enter onaylar.
