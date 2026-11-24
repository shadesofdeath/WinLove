# P01 — Kaynak

**Durum:** ✅ Tamamlandı (2026-09-28). Canlı sistem kartı kullanıcı isteğiyle kaldırıldı (D-021). 2026-11-10: düzen
kullanıcı isteğiyle yeniden yapıldı (D-133: tek satırlık bırakma alanı, tek sütunlu son kullanılanlar); §2, §5, §7 ve
§11 buna göre — kullanıcı henüz denemedi. · **Tasarım:** `04_screens/01-welcome-source-*.svg`, `s1-empty-no-source`, `s4-admin-required` · **Prototip:** `06_prototype/index.html` (source)

## 1. Amaç
Kullanıcı düzenleyeceği Windows imajını seçer: ISO, WIM/ESD/SWM ya da çıkarılmış ISO klasörü. Seçim anında (yönetici gerekmeden) sürümler okunur, kaynak son kullanılanlara eklenir ve İmajlar sayfasına (P02) geçilir. (Canlı sistem düzenleme kapsam dışı — D-021.)

## 2. Görünüm bölgeleri (2026-11-10, `SourcePage.cpp`; yukarıdan aşağı, sayfa kartının gövdesinde)
Sayfanın eylemleri D-109'dan beri şeritte: Windows indir · Dosya aç… · Klasör… · Klasörden imaj oluştur…; açıklama
ipucu satırında. Gövde:

