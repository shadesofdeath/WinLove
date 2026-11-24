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
- **İşlem şeridi (03):** "Windows 11 Pro bağlanıyor", yol · % · kalan süre, İptal. Dönen gösterge 2026-11-11'den beri
  WinLove'un ilerleme halkası (D-142, `size.ringLarge` 20, eski ikonun merkezinde).
- **Başka bir araçla bağlanmış imaj (D-148):** bilgi çubuğu "Başka bir araçla bağlanmış imaj … Kaydet ve çöz yalnızca bu
  bağlamaya işler"; çözme sorusunda ek paragraf; o aracın WIM'i kayıttan sonra yeniden yazılmaz (`optimizeWim` yok),
  diğer sürümlere uygulama (D-055) sunulmaz.
- **Tablo:** Index · Ad | Sürüm ID | Mimari | Build | Dil | Değiştirilme | Boyut | Durum.
  "Sürüm ID" ve "Değiştirilme" dar pencerede (ad sütunu < 260 px) gizlenir.
  **Durum sütunu (D-148, 116 px):** iş sürerken işin bütün satırlarında kendi kelimesi (`operationRowWord`): Bağlanıyor ·
  Siliniyor · Dışa aktarılıyor · Adlandırılıyor · Çözülüyor · Kopyalanıyor · Taşınıyor · Hazırlanıyor · Okunuyor ·
  Denetleniyor · Doğrulanıyor (silme için kopya hazırlanırken de "Hazırlanıyor", "Bağlanıyor" değil). İş bitince satır
  eski hâline döner (Bağlı / Bağlanamadı / —); "Bağlanamadı" yalnız bağlama ya da bağlama için kopya başarısızsa.
  **Çoklu seçim (D-035):** tıklama tek satırı seçer; onay kutusu ya da `Ctrl`+tık ekler / çıkarır; `Shift`+tık ve
  `Shift`+↑↓ aralık; `Ctrl+A` hepsi. İşaretli satırlardan biri "birincil"dir (inspector onu gösterir; Bağla ve
  yeniden adlandırma ona uygulanır). Son işaretli satırın işareti kaldırılamaz.
  Satıra sağ tık (yalnız o an yapılabilenler) — tek sürüm: Bağla / Dışa aktar / Yeniden adlandır… / Sürümü sil… /
  Yalnız bu sürümü tut…; birden çok: Seçili N sürümü dışa aktar… / sil… / Yalnız seçili N sürümü tut….
  `Del` = sil, `F2` = yeniden adlandır.
  **Kısayollar (2026-09-30):** sağ tıkta ayrıca *Dosya konumunu aç* (install.wim / ISO Dosya Gezgini'nde seçili) ve
  *Bilgileri kopyala* (işaretli sürümlerin adı, sürüm ID, build, mimari, dil, boyut, dosya yolu → pano); **bağlı**
  sürümde *Bağlama klasörünü aç* ve *Komut istemini bağlama klasöründe aç* (yönetici, `dism /Image:.` hazır).
  Klavye: `Ctrl+E` bağlama klasörü (**her sayfadan**), `Ctrl+Shift+E` dosya konumu, `Ctrl+C` bilgileri kopyala
  (tablo odaktayken), `F5` kaynağı yeniden oku (bağlı imaj yokken; işaretler korunur).
- **Inspector (280):** sürüm, build, dal, mimari, dil (+n), kurulum tipi, Sysprep durumu, oluşturma,
  değiştirilme, boyut, içerik (dosya · klasör), WIMBoot; "WIM dosyası": sıkıştırma, dosya boyutu,
  index sayısı, bölünmüş, önyükleme index'i. Altta Bağla/Çöz + "Sürümü sil…" / "Seçili N sürümü sil…" (kapalıysa
  tooltip nedenini söyler). Başlığın sağında kalem: yeniden adlandır; bağlı sürümde yanında klasör: bağlama
  klasörünü aç.
