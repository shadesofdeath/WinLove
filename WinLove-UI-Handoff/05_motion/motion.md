# Motion — sayısal spec

Easing: standard (0.2, 0, 0, 1) · decelerate (0, 0, 0, 1) · accelerate (0.4, 0, 1, 1) · spring {stiffness 380, damping 34, mass 1}. Süre tokenları: fast 80 · base 140 · slow 220 · enter 180 · exit 120.

| Geçiş | Tetikleyici | Özellikler | Süre / easing / gecikme | Reduce motion |
|---|---|---|---|---|
| Sayfa geçişi | nav tık, palet, kısayol | eski: opacity 1→0 (exit 120 accelerate); yeni: opacity 0→1 + translateY 4→0 | 120 + 180, decelerate, yeni 40 ms gecikme | yalnızca opacity 80 ms |
| Inspector aç/kapa | seçim / Esc / toggle | width 0↔280 (içerik clip), opacity 0↔1 | 220 standard | anlık, opacity 80 |
| Nav rail genişleme | Ctrl B / footer | width 200↔44 (spring), etiket opacity 0↔1 (80 ms, genişlerken 100 ms gecikme) | spring 380/34/1 (~260 ms) | anlık |
| Hover | pointer enter/leave | background rengi rest↔bg.raised | 80 standard | 0 |
| Press | pointer down/up | background bg.raised→bg.pressed; primary accent→accent.pressed | 80 / kalkışta 140 | 0 |
| Focus ring | klavye odak | outline opacity 0→1 | 80 | 0 |
| Checkbox | toggle | zemin rengi 80; tik path stroke-dashoffset 1→0 140 decelerate; indeterminate çubuk scaleX 0→1 140 | 140 | anlık |
| Toggle | toggle | knob translateX 1↔13 (140 standard), zemin rengi 80 | 140 | anlık |
| Tree expand/collapse | chevron / dblclick / → ← | chevron rotate 0↔90 (140), çocuk satırlar height 0→n×24 + opacity (140 decelerate; 12'den fazla satırda yalnızca opacity) | 140 | anlık |
| Toast giriş | olay | opacity 0→1, translateX 16→0 | 180 decelerate | opacity 80 |
| Toast çıkış | 4000 ms sonra / kapat | opacity 1→0 | 120 accelerate | opacity 80 |
| Dialog açılış | onay gerektiren aksiyon | scrim opacity 0→0.6 (180); dialog opacity 0→1 + translateY −8→0 + scale 0.98→1 (180 decelerate) | 180 | opacity 80 |
| Dialog kapanış | Esc / buton | ters, 120 accelerate | 120 | opacity 80 |
| Command palette | Ctrl K | dialog ile aynı, translateY −8 | 180 | opacity 80 |
| Progress indeterminate | görev başladı | 25% segment translateX −25%→100% | 1400 standard, sonsuz | statik %25 segment, opacity 0.5↔1 1400 |
| Spinner | yükleniyor | rotate steps(8) | 800 ms/tur | 8 dilimin opacity nabzı 1400 |
| "Uygula" dikkat durumu | kuyrukta >0 değişiklik | box-shadow 0 0 0 0 → 0 0 0 2px accent.subtle, geri | 2400 ease-in-out sonsuz; ilk değişiklikte 1 kez 140 ms scale 1→1.04→1 | statik; yalnızca rozet |
| Splitter sürükleme | drag | panel width anlık; bırakınca snap (spring) | spring | anlık |
| Scrollbar | hover / scroll | width 4↔8, thumb rengi line.strong↔text.tertiary | 140 | anlık |
| Tooltip | hover 400 ms | opacity 0→1 | 80 | 0 |
| Skeleton | yükleme | opacity 0.6↔1 | 1200 ease-in-out sonsuz | statik 0.8 |

Kurallar: aynı anda en fazla iki özellik animasyonlanır (opacity + transform). Renk geçişleri yalnızca 80 ms. Layout (width/height) animasyonu yalnızca panel ve tree'de; DirectComposition'da bu iki durum için clip + translate kullanılır, gerçek relayout tek karede yapılır.
