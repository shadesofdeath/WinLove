# WinLove kurulum ekranı (OOBE) — yeniden tasarım özeti

Amaç: WinLove'un Windows OOBE'si yerine gösterdiği kendi kurulum ekranları için **Fluent, havalı ve
güzel** yeni bir tasarım. Bugünkü ekranın yaptığı her şeyi eksiksiz desteklemeli. Onaylanırsa
`resources/scripts/oobe.ps1` (WPF / XAML) bu tasarıma göre güncellenir.

Ekranın tüm metinleri: [oobe-brief-texts.md](oobe-brief-texts.md) (129 metin, Türkçe + Almanca).

---

## 1. Nerede, kim için çalışır

- Windows Kurulumu sırasında, **specialize** aşamasında SYSTEM olarak, henüz hiçbir hesap yokken
  açılır. Windows logosundan hemen sonra gelir; bitince Kurulum devam eder.
- Ya da bir kurulum hesabının ilk oturumunda (eski yol). Ekran aynıdır.
- Tam ekran, kenarlıksız bir pencere. 1024×768'den 4K'ya kadar her çözünürlükte ve her DPI
  ölçeğinde (%100–%250) düzgün görünmeli.
- Kurulumu yapan kişinin dili: **15 dil** — de, en, es, fr, it, ja, ko, nl, pl, pt-BR, ru, tr, uk,
  zh-CN, zh-TW. Sağdan sola dil yok. Almanca metinler en uzun (taşma payı bırak), CJK dilleri yazı
  tipi geri düşüşüne ihtiyaç duyar.
