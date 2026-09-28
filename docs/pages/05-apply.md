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
Hata), grup başına sonuç tablosu (ok / toplam, atlanan, süre). Logu kaydet: çalışma başından beri log satırları.
Presete kaydet: uygulanan ChangeSet (`.wlpreset`, ChangeSet JSON).

## 6. Bilinen sınırlar
- "Kazanç" imajın açılmış boyutudur (XML TOTALBYTES); install.wim dosyasının kendisi export ile küçülür (P06).
- ResetBase / StartComponentCleanup DISM API'de yok; P06'da ele alınacak.
- Yalnızca özellik/capability işlemleri gerçek; bileşen, sürücü, güncelleme, kayıt defteri, servis Applier'da
  `Unsupported` (kendi sayfalarıyla gelecek) — kuyruğa şu an bunları ekleyen sayfa yok.

## 7. Kabul (kullanıcı testi)
- [ ] Özellikler'de birkaç değişiklik → Uygula sayfası özet: sayaçlar, süre, adımlar doğru.
- [ ] Bir Features on Demand kaldırması varsa onay dialogu çıkar; kutu işaretlenmeden düğme pasif.
- [ ] Uygula → adım listesi ilerler, canlı log akar, durum çubuğunda "Uygulanıyor %".
- [ ] Bitince Tamamlandı: sonuçlar, süreler; imaj çözülmüş; İmajlar'da boyut güncel.
- [ ] (İsteğe bağlı) Durdur → Durduruldu; imaj bağlı, kalanlar kuyrukta.
