# Layout sistemi

## Pencere
- Min 1100×700; referans 1440×900 ve 1920×1080. Tüm ölçüler DIP (96 DPI). Ölçekleme: `GetDpiForWindow` → tüm DIP değerleri D2D transform ile ölçeklenir; **piksel hizalama** için çizgi koordinatları `floor(v*scale)+0.5/scale`.
- Izgara: 4px temel, satır ritmi 24 (comfortable 28). Bölümler arası ≤ 24.

## Bölgeler (soldan sağa, üstten alta)
| Bölge | Ölçü | Zemin | Davranış |
|---|---|---|---|
| TitleBar | h 32 | bg.panel | client alanına genişletilmiş (DWMWA_EXTEND / WM_NCCALCSIZE 0 çerçeve); üst 1px pencere kenarı DWM'den. |
| NavigationRail | w 200 / 44 | bg.panel | Ctrl B ile geçiş; < 1200 genişlikte otomatik 44 (kullanıcı geri açabilir, tercih kalıcı). |
| Content | esnek, min 560 | bg.base | padding 16; başlık bloğu 16+22+2+16; araç çubuğu 12 üst/alt. |
| Inspector | w 280 (240–480 splitter) | bg.panel | Esc/close ile kapanır; sayfa başına hatırlanır; < 1280 genişlikte içerik üstüne overlay (elevation.menu) olarak açılır. |
| StatusBar | h 24 | bg.panel | her zaman görünür; CTA sağda. |

Ayırıcı çizgiler 1px `line.subtle`: title alt, nav sağ, inspector sol, status üst.

## Breakpoint'ler
- ≥ 1440: tam düzen. 1200–1439: inspector 280 sabit, içerik daralır. 1100–1199: nav 44, inspector overlay. Yükseklik < 800: başlık açıklaması gizlenir, araç çubuğu 8 padding.

## Splitter
- Yalnızca inspector kenarı (ve 11/14 ekranlarında iç bölme). 6px hit alanı ayırıcı çizgi üstünde ortalı. Sürüklerken canlı relayout (throttle 1 kare), bırakınca 8px snap noktaları (240/280/320/…). Çift tık = varsayılan. İmleç `IDC_SIZEWE`.

## Yoğunluk
- Compact: row 24, control 24. Comfortable: +4 (28/28); ikon ve yazı boyutu değişmez; nav öğesi 28, sütun başlığı 24 sabit.

## Kaydırma
- İçerik listeleri kendi kaydırıcısında (başlık ve araç çubuğu sabit). Overlay scrollbar, sağ 2px boşluk. Tekerlek 3 satır (72px), Shift+tekerlek yatay.

## Metin kesme
- Ad sütunları ellipsis + tooltip; sayısal sütunlar asla kesilmez (min genişlik). Sabit yükseklikli kontrollerde tek satır (nowrap).

## Z-order
tokens.zOrder: content 0 → panels 10 → splitter 20 → statusBar 30 → titleBar 40 → flyout 100 → menu 110 → tooltip 120 → dialogScrim 200 → dialog 210 → commandPalette 220 → toast 300.