- **Durum çubuğu:** mount segmenti (nokta + yol + imaj boyutu), görev segmenti, "Uygula" CTA (mount'ta görünür).

## 2b. Özet sekmesi (D-138, 2026-11-11)
Görüntü kartının sekmeleri imaj açıkken **Kaynak seç · Sürümler · Özet**. İmaj açılınca yine Sürümler gelir (alışılan
tablo yerinde); Özet, Görüntü'nün son açılan alt sayfası olarak hatırlanır; durum çubuğunun imaj bölümü (kaynak
açıkken) ve komut paletindeki "Özet" onu açar; `--page=overview`. Şerit, Sürümler'inkiyle aynı (`Shell::addImageActions`);
yan kart yok, sayfa tam genişlik, sığmazsa kayar (`ScrollableBody`). Model `pages/overview/OverviewModel`
(`buildOverview`: AppState + denetleyicilerin bildiği `OverviewInputs`, saf fonksiyon), sayfa `pages/OverviewPage`;
metinler durum bildirimlerinde kurulur, `paint()` yalnız çizer. **Yeni motor çağrısı, dosya erişimi ya da liste
okuması yok:** okunmamış liste "henüz okunmadı", okunan "okunuyor…", hatalı "okunamadı", bağlı değilken "bağlanınca
okunur" der; dönen gösterge ya da uydurma sayı yok.
- **Kimlik kartı:** bağlı sürüm (yoksa seçili): ad + rozet (Bağlı / Bağlı değil / Bağlanıyor · %38 / İçerik okunuyor
  / Uygulanıyor; salt okunursa eki), "11 25H2 · 26200.9445 · Professional · x64 · tr-TR +1 · 24,06 GB",
  "install.wim · LZX (WIM) · 6,16 GB · sürüm 1 / 1 · x64-9448.iso", sağda bağlama klasörü. Altında **Sıradaki adım**
  cümlesi ve sayfanın tek dolu düğmesi: Kaynak seç · Bağla (mount) · ESD/solid → WIM · Bileşenler (boş kuyruk) ·
  Uygula'ya geç (kuyruk) · İlerlemeyi gör (çalışıyor / duraklatıldı) · Sonuca bak (kaydedilmeden kalmış çalışma) ·
  ISO Oluştur (kaydedilmiş çalışma, imaj çözülmüş); bir iş sürerken (bağlama, dışa aktarma…) düğme yok.
