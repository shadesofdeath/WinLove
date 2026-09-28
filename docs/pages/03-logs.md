# P03 — Loglar

> Durum: 🟨 geliştirme bitti, kullanıcı testi bekliyor (2026-09-28). Tasarım: 18-logs.

## 1. Amaç
Uygulamanın oturum logunu (motor, DISM sarmalayıcısı, mount sağlığı, kilitler, arayüz) canlı göstermek,
filtrelemek, aramak, kopyalamak ve dışa aktarmak. Sonraki sayfalarda hata ayıklamanın ana aracı.

## 2. Ekran
- **Başlık eylemleri:** Temizle, Dışa aktar (ikonlu).
- **Araç çubuğu (24px):** SearchBox 240 ("Logda ara", Ctrl F ipucu) · Dropdown "Seviye" (Tümü / Hata ayıklama ve
  üzeri / **Bilgi ve üzeri** / Uyarı ve üzeri / Yalnız hatalar) · Dropdown "Kaynak" (Tümü + görülen etiketler) ·
  Toggle "Otomatik kaydır" · sağda "1.284 satır · son 14:22:20".
- **LogConsole** (kalan yükseklik): bg.input, satır 20, mono 11; zaman · seviye (renkli) · kaynak · mesaj;
  ERR satırı status.errorSubtle zemin + text.primary; arama eşleşmesi accent.subtle.

## 3. Davranış
- Kaynak: `AppState::logBuffer()` (süreç boyu `log::RingBufferSink`, 20 000 satır). Sayfa açıkken Shell 250 ms'de
  bir `poll()` çağırır; `RingBufferSink::since(version)` yalnız yeni satırları verir.
- Dosyaya da yazılır: `%LOCALAPPDATA%\WinLove\logs\WinLove-YYYYMMDD-HHMMSS.log` (dism.log yanında).
- **Otomatik kaydır:** yeni satırları izler; kullanıcı yukarı kaydırınca kapanır, "↓ n yeni satır" çipi çıkar
  (tıkla/End → sona git, tekrar açılır). Toggle ile de açılıp kapanır.
- **Seçim:** tıkla, Shift+tıkla, sürükle, ↑↓ PgUp/PgDn Home/End, Ctrl+A; **Ctrl+C** seçili satırları kopyalar.
- **Temizle:** ekrandaki satırları gizler (o ana kadarki sürüm numarası AppState'te tutulur; sayfaya dönünce de
  gizli kalır). Dosyalar silinmez.
- **Dışa aktar:** filtrelenmiş satırlar, dosya log biçiminde, UTF-8 `.log`.
- **Ctrl+F:** Loglar sayfasındayken arama kutusuna odak. Esc aramayı temizler.

## 4. Yeni çerçeve parçaları (P04+ de kullanacak)
Fare tekerleği ve `WM_CHAR` (Window → Host → `Widget::onWheel/onChar`), pano (`ui/platform/Clipboard`),
scrim'siz popup katmanı (`Host::pushModal(..., scrim=false)`), `SearchBox`, `Dropdown` + `MenuPopup`, `Toggle`,
`ScrollBar` (overlay, 4↔8px), `LogConsole`. Render seçenekleri: `--demo-logs`, `--click-at=x,y`.

## 5. Ertelenen
Sağ tık menüsü (kopyala/dışa aktar/temizle — araç çubuğu ve Ctrl+C karşılıyor), canlı imleç bloğu, dism.log'un
kendisini sayfada gösterme (sarmalayıcı kendi satırlarını yazıyor), yatay kaydırma (uzun mesajlar kesilir).

## 6. Kabul (kullanıcı testi)
- [ ] Loglar sayfası açılır; uygulama başlangıcı ve önceki işlemler (kaynak açma, mount…) listede.
- [ ] Bir sürüm bağla/çöz → satırlar canlı eklenir, otomatik kaydır sona gider.
- [ ] Yukarı kaydır → toggle kapanır, yeni satır gelince "↓ n yeni satır"; çipe tıkla → sona döner.
- [ ] Seviye "Yalnız hatalar", Kaynak "dism" filtreleri; Ctrl+F → ara, eşleşmeler vurgulu.
- [ ] Satır seç + Ctrl+C → panoda; Dışa aktar → .log dosyası; Temizle → boşalır.
