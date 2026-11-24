# P05 — Uygula

> Durum: 🟨 geliştirme bitti, kullanıcı testi bekliyor (2026-09-28). Tasarım: 13, 13b, 14, 15.

## 1. Amaç
Kuyruktaki değişiklikleri (ChangeSet) bağlı imaja uygulamak, sonunda imajı **kaydedip çözmek**, sonucu raporlamak.
Kuyruk bu sayfaya kadar imaja dokunmaz (D-003).

## 2. Modlar (tek sayfa, `ApplyPage::modeFor(AppState)`)
| Mod | Ne zaman | Başlık eylemleri |
|---|---|---|
| Boş / Bağlı imaj yok | kuyruk boş | — (EmptyState: Özelliklere / İmajlar'a git) |
| **Özet (13)** | bağlı imaj + kuyruk dolu | Preset olarak kaydet · **Uygula · n** |
| **Onay (13b)** | Uygula'da yüksek riskli işlem varsa | dialog: liste + "Riskleri anladım" + Danger "Kaldır ve uygula" |
| **Çalışıyor (14)** | çalışma sürerken | Durdur (durum çubuğu CTA'sı da "Durdur") |
| **Tamamlandı (15)** | çalışma bitti, kuyruk boş | Logu kaydet · Presete kaydet · **ISO Oluştur** |
Mod değişince Shell sayfayı *ertelenmiş* olarak yeniden kurar (tıklanan düğme kendi olayında yok edilmesin).

> **D-138 (2026-11-11):** her modun başında bir **baş kartı** var (`pages/apply/ApplyCards`; bg.raised, r3, durumu
> olan kartta solda 3 px durum şeridi). Ekranın **tek dolu vurgu düğmesi** bu karttadır (şerit düğmeleri D-109'dan
> beri hep şerit görünümünde); kartın düğmeleri şeritteki eylemlerle aynı işi yapar (`ApplyPage::Intents`: start,
> openFolder, stop, resume, buildIso, saveReport, saveToPreset, saveLog), şerit ve akışlar değişmedi. Sayılar
> `pages/apply/RunSummary` (`summarizeRun`, `planRows`, `planSeconds`) ile; Görüntü › Özet de aynısını kullanır.

## 3. Özet
**Plan kartı:** "39 değişiklik · 6 aşama" (aşama = Planner grubu + kaydet), imaj ("Windows 11 Pro · sonunda imaj
kaydedilir ve çözülür"), uyku uyarısı; ortada boyut önce → ≈ sonra, çubuk (kalan vurgu, giden yeşil) ve "1,05 GB
küçülür · %4", tahmini süre aralığı (×0,7–×1,4); sağda **Uygula · n** (dolu; `Shell::requestApply`, riskliyse onay).
Altında, değişmeden: indirme bekleme InfoBar'ı, yüksek risk InfoBar'ı ("İncele" → onay), etki raporu (D-124),
seçenekler (diğer sürümler, kaydetmeden önce dur, güncellemelere karşı koru, **Bitince — ISO oluştur**). Plan tablosu: Sıra · İşlem (grubun
yüksek riskli adımı varsa sağında "● 2 riskli") · Adım sayısı · **Boyut etkisi** (grubun tahminlerinin toplamı) ·
Tahmini süre; son satır "İmajı kaydet ve çöz". Eski 7 sayaçlı şerit (çoğu "0 –") ve ayrı süre InfoBar'ı kalktı:
ikisinin bilgisi plan kartında ve tabloda. Hücre metinleri sayfa kurulurken bir kez yapılır (çizimde biçimleme yok).
Süre tahminleri `core::ops::estimateSeconds`. Boyut değişmiyorsa plan kartının çubuğu yalnız nötr iz (D-148).

**Bitince — ISO oluştur (D-144, 2026-11-11):** seçenek satırlarının üçüncüsü: [ ] **ISO oluştur** · dosya
(`<isoFolder>\<kaynak adı>_WinLove.iso`, `isoFolder` boşsa Masaüstü) · **Değiştir…** (kaydetme diyaloğu; klasör
`isoFolder`'a, ad yalnız bu kaynak için bellekte) · **ISO seçenekleri…** (ikisi seçenek açık ve kullanılabilirken). Altında
tek satır özet (`isoChoicesSummary`): "UEFI + BIOS · etiket CPRA_X64FRE_TR-TR_DV9 · SHA-256"; yalnız varsayılandan farklı ya
da hep bilinmesi gerekenler. Kaynak buna uygun değilse pasif + turuncu neden (kaynak açık değil / tek WIM / ESD / sanal disk
/ yanıt dosyasında Kurulum'un reddedeceği değer; kural `IsoController::sourceBlocker`). Kutu `AppSettings::isoAfterApply`
(settings.json, varsayılan kapalı; ön ayara girmez). **ISO seçenekleri…** iletişim kutusu (760 geniş) ISO Oluştur
sayfasının satırlarıdır, aynı durumu paylaşır (`AppState::IsoChoices`; biri değişince öteki de), değişiklik anında geçerli,
Tamam / Esc kapatır; seçenekler uygulamanın oturumu boyunca kalır (settings.json'a yazılmaz), etiket ve önceki kurulum
başka kaynak açılınca sıfırlanır.
`core::ops::runApplyJob` (engine iş parçacığı): DISM oturumu → `apply(plan, Skip)` → oturumu kapat →
`unmountSafely(commit)`. İlerleme adımların ve commit'in tahmin ağırlıklarıyla tek 0…1 ölçek.
- Hata politikası: hatalı adım **atlanır ve raporlanır** (sonuçta "13 / 14 · 1 atlandı"), sessizce yutulmaz.
- NetFx3 / payload'ı silinmiş özellik: `sources\sxs` (WIM'in yanındaki) DismEnableFeature SourcePaths olarak verilir.
- Durdur: iptal olayı DISM çağrısını keser, kalan adımlar çalışmaz, commit yapılmaz; imaj bağlı kalır, çalışan
  adımlar kuyruktan düşer, kalanlar kuyrukta.
- Başarılı adımlar kuyruktan çıkarılır. Commit başarılıysa imaj çözülür (kuyruk boşalır), kaynak yeniden okunur,
  "Sonra" boyutu WIM XML'inden (TOTALBYTES) gelir.
- CLI: `wlcli apply <changeset.json> <mount> [--commit] [--source=<sources\sxs>]` aynı işi yapar.

**Çalışıyor kartı (D-138):** o anki aşama büyük ("Bileşenleri kaldır"), yanında adım ("Xbox"), sağda yüzde büyük;
"9 / 40 adım · geçen 3 dk 34 sn · kalan ≈ 4 dk" (kaydetme de bir adım, üstteki satırla aynı sayı; geçen süre
sayfanın 250 ms zamanlayıcısıyla saniyede bir yenilenir), 6 px ölçer, "● 8 tamam ● 1 atlandı ● 30 bekliyor"; D-055'te
sağda "Sürüm 2 / 3 · …". Kartın yüksekliği çalışma boyunca sabit; altında "Adımlar" (gruplar, çalışan satır seçim
dolgusu + vurgu çubuğu) ve "Canlı log" (20.000 satır sınırı aynı) — satırlar adım bittikçe yer değiştirmez.
Baş kartta ve adım listesinin çalışan satırında WinLove'un ilerleme halkası döner (D-142; önce dönmüyordu). "Bitince ISO
oluştur" açıksa listenin son satırı "ISO oluştur · imaj kaydedilince · <dosya>"; durum çubuğu "Uygulanıyor · ardından ISO".
**Duraklatıldı (D-106):** aynı kart, "Kaydetmeden önce durdu" (bilgi rengi), kalan süre tahmini yok (kullanıcıyı
bekliyor); altında nedeni (bağlama klasörü yolu) ve üç seçim: Klasörü aç · Durdur · **Kaydet ve devam et**.

## 5. Tamamlandı
**Sonuç kartı (D-138):** sonuç tek cümlede (Tamamlandı / N işlem atlandı / N işlem başarısız — imaj kaydedilmedi /
Kaydedilmedi; imaj bağlı / Durduruldu / Uygulama tamamlanamadı), sağda "9 dk 42 sn sürdü"; ne olduğu düz sözlerle
(atlananlar · kaydedilmedi · durduruldu metinleri; D-131 oturum kaybında "DISM oturumu yeniden açılamadı: …";
diğer sürümlerin sonucu); üç sütun: Boyut "24,06 GB → 23,00 GB" + çubuk (kaydedilmediyse yalnız önceki), Kazanç
"1,05 GB · %4" (yeşil) ya da "—", Adımlar "● 39 tamam ● 0 atlandı (● N çalışmadı)"; sıradaki adım düğmeleri:
kaydedildiyse **ISO Oluştur** · Raporu kaydet · Presete kaydet; bağlı ve kaydedilmemişse **Kaydet…** (çözme
dialogu: kaydet ya da vazgeç) · Raporu kaydet · Logu kaydet; kaydetme / başlatma hatasında **Görüntü'ye git** · Logu
kaydet. Eski sonuç InfoBar'ı ve 6 sayaçlı şerit kalktı. Altında grup başına sonuç tablosu (ok / toplam, atlanan,
süre) ve **atlanan her adım için bir satır: adı + nedeni** (bilinen kodlar Türkçe / İngilizce: 0x80073CFA korumalı
uygulama, 0x800F0825 kalıcı paket, 0x800F0806 bekleyen işlem; diğerlerinde DISM'in metni + kod). Logu kaydet:
çalışma başından beri log satırları. Presete kaydet: uygulanan ChangeSet (`.wlpreset`, ChangeSet JSON).

**ISO bandı (D-144):** "Bitince ISO oluştur" açıkken ve çalıştırma imajı kaydettiyse (bütün adımlar tamam ya da
atlananlarla kaydedilmiş) ISO, imaj kaydedilip çözüldükten ve kaynak yeniden okunduktan sonra kendiliğinden başlar (tek
ISO motoru `IsoController::start`; istek ISO sayfasıyla ortak seçeneklerden, ayrıca çalışma kopyası olduğu gibi kullanılır,
Gezgin açılmaz). Sonuç kartında bant: halka + "ISO oluşturuluyor · ISO yazılıyor" + ölçer + % + **ISO'yu durdur** → ✓ "ISO
hazır: <dosya> · 5,96 GB" + **Klasörde göster** (kartın dolu düğmesi; "ISO Oluştur" ikincil) / ⛔ "ISO oluşturulamadı:
<neden>" / iptal / ⓘ "ISO oluşturulmadı: <neden>" (başlatılamadı / kaydedilemedi, durduruldu, imaj kaydedilmedi — held ya
da duraklatmada Durdur —, diğer sürüm kaydedilemedi, ISO Oluştur reddetti, karşılama betiği kurulacak sürümde yok).
Duraklatmada **Kaydet ve devam et** kaydeder → ISO başlar. Durum çubuğu "Uygulandı · ISO oluşturuluyor". Sonradan ISO
sayfasında yapılan başka bir ISO bu kartı değiştirmez.

**Durdurulduktan sonra kaydet / çöz (D-148):** durdurulan (ya da kaydedilmeden kalan) çalışmanın imajı Kaydet… ile ya da
Görüntü'den çözülünce sonuç kartı kapanışı izler: "İmaj kaydediliyor… / Değişiklikler atılıyor…" + motorun yüzdesiyle
ölçer → "Kaydedildi ve çözüldü" (sıradaki: ISO Oluştur) / "İmaj çözüldü, değişiklikler atıldı" (sıradaki: Görüntü'ye git)
/ "Kaydedilemedi / Çözülemedi" + gerekçe ("İmaj hâlâ bağlı; yeniden deneyebilirsin", Kaydet…). Yalnız çalıştırmanın
bıraktığı imaj için (kaydetmiş çalıştırmada ya da sonradan bağlanan imajda devreye girmez). Başka bir araçla bağlanmış
imajda (D-148) kayıttan sonra WIM yeniden yazılmaz (aşağıdaki `optimizeWim` atlanır).

Kaydet ve çöz adımının sonunda (başka araçla bağlanmış imaj dışında, D-148) WIM, commit'in bıraktığı başvurusuz akışlar olmadan yeniden yazılır
(`core::optimizeWim`; 7-Zip'te görünen `[DELETED]` klasörü kalmaz; 6,8 GB'lık imajda 17 sn). Başarısız olursa imaj
yine kaydedilmiştir, logda uyarı kalır.

Durum çubuğundaki "Uygula · n" düğmesi başka sayfalarda Uygula sayfasını açar; Uygula sayfasının özetindeyken
başlıktaki düğmeyle aynı işi yapar (çalıştırmayı başlatır).

**Raporu kaydet** (D-041): Tamamlandı ekranının başlığında. Tek sayfalık, dışa bağımlılığı olmayan HTML (UTF-8):
imaj, kaynak, tarih; önce / sonra / kazanç, süre, adım sayısı, kaydedildi mi; her adım için grup, ad, sonuç
(atlananlar sayfadaki sözcüklerle nedenli) ve süre. Varsayılan ad `WinLove-rapor-YYYYMMDD-HHMM.html`.

## 6. Bilinen sınırlar
- "Kazanç" imajın açılmış boyutudur (XML TOTALBYTES); install.wim dosyasının kendisi export ile küçülür (P06).
- Bileşen deposu temizliği P07'de (kuyruk işlemi `CleanupImage`, güncellemelerden sonra).
- Kapatılmak istenen özellik aynı çalıştırmada paketiyle birlikte kaldırıldıysa (0x800F080C) adım başarılı sayılır.
- Atlanan adımların adı, imaj çözüldükten sonra DISM adıyla görünebilir (özellik listesi imajla birlikte gider).

Render durumları (yönetici gerekmez): `--demo-features --demo-apply=confirm|running|paused|done|skipped|held`
(iki değişiklik) ve D-138'in zengin kuyruğu `--demo-overview[=running|paused|done|held] --page=apply` (isteğe bağlı
kaynak yolu ile: `build\lab\uup\out\x64-9448.iso`). 2026-11-11: `--demo-apply=stopped|discarding|discarded|saving|saved|
closefailed` (D-148), `--auto-iso` (seçenek açık, ISO klasörü sabit `D:\ISO`), `--demo-overview=iso-running|iso-done|
iso-failed|iso-cancelled|iso-skipped --page=apply`, `--iso-choices`, `--iso-options` (D-144), `--anim-time=<ms>` (D-142).

## 7. Kabul (kullanıcı testi)
- [ ] Özellikler'de birkaç değişiklik → Uygula sayfası özet: sayaçlar, süre, adımlar doğru.
- [ ] Bir Features on Demand kaldırması varsa onay dialogu çıkar; kutu işaretlenmeden düğme pasif.
- [ ] Uygula → adım listesi ilerler, canlı log akar, durum çubuğunda "Uygulanıyor %".
- [ ] Bitince Tamamlandı: sonuçlar, süreler; imaj çözülmüş; İmajlar'da boyut güncel.
- [ ] (İsteğe bağlı) Durdur → Durduruldu; imaj bağlı, kalanlar kuyrukta.
- [ ] Durdur → Kaydet… (kaydetmeden çöz) → kart "Değişiklikler atılıyor…" + ölçer → "İmaj çözüldü, değişiklikler
      atıldı"; Kaydet ile → "Kaydedildi ve çözüldü", sıradaki ISO (D-148; birim testi + render, pencerede denenmedi).
- [ ] Bitince ISO oluştur (D-144): kutuyu aç → dosya + Değiştir… + ISO seçenekleri… görünür; seçenekleri değiştir → özet
      satırı ve ISO Oluştur sayfası aynı; Uygula → imaj kaydedilince ISO kendiliğinden başlar, kartta bant, bitince
      "Klasörde göster"; bir adım başarısız olup imaj kaydedilmezse ISO yok ve nedeni yazılı. VHD / tek WIM kaynakta
      seçenek pasif. Birim testi + render; gerçek imajla ve pencerede denenmedi.
- [ ] Çalışırken baş kartta ve adım listesinde halka döner (D-142; hareketi azaltta duran yay).
