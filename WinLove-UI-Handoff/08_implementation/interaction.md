# Etkileşim

## Pointer state'leri
rest → hover (80 ms renk) → pressed (down, 80 ms) → rest/hover (up, 140 ms). Tıklama up'ta tetiklenir; pointer dışarı çıkarsa iptal. Sağ tık: down'da context menu (Windows standardı up — burada up kullan). Çift tık: tree expand, tablo aç, splitter reset.

## Odak
- FocusVisible yalnızca klavye gezinmesinde; fare ile odak halkası çizilmez.
- Sıra (Tab): TitleBar (komut tetikleyicisi → caption) → Nav (tek durak; ↑↓ içinde) → içerik başlık butonları → araç çubuğu (soldan sağa) → liste/ağaç (tek durak; ↑↓ satır, →← expand, Space checkbox, Enter aç/seç) → inspector (bölümler, butonlar) → StatusBar CTA. Shift+Tab ters. F6 bölgeler arası atlar.
- Dialog açıkken odak dialog içinde döner; ilk odak iptal butonu (tehlikeli) ya da ilk alan (form). Kapanışta tetikleyiciye döner.
- Odak halkası: 1px accent.focus, offset 1, radius +1 (HC: 2px #FFFFFF).

## Kısayollar
| Kısayol | Eylem |
|---|---|
| Ctrl K | Komut paleti |
| Ctrl B | Nav daralt/genişlet |
| Ctrl 1…9 | Nav öğesi (görünüm sırasına göre) |
| / | Sayfa aramasına odak |
| Ctrl F | Log araması / sayfa araması |
| Esc | Palet/dialog/menü kapat → seçimi temizle → inspector kapat |
| Space | Checkbox / toggle |
| Enter | Birincil eylem, satır aç |
| → ← | Tree expand/collapse |
| Ctrl Enter | Kuyruğu uygula (onay dialoguna gider) |
| Ctrl Shift D | Seçili bileşenleri kuyruğa ekle |
| Ctrl Shift T | Tema koyu/açık |
| Ctrl , | Uygulama ayarları |
| Ctrl S / Ctrl O | Preset kaydet / yükle |
| Ctrl Z / Y | Kuyruk değişikliğini geri al / yinele |
| Alt ↓ | Dropdown / split-button menüsü |
| F2 | Yeniden adlandır (preset) |
| Del | Kuyruktan çıkar / listeden sil |
| Ctrl C | Seçili satır(lar)ı kopyala (tab-ayrılmış) |
| Alt Space | Pencere menüsü (custom title bar'da yeniden yönlendir) |

## Drag & drop
- Dosya bırakma: pencere geneli `IDropTarget`; ilgili sayfa DropZone vurgusu (accent sınır + accent.subtle). ISO/WIM/ESD/SWM/VHD → Kaynak; MSU/CAB → Güncellemeler; INF klasör → Sürücüler; .reg → Kayıt Defteri; .xml → Katılımsız; .wlpreset → Presetler. Geçersiz tür: status.error sınır + "desteklenmeyen dosya".
- Liste sıralama (güncellemeler, kurulum sonrası): drag-handle'dan; sürüklenen satır opacity .6 + elevation.menu, hedef konumda 2px accent çizgi; 24px adım snap; kaydırma kenarda otomatik.
- Splitter: bkz. layout-system.md.

## Title bar (custom)
`WM_NCCALCSIZE` ile çerçeve 0; `WM_NCHITTEST`: caption alanı HTCAPTION, min/max/close butonları HTMINBUTTON/HTMAXBUTTON/HTCLOSE (Snap Layouts hover'ı için zorunlu), üst 4px HTTOP (resize), komut tetikleyicisi HTCLIENT. Çift tık caption → maximize. Maximize'da caption ikonu restore'a döner; pencere kenarında 8px DWM görünmez sınır korunur.

## Kuyruk modeli
Her değişiklik önce kuyruğa girer (nav rozetleri + status "Kuyruk n"). "Uygula" → 13 özet → tehlikeli işlemler varsa 13b onay → 14 çalışıyor (Durdur: geçerli DISM işlemi bitince durur, imaj bağlı kalır) → 15 tamamlandı. Hata: adım listesinde error-octagon, InfoBar error, toast; "Yeniden dene" / "Atla ve devam".

## Tooltip
400 ms gecikme, pointer 4px hareketle iptal, 8 s sonra kapanır; klavye odağında 800 ms sonra görünür. Kısayol tooltip metninin sonunda parantez içinde ("Daralt (Ctrl B)").