| Bölge | İçerik | Ölçü |
|---|---|---|
| Bırakma satırı (`source/DropRow`) | indirme ikonu (iconLarge) + "İmaj dosyasını buraya bırak" (Body) + biçimler ".iso · .wim · .esd · .swm · ya da çıkarılmış kurulum klasörü" (Caption, text.tertiary); çerçeve line.strong, r3; üzerine gelince çerçeve belirginleşir, bg.hover söner-yanar | tam genişlik, yükseklik control + 2 × s4; ikon ve metin aşağıdaki listenin ikon / ad sütunuyla hizalı (`RecentList::kInset`, `kTextX`) |
| Bilgi çubukları | açılamadı hatası; bu PC'nin DISM uyarısı (D-081) — yalnız görünürken yer kaplar | 32 yükseklik, aralarında s4 |
| Bu bilgisayarda bağlı imajlar | yalnız bağlı (ya da artığı kalmış) bir imaj varken: bölüm başlığı, tablo (Klasör · İmaj · WIM dosyası · Durum: renkli nokta + sözcük; devralma sürerken "Devralınıyor" / "Yeniden bağlanıyor", D-148), altında ipucu ("Çift tık / Enter: bu imajla çalış · Delete: değişiklikleri atarak ayır"). Yollar `\\?\` öneki atılmış düz hâliyle | en çok 5 satır görünür (tablo kendi içinde kayar); Durum 168; kalan genişliğin %32'si Klasör, %34'ü İmaj, gerisi WIM dosyası |
| Son kullanılanlar | bölüm başlığı + `source/RecentList` (§7) | satır 2 × 16 + 2 × s2 (iki metin satırı); liste kartın kalanını alır ve içinde kayar; en az 3 satır sığmıyorsa sayfa kayar |

Bölüm başlıkları diğer sayfaların formlarındaki gibi (`paintFormSection`: BodySemi, text.primary, `FormView::kSection`
yüksekliğinde blok); büyük harf yok.

## 3. Veri
| Veri | Kaynak | Ne zaman | Önbellek |
|---|---|---|---|
| Son kullanılanlar (≤10) | `%LOCALAPPDATA%\WinLove\recent.json` (`app/state/RecentSources`) | açılışta | bellekte; her açmada yazılır |
| Kaynak bilgisi | `core::openSource` (motor thread'i) | dosya seçilince | `AppState.source` |

## 4. Aksiyonlar
| Aksiyon | Tetikleyici | Etki | Admin? |
|---|---|---|---|
| Dosya aç | şeritte "Dosya aç…"; bırakma satırına tık / Enter / Boşluk; Ctrl+O yok (O = preset, interaction.md) | IFileOpenDialog (.iso .wim .esd .swm) → aç | Hayır |
| Klasör aç | şeritte "Klasör…" | IFileOpenDialog (klasör) → `sources\install.*` aranır | Hayır |
| Sürükle-bırak | pencerenin herhangi bir yeri | kaynak türü → aç; değilse bırakma satırı hata durumu | Hayır |
| Son kullanılanı aç | **tek tık** / Enter (2026-11-10; çift tık ayrıca bir şey yapmaz) | aç; dosya yoksa satır soluk, açarken hata | Hayır |
| Listeden kaldır | satırın sonundaki ×; Delete; → ile ×'e geçip Enter / Boşluk; sağ tık menüsünde "Listeden kaldır" | §7'deki kural (çalışma kopyasıysa diyalog) | Hayır |

Açma sonrası: `AppState.source` dolar, P02'ye geçilir, son kullanılanlar güncellenir. (Başlık 2026-11-11'den beri
yalnız "WinLove" der, D-147; dosya adı durum çubuğunda ve sayfalarda.)

## 5. Durumlar
- **Boş (2026-11-10):** "Son kullanılanlar" başlığı kalır; listenin ilk satırının yerinde tek sessiz satır: saat-kuyruk
  ikonu + "Henüz bir kaynak açmadın — açtığın imajlar burada listelenir." (Caption, text.tertiary). Boş liste odak almaz.
  (Önce bölüm gizleniyordu.)
- **Yükleniyor:** ISO ~70 ms; bekleme göstergesi yok. Açma sürerken bırakma satırının cümlesi "Açılıyor…" (text.secondary)
  olur, satır tıklanınca bir şey yapmaz.
- **Hata:** bırakma satırının altında error InfoBar ("Açılamadı: …", kapatılabilir; tamamı tooltip'te) + log.
- **Bu PC'nin DISM'i eksik / değiştirilmiş (D-081):** açılıştaki denetim sorun bulursa bırakma satırının altında warning InfoBar
  (kapatılabilir): ilk sorun ("CbsProvider.dll — eksik (+1 sorun daha)") ve ne yapıldığı (ADK'nın DISM'i kullanılıyor /
  ADK kurulu / ADK Deployment Tools önerisi). Sağlıklı PC'de görünmez.
- **Sürükleme** (bırakma satırı): geçerli → accent.subtle zemin, accent çerçeve ve ikon; geçersiz → status.error'un soluk
  zemini, status.error çerçeve, hata ikonu ve "Desteklenmeyen dosya" (biçimler yerinde kalır).
- **Admin gerekli:** bu sayfada yok (s4 dialogu P02'de bağlarken kullanılır).

## 6. Motor / wlcli
- [x] `openSource` ISO/WIM/ESD/SWM (P00/Faz 2)
- [x] `openSource` klasör (çıkarılmış ISO) → `wlcli info <klasör>`
- [x] `releaseLabel(build)` / `releaseSummary` → "11 25H2 · 26200.8037"
- [x] `openSource` VHD / VHDX (D-141, 2026-11-11) → `wlcli info <vhd|vhdx>`: disk kısa süre yalnız okumalık bağlanır,
      kovanlar offreg ile okunur. Sayfada: filtre / bırakma kuralı / ipucu .vhd · .vhdx, son kullanılanlarda disk ikonu.
      Bağlama, kaydetme ve çözme P02'de (fark diski, D-141). Lab'da kanıtlı (`tools\lab_vhd.ps1`); arayüz pencerede
      denenmedi.
- [x] Başka bir aracın bağladığı imajı devralma (D-148, 2026-11-11) → `wlcli adopt <klasör>`, `wlcli mounts --fast`. Tek
      motor işi: DISM kaydı → `core::adoptionStep` (Ok → kullan; NeedsRemount → `DismRemountImage`, sonra kullan; Invalid /
      imaj yok / kalıntı / boş → hiçbir şeye dokunmadan red) → kaynağı oku. Kaynak, imaj bir kurulum klasörünün install
      imajıysa (`sources\install.*` + `setup.exe` ya da `sources\boot.wim`) o klasör, değilse WIM'in kendisi. İlerlemesiz
      iki iş (devralma, yeniden bağlama) durum çubuğunda yüzdesiz; hata işi bitirir, gerekçe hata çubuğunda ve bildirimde,
      liste yeniden okunur. Liste artık her bağlama için kullanan programları taramıyor (`blockersOf`; 12 sn'lik bekleme
      buradandı). Yönetici lab'ında gerçek bir `\\?\` bağlamasıyla kanıtlı (`build\lab\fixes\lab-adopt.ps1`); NeedsRemount
      yalnız birim testi; pencerede denenmedi. Render `--demo-mounts=adopting|remounting|failed|adopted`.

## 7. Widget'lar
- [x] `DropZone` (rest / hover / drag-valid / drag-invalid / loading) — 2026-11-10'dan beri bu sayfada yerine
      `pages/source/DropRow` (aynı durumlar, tek satır; sürükleme durumları ortak `ui::DropZone::DragState`)
- [x] `InfoBar` (error, warning, success, info; kapatılabilir)
- [x] `Dialog` + Host modal katmanı (scrim, odak kapanı, Esc/Enter, scrim tıklaması)
- [x] Çok satırlı metin (Canvas `drawTextWrapped`, `TextStyles::measureWrapped`)
- [x] Sayfaya özel: `RecentList` — 2026-11-10'dan beri tek sütun (önce D-091'in kart ızgarası). Satır: solda kaynak
      ikonu (ISO / klasör / WIM, vurgu renginde), dosya adı (Body) ve altında Windows sürümü ("11 25H2 · 26200.9445",
      Caption); sağda "ISO · 6,90 GB · bugün 18:40" (Caption, text.tertiary); en sonda ×. Üzerine gelinen satır bg.hover
      (söner-yanar, `ui::ItemHover`); seçili satır yalnız liste odaktayken accent.subtle + 2 px vurgu çubuğu; bağlam menüsü
      açıkken satırı aydınlık kalır. Satırın tooltip'i dosyanın klasörü, ×'inki "Listeden kaldır (Delete)". Liste
      yerinden uzunsa kendi içinde kayar (tekerlek, kaydırma çubuğu; × çubuğun altında kalmaz). Bütün metinler
      `setEntries`'te kurulur, `layout()`'ta ölçülür; `paint()` yalnız çizer. Dosyanın var olup olmadığı `setEntries`'te
      bir kez denetlenir (arayüz iş parçacığında: AUDIT SUB-04, açık).
- [x] Klavye (2026-11-10): ↑ ↓ Page Up / Down Home End seçer; Enter açar; → seçili satırın ×'ine geçer (odak halkası
      düğmede), ← geri; × üzerindeyken Enter / Boşluk kaldırır; Delete seçili satırı kaldırır; Menü tuşu / Shift+F10
      bağlam menüsü.
- [x] Listeden kaldırma (2026-09-30, kullanıcı isteği; 2026-11-10'da × görünür hâle geldi): × dosyası bulunamayan
      kayıtta hep görünür (satır soluk, sağda "Dosya bulunamadı", status.warning); diğerlerinde üzerine gelinince
      (söner-yanar) ya da klavyeyle seçiliyken. `Del` ve sağ tık menüsü (Aç · Klasörde göster · SHA-256 doğrula… (yalnız
      dosyalarda, D-058) · Listeden kaldır; Menü tuşu / Shift+F10 da açar). Kullanıcının kendi dosyası ya da
      klasörü yalnız listeden çıkar. WinLove'un ISO'dan çıkardığı çalışma kopyasıysa (`AppSettings::isWorkCopy`:
      `<çalışma klasörü>\work\…` ya da eski `C:\WinLove\work\…`) dialog sorar: Vazgeç · Yalnız listeden kaldır ·
      Kopyayı da sil. O kaynaktan bir imaj bağlıyken ya da iş sürerken kaldırılmaz ("Önce bağlı imajı çöz").
- [x] Platform: `DropTarget` (OLE), `FileDialog` (IFileOpenDialog)

## 8. String anahtarları
Mevcut: `source.*`, `dialogs.admin*`, `common.cancel|name|type|size`. Eklenecek: `source.version`, `source.dropUnsupported`, `source.openFailed`, `source.liveLater`, `source.adminYes`, `source.fileMissing`, `source.filterImages`, `source.filterAll`, `source.pickFolder`. Düzeltme: `common.size` TR "Size" → "Boyut" (handoff çeviri hatası).

## 9. Kabul kriterleri
- [x] Test ISO'su açılır (komut satırı + birim testi: Shell→motor→UI akışı), 6 sürüm okunur, P02'ye geçilir, breadcrumb dosya adını gösterir — dosya diyaloğu ile: kullanıcı testi
- [ ] Aynı ISO sürükle-bırakla açılır; .txt bırakınca hata durumu, hiçbir şey açılmaz — kurallar test edildi, gerçek OLE sürüklemesi kullanıcı testinde
- [x] Çıkarılmış klasör (`build\lab\iso`) açılır (`wlcli info`); "Klasör…" diyaloğu kullanıcı testinde
- [x] Son kullanılanlar kalıcı, en yeni üstte, en fazla 10, büyük/küçük harf farkı olmadan tekil, bozuk dosya boş sayılır, silinmiş dosya soluk + tooltip
- [x] Koyu/açık/HC ve %100/%150/%200 render tasarımla uyumlu (bkz. §11)
- [x] Klavye: Tab ile başlık → menü → Dosya aç/Klasör → DropZone → liste; ↑↓ Enter liste içinde; dialog'da odak kapanı (Host testleri)
- [x] TR/EN eksiksiz (üretici denetimi; `common.size` TR düzeltildi)
- [ ] 2026-11-10 düzeni (D-133) kullanıcıyla denendi: tek tıkla açma; × ile, Delete ile ve → + Enter ile kaldırma;
      boş liste satırı; dar / kısa pencerede listenin kendi içinde kayması. Birim testi var
      (`tests/app/SourcePolishTests.cpp`), render ile görüldü; canlı pencerede denenmedi.
- [ ] Başka bir araçla bağlanmış imaj (D-148): listede çift tık / Enter → "Devralınıyor" (gerekirse "Yeniden
      bağlanıyor") → Sürümler'de "Başka bir araçla bağlanmış imaj" bilgi çubuğu, Özet'te "Başka bir araçla bağlı ·
      <klasör>"; geçersiz / boş bağlamada hata çubuğu, bağlamaya dokunulmamış. Motor lab'da kanıtlı, pencerede denenmedi.

## 10. Kullanıcı test senaryosu
1. WinLove'u normal (yönetici olmayan) başlat → Kaynak sayfası.
2. "Dosya aç…" → `Downloads\Win11_25H2_Turkish_x64_v2.iso` → İmajlar sayfasına geçmeli, başlıkta ISO adı.
3. Kaynak'a dön → listede ISO "bugün HH:MM", "ISO", "11 25H2 · 26200.8037", "6,72 GB" görünmeli.
4. Masaüstünden bir .txt dosyasını pencereye sürükle → kırmızı "Desteklenmeyen dosya"; bırak → hiçbir şey olmamalı.
5. ISO'yu Explorer'dan sürükle-bırak → açılmalı.
7. Uygulamayı kapatıp aç → son kullanılanlar duruyor olmalı.

## 11. Görsel doğrulama notları / bilinçli sapmalar
Karşılaştırma: `python tools/compare_design.py 01-welcome-source -- --recent-file=tests/integration/fixtures/recent-sample.json`
- Sütun başlıkları caption 400 (tasarım 500): 11px medium stili yok; ayırt edici değil, stil eklemeye değmez. (2026-11-10:
  son kullanılanlarda sütun başlığı yok; bu not bağlı imajlar tablosu için geçerli.)
- Admin dialogu içerik kadar uzun (tasarımda gövde ile butonlar arasında ~64px boşluk var; spec'teki 24'ü uyguladık).
- Satır seçimi accent.subtle yalnızca liste odaktayken; odak yokken yalnızca hover (2026-11-10'dan beri bg.hover, söner-yanar).
- ~~Section başlığı Türkçe büyük harf~~ — 2026-11-10'dan beri bölüm başlıkları büyük harf değil, formlardaki stil
  (`paintFormSection`); bütün sayfalarda aynı.
- Birden fazla dosya bırakılırsa ilk kaynak türündeki açılır; diğerleri son kullanılanlara eklenir ("{n} kaynak daha son
  kullanılanlara eklendi" bildirimi).
- 2026-11-10 (D-133, kullanıcı isteği): bırakma alanı referans araçta sayfanın üçte birini kaplıyordu, artık tek satır;
  son kullanılanlar kart ızgarası (D-091) yerine tek sütun. D-109'dan ve D-091'den sapma.
