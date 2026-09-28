# WinLove marka kuralları

## İşaret
Geometrik kalp: 16 grid, 10 köşeli poligon (`M8 14L2 8V5l2-2h2l2 2 2-2h2l2 2v3z`) + içte tek chevron (`M5 8l3 3 3-3`). Stroke 1.25, köşe birleşimi round. Renk: `accent.base` (koyu), `accent.base`-light (açık), tek renk varyantları beyaz / #1A1918.

"Love" motifi **yalnızca** logo, app icon ve splash'ta. Arayüz içinde kalp yok; hiçbir ikon, boş durum ya da başarı mesajı kalp kullanmaz. Çocuksu ton, kırmızı/pembe kalp, gradyanlı arayüz ögesi yok.

## Boşluk ve ölçek
- Mark minimum 16px; 12px altında kullanma (title bar'da 16).
- Koruma alanı: mark boyutunun %25'i her yönde.
- Wordmark: IBM Plex Sans Medium 14/16, letter-spacing +0.2; mark ile 8px aralık (logo-full.svg).

## App icon
Yuvarlatılmış kare (radius %18.75), dikey gradyan #DE9F6C → #C27F4B — **gradyan yalnızca burada**. İçte koyu (#1A1210) mark. 32px altında iç chevron kaldırılır. 10 boyut ayrı dosya (app-icon-{16…256}.svg); .ico üretirken her boyutu kendi dosyasından rasterize et.

## Splash (480×300)
bg.base zemin, 1px line.strong çerçeve, 48px mark, "WinLove" title, caption açıklama, 80×2 ilerleme çubuğu, mono durum satırı. Gölge, blur, resim yok. Süre: DISM hazır olduğunda kapanır; min 600 ms görünür kalır.

## Yanlış kullanım
- Mark'ı doldurma (filled) — yalnızca stroke.
- Mark'ı döndürme, eğme, gölge verme.
- Vurgu dışında bir renkle boyama (tek renk varyantları hariç).
- Metnin içine emoji/kalp ekleme.