- **Uygulama kartı** (çalışıyorken ya da son çalışma hâlâ haberken: imaj çözülmüş, ya da kaydedilmeden kalmış ve
  kuyruk değişmemiş): durum + aşama, ölçer ya da önce → sonra çubuğu, "● tamam ● atlandı ● bekliyor", geçen / kalan
  ya da "… sürdü · önce → sonra", "Uygula ekranı" bağlantısı. Çalışırken halka (D-142). Otomatik ISO (D-144):
  yapılırken başlık "ISO oluşturuluyor · %42", çubuk ISO'nun ilerlemesi, sıradaki adım "İlerlemeyi gör"; sonra
  "Tamamlandı · ISO hazır", alt satırda dosya / neden; sıradaki adım `Next::IsoReady` ("ISO hazır: … Önce bir sanal
  makinede dene, sonra USB'ye yaz." · **Klasörde göster**). Durdurulan çalışmanın kaydet / çöz'ü (D-148) kartta da
  görünür ("İmaj kaydediliyor… / Değişiklikler atılıyor…" → sonuç); kapanış sürerken sıradaki adım "Bekle".
- **İmajda ne var:** 4 sütun (dar: 2), her kutucuk kendi sayfasını açar (Enter / Boşluk da): Bileşenler (iki
  sütun; ağaçtaki öğe sayısı + en kalabalık üç kategori), Sağlanan uygulamalar (→ Bileşenler), Windows özellikleri
  (açık / toplam · kurulu yetenek), Üçüncü parti sürücüler (Sürücüler sayfası okur), Diller (WIM'in XML'inden,
  okununca imajın listesi), Servisler (devre dışı sayısı), Güncellemeler (kuyruktakiler). Sağ üstte o sayfanın
  kuyruk sayısı (nav rozeti biçimi). Kesilen ad / satır ipucunda tam.
- **Kuyruk** (D-148, 2026-11-11): çipler **işlem sayısıdır** (`pages/overview/QueueChips`): her işlem onu kuyruğa koyan
  sayfaya (`queuePageOf`: türe göre; paylaşılan türlerde sayfaların sahiplendiği yuvalar — Kayıt Defteri'nin girdileri,
  karşılamanın dosyaları, küçültmenin güncelleme ilkesi; sahipsiz kayıt / dosya Ayarlar'ın) ve o sayfanın sekmesine
  sayılır. Gruplar gezintininkiler + Görüntü (Başlat; ör. sürüm yükseltme) + "Kurulum › Katılımsız" (karşılamanın iki
  `WriteFile`'ı) + hiçbir sekmede olmayan "Diğer"; çiplerin toplamı başlıktaki "N değişiklik". Tıklayınca sayfası. Sol
  tarafta çip yoksa sağ sütun kartın tamamını alır. Sağda boyut önce → ≈ sonra + çubuk + "1,05 GB küçülür · %4" (boyut
  değişmiyorsa çubuk yalnız nötr iz), "≈ 7 dk sürer", yüksek riskli adımlar (ilk üçü adıyla; yoksa başarı renginde ✓ +
  ikincil metin, varsa `RiskHigh` hata renginde), uyumluluk: etkilenen özellikler (D-124 raporu) ya da "N koruma açık ·
  bilinen bir bozulma yok". Bağlı değilken / Uygula çalışırken / boşken tek cümle; bağlanınca kuyruğa girecek indirmeler
  (D-124) ayrıca söylenir. Gezinti ve kutucuk rozetleri eskisi gibi sayfanın kendi biriminde (dil, ayar…).
- Başka bir araçla bağlanmış imajda kimlik kartı "Başka bir araçla bağlı · <klasör>" der (D-148).
- Render: `--page=overview <kaynak>` (bağlı değil), `--demo-overview[=unread|running|paused|done|held|iso-running|iso-done|
  iso-failed|iso-cancelled|iso-skipped] [<kaynak>]`, `--demo-unattended=welcome --demo-overview=unread --page=overview`,
  `--demo-mounts=overview`.

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
- **Sürümü yükselt (D-035):** yalnız **bağlı** sürümde (inspector'ın ikinci düğmesi ve sağ tık). Önce imajın
  çevrilebileceği sürümler okunur (`core::readEditions`: iki `dism.exe` çağrısı, ~8 sn; mount başına bir kez), sonra
  dialog: hedef sürüm (Pro önseçili) → **Kuyruğa ekle**. İmaja o an dokunulmaz: `SetEdition` kuyruk işlemidir,
  Uygula'da **ilk adım** olarak çalışır (`dism /Set-Edition`), commit'ten sonra WIM'deki ad / açıklama / FLAGS yeni
  sürüme göre yazılır (kullanıcının kendi verdiği ad korunur). Tek yönlüdür. Kuyruktayken inspector "Kuyrukta:
  Windows 11 Pro sürümüne yükseltme" der; dialog yeniden açılınca "Kuyruktan çıkar" sunar.
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
D-141 (2026-11-11, VHD / VHDX kaynağı): "Bağla" önce sorar ve kullanıcının dosyasına değil WinLove'un fark diskine yazar;
Kaydet = birleştir, Kaydetmeden çöz = fark diskini sil (dosya değişmez). Sürüm listesinin WIM işleri (sil, yeniden
adlandır, çoğalt, sırala, ekle, dışa aktar), doğrulama ve Araçlar pasif, ipucu nedenini söyler; yan kartta WIM satırları
yerine "Sanal disk" bölümü; Özet'te disk satırı. Render `--demo-vhd[=…]`; pencerede denenmedi.
D-148 (2026-11-11): başka araçla bağlanmış imajı devralma (P01), Durum sütununun işlem kelimeleri, Özet › Kuyruk'un işlem
sayısıyla çipleri, durdurulan Uygula'dan sonraki kaydet / çöz'ün kartlarda görünmesi. D-142 halka, D-144 Özet'te otomatik
ISO. Render `--operation=delete|export|rename|duplicate|move|unmount|prepare-delete [--select=…]`, `--demo-mounts=…`.
Ertelenen: tasarımdaki arama kutusu ve mimari filtresi, birleştirme (merge).

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
- [ ] Sürümü yükselt: Home'u bağla → "Sürümü yükselt…" → Windows 11 Pro → Uygula → tabloda ad "Windows 11 Pro",
      Sürüm ID "Professional"; ISO + VM: Pro kurulur.
- [ ] Kısayollar: bağlıyken `Ctrl+E` / klasör düğmesi → Dosya Gezgini bağlama klasöründe; sağ tık → komut istemi o
      klasörde açılır; Dosya konumunu aç; Bilgileri kopyala → yapıştırınca sürüm bilgileri; `F5`.
      (Yazıldı, **hiç denenmedi** — kullanıcı "test etme, ben ederim" dedi: render / birim testi yok.)
- [ ] Doğrula: ISO açıkken (kopyalamadan) ve çalışma klasöründe → ilerleme şeridi → yeşil "sağlam" çubuğu.
- [ ] D-148: ISO kaynakta bir sürümü sil → kopya hazırlanırken satır "Hazırlanıyor", silerken "Siliniyor" (iki sürüm
      seçiliyse ikisi de), bitince eski hâli. Birim testi + render; pencerede izlenmedi.
- [ ] D-148: Katılımsız'da karşılama açıkken bağla → Özet › Kuyruk "Kurulum · Katılımsız 2", çiplerin toplamı başlıktaki
      sayıya eşit, boyut değişmiyorsa çubuk nötr. Birim testi + render.