- Windows 11 ve Windows 10'da çalışır. Windows 10'da:
  - görev çubuğu hizalaması seçeneği yok;
  - "Pencere öğeleri" yerine "Haberler ve ilgi alanları" yazar;
  - Segoe UI Variable ve Segoe Fluent Icons yoktur (Segoe UI ve Segoe MDL2 Assets'e düşer).

## 2. Akış (sayfalar)

Sayfalar sırayla gelir. Üstte "adım i / n" göstergesi var. Koşullu sayfalar gizlenince n küçülür.

| # | Sayfa | Ne zaman | İçerik |
|---|---|---|---|
| 1 | **Ağa bağlan** | PC'de kablosuz bağdaştırıcı varsa ve plan açtıysa | Yakındaki kablosuz ağlar listesi (sinyal gücü, güvenli / açık / kurumsal), Yenile, seçilen ağda anahtar alanı + "Otomatik bağlan" + Bağlan; durumlar: aranıyor, bağlanıyor, bağlandı, bağlanamadı, ağ yok, kablolu bağlı; "Bağlanmadan devam et" |
| 2 | **Hoş geldiniz (hesap)** | Her zaman | Kullanıcı adı, parola, parola tekrar, "Parolayı göster"; plan izin veriyorsa parola boş olabilir ("Parola yok" durumu); hesap yerel yönetici; hatalar satır içinde |
| 3 | **Bu bilgisayar** | Plan açtıysa | Bilgisayar adı (en çok 15 karakter: harf, rakam, tire; öneri hazır gelir), saat dilimi açılır listesi (önceden seçili) |
| 4 | **Görünüm** | Plan açtıysa | Tema: Koyu / Açık (her biri küçük bir Windows resmiyle); vurgu rengi: 9 renk (Mavi #0078D4, Lacivert #0063B1, Deniz mavisi #038387, Yeşil #107C10, Mor #8764B8, Pembe #E3008C, Kırmızı #E81123, Turuncu #CA5010, Bakır #D4905A); görev çubuğu: Ortada / Solda (yalnız Win11); şeffaflık efektleri açık/kapalı |
| 5 | **Masaüstü arka planı** | Plan açtıysa | Windows'un kendi duvar kâğıtlarından küçük resim ızgarası (`Web\Wallpaper`, `Web\4K\Wallpaper`, `Web\Screen`) + "Windows'un varsayılanı" |
| 6 | **Uygulama paketleri** | Plan açtıysa | 7 paket, çoklu seçim (Temel, Tarayıcılar, Medya, İletişim, Geliştirici, Oyun, Araçlar), her birinin program listesi; not: ilk oturumda winget ile sessizce kurulur, ağ yoksa atlanır |
| 7 | **Tercihler** | Plan açtıysa | 7 aç/kapa anahtarı ve her birinin bir satır açıklaması: dosya uzantıları, gizli dosyalar, Gezgin "Bu bilgisayar"da açılsın, klasik sağ tık menüsü, görev çubuğunda arama kutusu, Görev Görünümü düğmesi, Pencere öğeleri (Win10: Haberler ve ilgi alanları); özet "{n} alışkanlık açık" |
| 8 | **Gizlilik** | Plan açtıysa | İki seçenek (radyo kart): "Az veri" (önceden seçili olabilir; altında kapanan şeylerin çipleri: zorunlu tanılama, reklam kimliği kapalı, etkinlik geçmişi kapalı, yazma kişiselleştirme kapalı) / "Windows'un varsayılanı" |
| — | **Hazırlanıyor** | Sayfalar bitince | Tam ekran; adım listesi (hesap oluşturuluyor, ad ve saat dilimi, gizlilik ayarları, görünüm ve tercihler, kurulum hesabı kaldırılıyor, yeniden başlatılıyor / Windows kurulumu bitiriyor), her adımın durumu (bekliyor / sürüyor / bitti), ilerleme çubuğu, ilerleme halkası, "Bilgisayarınızı kapatmayın"; hata durumu: "Bir sorun oluştu: …" + "Windows kendi hesap sayfasıyla devam edecek" |
| — | **Hazır** | Sonda | "Her şey hazır" / "Windows kurulumu bitirip bilgisayarı yeniden başlatır"; sonra Kurulum'un siyah ekranına geçiş |

Geri / İleri düğmeleri; son sayfada "İleri" yerine "Bitir". Ağ sayfasında "Atla".

## 3. Bugünkü tasarımın bilinen öğeleri (korunacak ya da daha iyisi yapılacak)

- Arkada Windows 11 kurulumundaki gibi yumuşak renk bulutları ("bloom"), seçilen vurgu rengine göre.
- Ortada yuvarlak köşeli bir kart: **solda seçimin canlı resmi**, sağda soru ve kontroller.
  - Ağ: ağ simgesi + rozet.
  - Hesap: kullanıcı adı çipi, rol ("Bu bilgisayarın yöneticisi"), kilit / açık.
  - Bilgisayar: PC çizimi, saat, tarih, bilgisayar adı, saat dilimi.
  - Görünüm: küçük bir masaüstü (duvar kâğıdı, pencere, satırlar, görev çubuğu, simgeler), tema ve
    vurguyla canlı değişir.
  - Duvar kâğıdı: küçük monitörde seçilen resim.
  - Paketler: seçilen paket sayısı ve listesi.
  - Tercihler: örnek Gezgin penceresi (Belgeler / Bu bilgisayar, Rapor.docx, Bütçe, Tatil), görev
    çubuğu; anahtarlar değiştikçe değişir.
  - Gizlilik: yörüngeler / küreler.
- Fluent kontroller: düğme (hover'da yumuşak geçiş), aç/kapa anahtarı, açılır liste, ince kaydırma
  çubuğu, radyo kart, çipler.
- Tema / vurgu değişince bütün pencere **çapraz geçişle** yeni renklere döner.
- Hareket: üstel yavaşlayan geçişler, işaretlerin "pop" girişi, Windows 11'in belirsiz ilerleme halkası.

## 4. Davranışlar (tasarım bunları göstermeli)

- Enter = İleri; ağ anahtarı alanında Enter = o ağa bağlan. Tab sırası mantıklı. Odak halkası görünür.
- Doğrulama hataları alanın altında, kırmızıyla:
  - ad boş;
  - adda yasak karakter (`" / \ [ ] : ; | = , + * ? < > @ %`);
  - ad alınmış;
  - parolalar farklı;
  - parola boş (izin yoksa);
  - bilgisayar adı kuralı.
- Hesap sayfası sayfaların her zaman ilki değildir (ağ sayfası önce gelebilir).
- Bütün renkler iki temada (açık / koyu) ve 9 vurgu renginin her biriyle okunaklı olmalı.
  Vurgunun üstündeki yazı, vurgu açıksa koyu olmalı (Bakır, Turuncu).
- İlk oturum yolunda pencere bir süre en üstte kalır (Başlat menüsü üstüne çıkmasın diye).
- Önizleme kipi: WinLove uygulamasındaki "Gerçek pencerede önizle" aynı pencereyi bu bilgisayarda
  hiçbir şey yapmadan açar. Uygulamadaki küçük önizleme kartı (`WelcomeCard`) da yeni tasarıma
  uymalı.

## 5. Teknik sınırlar (uygulanabilirlik)

- **WPF** (.NET Framework 4.x, Windows PowerShell 5.1). XAML, `XamlReader.Parse` ile yüklenir; kod
  PowerShell'de. WinUI, Mica ya da gerçek Acrylic yok: specialize aşamasında DWM arka planları
  güvenilir değil. Mica / Acrylic hissi gradyan, hafif gürültü ve yarı saydam katmanlarla taklit
  edilmeli. `BlurEffect` var ama pahalı; büyük alanlarda az kullanılmalı.
- Resimler: yalnız imajda olanlar (Windows'un duvar kâğıtları) ya da vektör (Path geometrileri).
  İnternetten bir şey yüklenmez; dış yazı tipi ya da resim paketi eklenmez.
- Yazı tipi: Segoe UI Variable (Win11) → Segoe UI (Win10); CJK için sistem geri düşüşü.
  Simgeler: Segoe Fluent Icons → Segoe MDL2 Assets.
- Animasyonlar WPF Storyboard / DoubleAnimation ile. Kare başı PowerShell kodu yok (yavaş);
  sürekli dönen öğeler XAML'da ya da küçük bir C# parçasıyla.
- Betik ASCII kalmalı; metinler `oobe.json`'dan gelir (tasarımda sabit metin olmamalı).

## 6. İstenen

- Windows 11'in kendi kurulumundan daha **havalı ama aynı ailede** bir görünüm: Fluent, derinlikli,
  sakin.
- Her sayfanın açık ve koyu temada tasarımı ve en az iki vurgu rengiyle görünümü.
- Hazırlanıyor ve Hazır ekranları; hata durumu; hata satırları; boş ve dolu durumlar (ağ yok,
  ağ listesi, bağlanıyor…).
- Sayfa geçişlerinin ve tema geçişinin nasıl hareket edeceği.
- 1024×768 ve 2560×1440 yerleşimleri.
