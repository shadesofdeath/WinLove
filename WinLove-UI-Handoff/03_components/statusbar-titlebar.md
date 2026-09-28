# StatusBar · TitleBar · Breadcrumb · Tabs/SegmentedControl
## TitleBar (32, client alan)
bg.panel, alt 1px line.subtle. Sol: 10 padding, mark 16 (accent), 8 gap, "WinLove" bodyStrong, 1px×12 line.strong ayırıcı, breadcrumb. Orta: komut paleti tetikleyicisi 280×20 (bg.base, 1px line.strong, r2, search ikonu, placeholder 11px, kbd Ctrl K); hover sınır text.tertiary. Sağ: caption butonları 46×32: minimize (10px çizgi), maximize/restore (9px kare / iki kare), close (X); ikon 1px stroke text.secondary; hover bg.raised, close hover status.error + beyaz ikon. Snap Layouts: maximize butonunun rect'i `WM_NCHITTEST` → HTMAXBUTTON döndürür (bkz. interaction.md). Sürükleme alanı: buton ve tetikleyici dışındaki her yer (HTCAPTION). Pencere pasifken metin/ikonlar text.tertiary.
## Breadcrumb
body; öğeler text.secondary, son öğe text.primary; ayırıcı "›" text.tertiary, 6px gap. Tık = o seviyeye git (imaj › sürüm › durum). Uzun adlar ortadan kesilir (max 180).
## StatusBar (24)
bg.panel, üst 1px line.subtle, 12 sol padding, 12 gap, 1px×12 line.strong ayırıcılar; 11px text.secondary etiketler, mono değerler text.primary. Segmentler: mount durumu (6px nokta success/tertiary + yol mono text.tertiary) · imaj boyutu · kazanç · kuyruk. Sağ: arka plan görev (etiket + 80×2 bar + %) ve CTA (20px primary, 4px sağ boşluk). CTA kuyruk 0 ise disabled (bg.raised, text.disabled); çalışırken "Durdur" (secondary görünüm). Segment tık: mount → İmajlar, kuyruk → Uygula.
## Tabs / SegmentedControl
24px, padding 0 12, text.secondary; aktif: bg.raised (r 3 3 0 0) + alt 2px accent (8px içerden), text.primary medium. Alt 1px line.subtle tüm genişlikte. SegmentedControl (ISO/USB gibi 2–3 seçenek): aynı ölçü, 1px line.strong dış çerçeve r2, aktif segment bg.raised; alt çizgi yok.
