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

## 3. Özet
7 sayaç (bileşen, özellik, güncelleme, sürücü, kayıt defteri, servis, tweak; boyut etkisi mono), yüksek risk
InfoBar'ı ("İncele" → onay), tahmini süre InfoBar'ı (tahmin ×0,7–×1,4), işlem sırası tablosu: Planner grupları +
"İmajı kaydet ve çöz" (adım sayısı, tahmini süre). Süre tahminleri `core::ops::estimateSeconds` (ENGINE'e göre
gerçek ölçümlerle güncellenecek).

## 4. Çalışma (motor)
`core::ops::runApplyJob` (engine iş parçacığı): DISM oturumu → `apply(plan, Skip)` → oturumu kapat →
`unmountSafely(commit)`. İlerleme adımların ve commit'in tahmin ağırlıklarıyla tek 0…1 ölçek.
- Hata politikası: hatalı adım **atlanır ve raporlanır** (sonuçta "13 / 14 · 1 atlandı"), sessizce yutulmaz.
- NetFx3 / payload'ı silinmiş özellik: `sources\sxs` (WIM'in yanındaki) DismEnableFeature SourcePaths olarak verilir.
- Durdur: iptal olayı DISM çağrısını keser, kalan adımlar çalışmaz, commit yapılmaz; imaj bağlı kalır, çalışan
  adımlar kuyruktan düşer, kalanlar kuyrukta.
- Başarılı adımlar kuyruktan çıkarılır. Commit başarılıysa imaj çözülür (kuyruk boşalır), kaynak yeniden okunur,
  "Sonra" boyutu WIM XML'inden (TOTALBYTES) gelir.
- CLI: `wlcli apply <changeset.json> <mount> [--commit] [--source=<sources\sxs>]` aynı işi yapar.

## 5. Tamamlandı
Sonuç InfoBar'ı (başarı / atlananlar / commit hatası / durduruldu), 6 sayaç (Önce, Sonra, Kazanç, Süre, Uyarı,
Hata), grup başına sonuç tablosu (ok / toplam, atlanan, süre) ve altında **atlanan her adım için bir satır: adı +
nedeni** (bilinen kodlar Türkçe / İngilizce: 0x80073CFA korumalı uygulama, 0x800F0825 kalıcı paket, 0x800F0806
bekleyen işlem; diğerlerinde DISM'in metni + kod). Logu kaydet: çalışma başından beri log satırları.
Presete kaydet: uygulanan ChangeSet (`.wlpreset`, ChangeSet JSON).

Kaydet ve çöz adımının sonunda WIM, commit'in bıraktığı başvurusuz akışlar olmadan yeniden yazılır
(`core::optimizeWim`; 7-Zip'te görünen `[DELETED]` klasörü kalmaz; 6,8 GB'lık imajda 17 sn). Başarısız olursa imaj
yine kaydedilmiştir, logda uyarı kalır.

Durum çubuğundaki "Uygula · n" düğmesi başka sayfalarda Uygula sayfasını açar; Uygula sayfasının özetindeyken
başlıktaki düğmeyle aynı işi yapar (çalıştırmayı başlatır).

## 6. Bilinen sınırlar
- "Kazanç" imajın açılmış boyutudur (XML TOTALBYTES); install.wim dosyasının kendisi export ile küçülür (P06).
- Bileşen deposu temizliği P07'de (kuyruk işlemi `CleanupImage`, güncellemelerden sonra).
- Kapatılmak istenen özellik aynı çalıştırmada paketiyle birlikte kaldırıldıysa (0x800F080C) adım başarılı sayılır.
- Atlanan adımların adı, imaj çözüldükten sonra DISM adıyla görünebilir (özellik listesi imajla birlikte gider).

## 7. Kabul (kullanıcı testi)
- [ ] Özellikler'de birkaç değişiklik → Uygula sayfası özet: sayaçlar, süre, adımlar doğru.
- [ ] Bir Features on Demand kaldırması varsa onay dialogu çıkar; kutu işaretlenmeden düğme pasif.
- [ ] Uygula → adım listesi ilerler, canlı log akar, durum çubuğunda "Uygulanıyor %".
- [ ] Bitince Tamamlandı: sonuçlar, süreler; imaj çözülmüş; İmajlar'da boyut güncel.
- [ ] (İsteğe bağlı) Durdur → Durduruldu; imaj bağlı, kalanlar kuyrukta.
