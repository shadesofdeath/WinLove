# InfoBar · Toast · Dialog · Onay dialogu
## InfoBar
32px, içerik alanına 16px kenar boşluğu ile, üst ve alt 1px line.subtle, zemin status.*Subtle (opak). Sol 16 padding; ikon 16 (warning-triangle / error-octagon / success-circle / info-circle, status rengi); 8 gap; `bodyStrong` başlık + body açıklama (text.secondary), tek satır ellipsis; sağda altı çizili eylem (text.secondary) ve close ikonu. Kapatılabilir; yeniden görünme kuralı ekran mantığında.
## Toast
360×48, sağ alt, status bar üstünde 16px; bg.overlay, 1px line.strong, r3, elevation.toast; ikon 16 + bodyStrong + caption; close ikonu. 4000 ms, hover'da durur. Aynı anda 1 (yeni gelen eskiyi değiştirir); ilerleme toast'ı progress satırı içerir. Giriş/çıkış motion.md.
## Dialog
Scrim `scrim` (%60 siyah) tüm pencere; dialog 480 (küçük 360, geniş 640), bg.overlay, 1px line.strong, r3, elevation.dialog, padding 16. Başlık bodyStrong (600) 16px'ten büyük değil; isteğe bağlı 16 ikon. Gövde body text.secondary. Alt sağ butonlar: iptal (secondary) + birincil; danger için danger butonu. Esc = iptal, Enter = birincil (danger'da onay checkbox'ı işaretliyse). Odak dialog içinde döner; kapanışta tetikleyiciye döner.
## Onay dialogu (tehlikeli işlem)
error-octagon ikonu status.error; etkilenen öğeler listesi (24px satır, ikon status.error, boyut mono); "Riskleri anladım" checkbox'ı işaretlenmeden danger buton disabled. Buton etiketi eylemi söyler ("Kaldır ve uygula"), "Evet/Hayır" değil.
