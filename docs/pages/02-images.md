# P02 — İmajlar

> Durum: ✅ kullanıcı onayladı (2026-09-28). Tasarım: 02 (liste), 03 (mount ilerlemesi), s2, s3.

## 1. Amaç
Açılan kaynaktaki (ISO / WIM / ESD / SWM / kurulum klasörü) sürümleri göstermek ve imaj seviyesindeki
işlemleri yapmak: bağla (mount), çöz (commit / discard), dışa aktar, ESD → WIM, index sil.

## 2. Ekran
- **Başlık eylemleri:** ESD → WIM (yalnız ESD kaynak), Dışa aktar, Bağla/Çöz (birincil).
- **Özet satırı:** `install.wim · 6 index · 6,72 GB`.
- **InfoBar'lar:** hata (çözüm önerisi + gerekiyorsa "Onar"), mount klasörü durumu (aşağıda §4).
- **İşlem şeridi (03):** "Windows 11 Pro bağlanıyor", yol · % · kalan süre, İptal.
- **Tablo:** Index · Ad | Sürüm ID | Mimari | Build | Dil | Değiştirilme | Boyut | Durum. Tek seçim.
  "Sürüm ID" ve "Değiştirilme" dar pencerede (ad sütunu < 260 px) gizlenir.
- **Inspector (280):** sürüm, build, dal, mimari, dil (+n), kurulum tipi, Sysprep durumu, oluşturma,
  değiştirilme, boyut, içerik (dosya · klasör), WIMBoot; "WIM dosyası": sıkıştırma, dosya boyutu,
  index sayısı, bölünmüş, önyükleme index'i. Altta Bağla/Çöz + "Index'i sil…".
- **Durum çubuğu:** mount segmenti (nokta + yol + imaj boyutu), görev segmenti, "Uygula" CTA (mount'ta görünür).

## 3. Akışlar
- **Yönetici:** uygulama açılışta kendini yükseltir (D-022). UAC reddedilirse işlemlerde s4 dialogu.
- **ISO kaynak:** DISM ISO içinden bağlayamaz → önce çalışma klasörüne açılır (`<workRoot>\work\<ad>`,
  devam ettirilebilir), kaynak o klasöre geçer.
- **Bağla:** `core::mountSafely` — Explorer pencerelerini taşır, artıkları onarır, klasörü yeniden
  oluşturur, "klasör meşgul" kodlarında bir kez temizleyip yeniden dener; aynı imaj zaten bağlıysa onu kullanır.
- **İçerik okuma (D-027):** bağlama (veya açılışta geri yükleme) biter bitmez aynı şeritte ikinci bir ilerleme
  başlar: "Windows 11 Pro içeriği okunuyor — Özellikler (1/3)". Sırayla Özellikler → Bileşenler (AppX) → Servisler
  okunur; her liste okunduğu an sayfasına düşer. Satır bu sırada "Bağlı" görünür. Vazgeç: kalan listeler
  sayfalarına girilince okunur (eski davranış). Okuma sürerken pencere kapatılabilir (okuma iptal edilir).
- **Çöz:** kaydet / at. `core::unmountSafely` — hive'ları boşaltır, Explorer'ı taşır, yarım unmount'ta
  (0xC1420117) yeniden dener, gerekiyorsa klasörü onarır.
- **Açılışta geri yükleme:** mount klasörü incelenir; sağlam mount varsa kaynağı açılır, sürüm seçilir,
  "Bağlı" gösterilir. Olmazsa sayfada "Bağlı imaj bulundu: <sürüm> (index n)" + **Devam et**.
- **Dışa aktar / ESD → WIM:** wimgapi `WIMExportImage` (LZX), kaydet dialogu.
- **Index sil:** yalnız diskteki WIM ve > 1 index; onay dialogu.

## 4. Mount klasörü durumları
`docs/ENGINE.md` "Mount durumları" ve "Hata kataloğu". Sayfa, klasör Free değilse ve uygulama o mount'u
göstermiyorsa bir InfoBar gösterir: durum + çözüm + kullanan programlar + "Onar" (veya "Devam et").

## 5. Kararlar
D-017 (DISM ADK'sız), D-018 (kaynak yerinde okunur), D-022 (açılışta yönetici). Çalışma klasörü
varsayılanı `%LOCALAPPDATA%\WinLove` (eski `C:\WinLove\mount` açılışta hâlâ kontrol edilir).
Ertelenen: tasarımdaki arama kutusu ve mimari filtresi, çoklu seçim, birleştirme (merge).

## 6. Kabul (kullanıcı testi — 2026-09-28 geçti)
- [x] ISO aç → 6 sürüm; seçim + inspector.
- [x] Bağla → hazırlık + mount ilerlemesi → "Bağlı"; Çöz (kaydet/at).
- [x] Explorer mount içindeyken çözme / bağlama → kendiliğinden toparlanır.
- [x] Dışa aktar, index sil (lab WIM).
- [ ] Uygulama kapat/aç → bağlı imaj geri gelir (düzeltme 2026-09-28 akşam, tekrar test edilecek).
- [ ] Bağla → mount ilerlemesinden sonra "içeriği okunuyor" ilerlemesi; bitince Özellikler / Bileşenler /
      Servisler sayfaları beklemeden açılır (2026-09-30, D-027).
