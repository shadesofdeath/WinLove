# P02 — İmajlar

> Durum: ✅ kullanıcı onayladı (2026-09-28). Tasarım: 02 (liste), 03 (mount ilerlemesi), s2, s3.

## 1. Amaç
Açılan kaynaktaki (ISO / WIM / ESD / SWM / kurulum klasörü) sürümleri göstermek ve imaj seviyesindeki
işlemleri yapmak: bağla (mount), çöz (commit / discard), dışa aktar, ESD → WIM, sürüm sil, yeniden adlandır,
bütünlüğü doğrula; birden çok sürümü birlikte seçip dışa aktar / sil.

## 2. Ekran
- **Başlık eylemleri:** ESD → WIM (yalnız ESD kaynak), Doğrula, Dışa aktar, Bağla/Çöz (birincil).
- **Özet satırı:** `install.wim · 6 index · 6,72 GB`.
- **InfoBar'lar:** hata (çözüm önerisi + gerekiyorsa "Onar"), mount klasörü durumu (aşağıda §4).
- **İşlem şeridi (03):** "Windows 11 Pro bağlanıyor", yol · % · kalan süre, İptal.
- **Tablo:** Index · Ad | Sürüm ID | Mimari | Build | Dil | Değiştirilme | Boyut | Durum.
  "Sürüm ID" ve "Değiştirilme" dar pencerede (ad sütunu < 260 px) gizlenir.
  **Çoklu seçim (D-035):** tıklama tek satırı seçer; onay kutusu ya da `Ctrl`+tık ekler / çıkarır; `Shift`+tık ve
  `Shift`+↑↓ aralık; `Ctrl+A` hepsi. İşaretli satırlardan biri "birincil"dir (inspector onu gösterir; Bağla ve
  yeniden adlandırma ona uygulanır). Son işaretli satırın işareti kaldırılamaz.
  Satıra sağ tık (yalnız o an yapılabilenler) — tek sürüm: Bağla / Dışa aktar / Yeniden adlandır… / Sürümü sil… /
  Yalnız bu sürümü tut…; birden çok: Seçili N sürümü dışa aktar… / sil… / Yalnız seçili N sürümü tut….
  `Del` = sil, `F2` = yeniden adlandır.
- **Inspector (280):** sürüm, build, dal, mimari, dil (+n), kurulum tipi, Sysprep durumu, oluşturma,
  değiştirilme, boyut, içerik (dosya · klasör), WIMBoot; "WIM dosyası": sıkıştırma, dosya boyutu,
  index sayısı, bölünmüş, önyükleme index'i. Altta Bağla/Çöz + "Sürümü sil…" / "Seçili N sürümü sil…" (kapalıysa
  tooltip nedenini söyler). Başlığın sağında kalem: yeniden adlandır.
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
- **Dışa aktar / ESD → WIM:** wimgapi `WIMExportImage` (LZX), kaydet dialogu. Birden çok sürüm seçiliyse hepsi
  sırayla tek WIM'e yazılır.
- **Yeniden adlandır (D-035):** dialog (Ad, Açıklama) → `core::setImageText`: WIM XML'inde NAME + DISPLAYNAME ve
  DESCRIPTION + DISPLAYDESCRIPTION (Setup'ın sürüm listesi DISPLAYNAME'i gösterir). Anında biter, dosyada başka
  hiçbir şey değişmez. Bağlı imaj varken ve ESD / bölünmüş imajda kapalı; ISO kaynakta önce çalışma klasörüne kopya.
- **Doğrula (D-035):** `core::verifyWim` imajdaki her akışı okur, açar (kendi LZX çözücümüz; XPRESS ntdll) ve
  lookup table'daki SHA-1 ile karşılaştırır. Hiçbir şey yazılmaz; ISO'nun içindeki install.wim yerinde okunur;
  bağlı imaj varken de çalışır; iptal edilebilir. Sonuç sayfada kalıcı InfoBar: "install.wim sağlam — 94.409 akış
  okundu (14,05 GB)…" ya da "install.wim bozuk — N akış SHA-1 özetiyle uyuşmuyor" (yerleri Loglar'da). ESD'de kapalı.
