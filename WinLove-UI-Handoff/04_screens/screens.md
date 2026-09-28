# Ekranlar (1440×900, koyu + açık)

Ortak iskelet: TitleBar 32 · Nav 200 (44 daraltılmış) · İçerik · Inspector 280 (isteğe bağlı) · StatusBar 24. İçerik: başlık 16px semibold + 11px açıklama, sağda ikincil butonlar; araç çubuğu 24px (arama 240, dropdown, chip, sağda özet metin); sütun başlığı 24px; satırlar 24px.

| # | Dosya | Amaç | Bölgeler / etkileşim |
|---|---|---|---|
| 01 | 01-welcome-source | Kaynak seç | DropZone (ISO/WIM/ESD/SWM/VHD), canlı sistem kutusu (yönetici uyarısı), son kullanılanlar tablosu (çift tık açar). Status: bağlı imaj yok, CTA yok. |
| 02 | 02-image-list | Sürümler | Index tablosu (checkbox, ad, mimari, build, dil, boyut, durum), inspector'da seçili index detayları; birincil aksiyon "Bağla". |
| 03 | 03-mounting | Bağlama sürüyor | Tam ekran değil: içerik üstünde 88px ilerleme şeridi (spinner, yol, %, kalan süre, İptal). Status bar aynı ilerlemeyi 80px bar ile tekrarlar. |
| 04 | 04-components | Bileşen ağacı | Tri-state TreeView (ad / risk / boyut), InfoBar (bağımlılık uyarısı), inspector (kategori, boyut, risk, durum, bağımlılıklar, uyumluluk, içerik). |
| 04b | 04b-components-search | Arama vurgusu | Odaklı SearchBox, eşleşme mark'ı, yalnızca eşleşen dallar. |
| 05 | 05-features | Özellikler | Tablo + toggle sütunu; kuyruktaki değişiklik "Etkinleştirilecek" durumu. |
| 06 | 06-updates | Güncellemeler | 56px DropZone, sıralı tablo (drag-handle), uyumsuz paket hatası satırda. |
| 07 | 07-drivers | Sürücüler | Sınıfa göre gruplanmış ağaç (checkbox), sağlayıcı/sürüm sütunu. |
| 08 | 08-registry | Kayıt defteri | 2 sütun kategori listesi (kart değil: üst 1px çizgi + ikon + sayaç), altta seçili kategorinin tweak tablosu (anahtar mono). |
| 09 | 09-services | Servisler | Tablo; "Yeni başlangıç" sütunu 20px dropdown; risk sütunu. |
| 10 | 10-tweaks | Ayarlar/Tweaks | Sekmeler + form (240px etiket sütunu, toggle/dropdown/radio). |
| 11 | 11-unattended | Katılımsız | Adım göstergesi (nokta + etiket), 55% form / 45% canlı XML önizleme (mono, değişen satırlar accent). |
| 12 | 12-post-setup | Kurulum sonrası | Sıralı adım tablosu (winget/komut/kopyala), bekle etiketi. |
| 13 | 13-apply-summary | Uygula özeti | 7 sayaç (üst çizgi + 16px sayı), 2 InfoBar (uyarı, bilgi), işlem sırası tablosu; "Uygula · 35" birincil. |
| 13b | 13b-apply-confirm | Onay dialogu | Scrim + 480px dialog: tehlikeli öğeler listesi, onay checkbox'ı, danger buton. |
| 14 | 14-apply-running | Çalışıyor | 2px genel ilerleme, 420px adım listesi (32px satır, aktif: accent.subtle), canlı log (mono 11/16, imleç bloğu), Durdur. CTA "Durdur"a döner. |
| 15 | 15-done | Tamamlandı | Success InfoBar, 6 sayaç, sonuç tablosu; CTA "ISO Oluştur". |
| 16 | 16-iso-build | ISO/USB | Sekme (ISO / USB), form, 320px özet kutusu (bg.panel). |
| 17 | 17-presets | Presetler | 360px liste + karşılaştırma: A/B dropdown, diff satırları (+ yeşil, − kırmızı, ~ amber sol çubuk). |
| 18 | 18-logs | Loglar | Araç çubuğu (Ctrl F arama, seviye, kaynak, otomatik kaydır) + tam yüzey log; ERR satırı status.errorSubtle. |
| 19 | 19-app-settings | Uygulama ayarları | Tema radio, vurgu rengi swatch'ları (seçili: 1px text.primary halka), yoğunluk, dil, klasörler. |
| 20 | 20-about | Hakkında | 24px mark, sürüm, mono bilgiler, lisans; CTA yok. |
| 21 | 21-command-palette | Komut paleti | 04 üstünde scrim + 560px palet: arama satırı, Sonuçlar (32px, ikon + ad + alt satır), Komutlar (24px + kbd). |
| s1 | s1-empty-no-source | Boş durum | 24px ikon + body-strong + caption + secondary buton; nav rozetleri gizli. |
| s2 | s2-loading-skeleton | Yükleme | Satır iskeletleri bg.raised 10px yüksek, 2px radius; inspector başlığı da iskelet. |
| s3 | s3-error | Hata | Error InfoBar + toast (sağ alt, 360×48) + satırda "Bağlanamadı". |
| s4 | s4-admin-required | Yönetici gerekli | 01 üstünde 440px dialog (shield-warning), primary "Yönetici olarak yeniden başlat". |

Notlar: hover/pressed/focus varyantları galeride; ekran SVG'lerinde yalnızca rest + seçili. Sütun genişlikleri screens SVG'lerinde piksel olarak okunabilir (viewBox 1440×900, 1 birim = 1 px @100%).
