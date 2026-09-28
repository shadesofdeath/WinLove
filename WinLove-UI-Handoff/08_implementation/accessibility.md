# Erişilebilirlik

## UI Automation rolleri
| Bileşen | ControlType | Pattern | Ad / Değer |
|---|---|---|---|
| Button | Button | Invoke | etiket; icon-only → tooltip metni |
| Split-button | SplitButton | Invoke + ExpandCollapse | |
| ToggleSwitch | Button | Toggle | On/Off |
| Checkbox | CheckBox | Toggle (tri-state: Indeterminate) | |
| RadioButton | RadioButton | SelectionItem | |
| TextBox/SearchBox | Edit | Value, Text | placeholder → HelpText |
| NumberBox | Spinner | RangeValue | |
| ComboBox | ComboBox | ExpandCollapse, Selection, Value | |
| Menü | Menu / MenuItem | Invoke, ExpandCollapse | kısayol → AcceleratorKey |
| Tooltip | ToolTip | — | |
| NavigationRail | Tab / TabItem (List) | SelectionItem | rozet → ItemStatus ("14 bekleyen") |
| Tabs | Tab / TabItem | SelectionItem | |
| Breadcrumb | List / ListItem + Invoke | | |
| TreeView | Tree / TreeItem | ExpandCollapse, SelectionItem, Toggle (checkbox), ScrollItem | ad; ItemStatus "Risk yüksek, 1,12 GB, kuyrukta" |
| DataGrid | DataGrid / DataItem | Grid, Table, Selection, Scroll | başlık → ColumnHeader |
| InfoBar | StatusBar (ya da Group, LiveSetting=Assertive error/Polite diğer) | | |
| Toast | Window → Text, LiveSetting Polite | | |
| Dialog | Window (IsModal) | Window | ilk odak; Esc kapatır |
| ProgressBar | ProgressBar | RangeValue (indeterminate → IsReadOnly, LiveSetting) | |
| StatusBar | StatusBar | | segmentler Text |
| TitleBar caption | Button (Name: Simge durumuna küçült…) | Invoke | |
| LogConsole | Document (Text pattern) | | yeni satır LiveSetting Polite (ERR Assertive) |
| Command Palette | Window + Edit + List | Selection | |
| Splitter | Thumb | Transform/RangeValue | |
| Kbd | Text | | "Kontrol K" okunuşu için AcceleratorKey |

Ekran okuyucu metinleri `07_copy/strings.*.json` anahtarlarından; ikon-only kontrollerin Name'i tooltip ile aynı. Risk ve durum: renk + ikon + metin; Name'e metin dahil ("Yüksek risk").

## Kontrast
Tablo `01_tokens/tokens.md`. Koyu: text.primary/bg.base ≥ 13:1, text.secondary ≥ 6:1, text.tertiary ≈ 3.4:1 (yalnızca ikincil etiket/sayaç, ≥ 3:1 hedefi). Açık: text.primary ≥ 14:1, text.secondary ≥ 6:1. Vurgu üzerinde metin: koyu 8.5:1, açık 5.5:1. Odak halkası/ikon ≥ 3:1 her iki temada. Devre dışı (opacity .45) kontrast garantisi vermez (WCAG istisnası).

## High Contrast
Sistem HC açıkken `tokens.color.hc` devreye girer ve şu sistem renkleri eşlenir: bg.* ← Window, text.* ← WindowText, line.* ← WindowText, accent.base ← Highlight, text.onAccent ← HighlightText, accent.focus ← WindowText, disabled ← GrayText, status.error ← #FF5050 (sabit), status.success ← #3FF23F, status.warning ← #FFFF00. Kurallar: tüm 1px çizgiler 2px; gölge/scrim yok (dialog 2px sınır); seçili satır Highlight zemin + HighlightText; odak 2px WindowText; ikonlar currentColor. Tema seçiciden manuel HC de mümkün.

## Hareket
`SPI_GETCLIENTAREAANIMATION` false ise reduce motion: motion.md fallback sütunu. Spinner opacity nabzı; CTA nabzı kapalı.

## Klavye
Tüm işlevler klavyeyle erişilir (interaction.md). Odak sırası görsel sırayla aynı; odak halkası her kontrolde. Tek harf kısayolları ("/") yalnızca metin alanı odakta değilken.

## Metin ölçekleme
Sistem metin ölçeği (%100–225) DIP ölçekleyicisine çarpan olarak uygulanır (yalnızca metin ve ikonlar; yükseklikler satır yüksekliğini aşarsa comfortable yoğunluğa geçilir). 11px altına asla inilmez.

## Renk körlüğü
Risk paleti (yeşil/amber/kırmızı) her zaman etiket ve ikonla; diff işaretleri (+ − ~) renkten bağımsız. Deuteranopi kontrolü: success/error arasında parlaklık farkı ≥ 1.4:1 tutuldu.