- **Sürüm sil (D-033):** onay dialogu → `core::removeImages`: WIM kalan sürümlerle yeniden yazılır (dosya küçülür,
  ilerleme + iptal; bitene dek özgün dosya durur). "Yalnız bu sürümü tut…" diğerlerinin hepsini tek seferde siler.
  ISO kaynakta önce çalışma klasörüne kopyalanır (ISO değişmez). Kapalı olduğu durumlar: bağlı imaj varken, ESD /
  bölünmüş imajda, tek sürüm kaldığında. Kalanlar yeniden numaralanır; seçim ve yanıt dosyasındaki sürüm index'i
  izler. Not: aynı ISO yeniden açılıp bağlanırsa çalışma kopyasındaki install.wim ISO'dakiyle değiştirilir —
  silinen sürümlerle devam etmek için son kullanılanlardaki çalışma klasörü açılır.

## 4. Mount klasörü durumları
`docs/ENGINE.md` "Mount durumları" ve "Hata kataloğu". Sayfa, klasör Free değilse ve uygulama o mount'u
göstermiyorsa bir InfoBar gösterir: durum + çözüm + kullanan programlar + "Onar" (veya "Devam et").

## 5. Kararlar
D-017 (DISM ADK'sız), D-018 (kaynak yerinde okunur), D-022 (açılışta yönetici). Çalışma klasörü
varsayılanı `%LOCALAPPDATA%\WinLove` (eski `C:\WinLove\mount` açılışta hâlâ kontrol edilir).
D-033 (sürüm silme), D-035 (çoklu seçim, yeniden adlandırma, doğrulama).
Ertelenen: tasarımdaki arama kutusu ve mimari filtresi, birleştirme (merge).
**Hazırlanan, UI'ı yazılmayan:** sürüm yükseltme (Home → Pro, `dism /Set-Edition`). Motor + `wlcli edition` +
`tools\lab_edition.ps1` hazır; kural 6 gereği UI, betik yönetici olarak gerçek imajda geçince yazılacak.

## 6. Kabul (kullanıcı testi — 2026-09-28 geçti)
- [x] ISO aç → 6 sürüm; seçim + inspector.
- [x] Bağla → hazırlık + mount ilerlemesi → "Bağlı"; Çöz (kaydet/at).
- [x] Explorer mount içindeyken çözme / bağlama → kendiliğinden toparlanır.
- [x] Dışa aktar, index sil (lab WIM).
- [ ] Uygulama kapat/aç → bağlı imaj geri gelir (düzeltme 2026-09-28 akşam, tekrar test edilecek).
- [ ] Bağla → mount ilerlemesinden sonra "içeriği okunuyor" ilerlemesi; bitince Özellikler / Bileşenler /
      Servisler sayfaları beklemeden açılır (2026-09-30, D-027).
- [ ] Sürüm silme (2026-09-30, D-033): ISO aç → bir sürüme sağ tık → "Yalnız bu sürümü tut…" → onay → kopyalama +
      silme ilerlemesi → tabloda tek sürüm (index 1), özet satırında küçülmüş boyut. Ardından Bağla ve ISO Oluştur
      çalışır; VM'de Setup sürüm sormadan kurar.
- [ ] Tek sürüm silme: inspector'daki "Sürümü sil…" ve `Del`; silme sırasında Vazgeç → imaj değişmeden kalır.
- [ ] Çoklu seçim (2026-09-30, D-035): onay kutusu / `Ctrl`+tık / `Shift`+tık / `Ctrl+A` ile işaretleme; sağ tık →
      "Seçili N sürümü sil…" ve "Yalnız seçili N sürümü tut…"; Dışa aktar → seçilenler tek WIM'de.
- [ ] Yeniden adlandır: kalem ya da `F2` → ad + açıklama → tabloda yeni ad; ISO üretilince Setup'ın sürüm listesinde
      yeni ad görünür.
- [ ] Doğrula: ISO açıkken (kopyalamadan) ve çalışma klasöründe → ilerleme şeridi → yeşil "sağlam" çubuğu.
