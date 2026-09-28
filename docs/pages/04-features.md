# P04 — Özellikler

> Durum: 🟨 geliştirme bitti, kullanıcı testi bekliyor (2026-09-28). Tasarım: 05-features.

## 1. Amaç
Bağlı imajdaki isteğe bağlı Windows özelliklerini ve yüklü Features on Demand'leri (capability) listelemek;
aç/kapa isteklerini **değişiklik kuyruğuna** (ChangeSet, D-003) eklemek. İmaja P05 "Uygula"da dokunulur.

## 2. Ekran
- **Başlık eylemi:** "Değişiklikleri sıfırla" (kuyrukta özellik değişikliği yoksa pasif).
- **Araç çubuğu:** SearchBox 240 ("Özellik ara", `/` veya Ctrl+F) · Dropdown "Durum" (Tümü / Açık / Kapalı /
  Kuyrukta) · Toggle "Yalnızca değişenler" · sağda "n değişiklik kuyrukta".
- **TableView:** Özellik (esnek; ikon + DISM görünen adı) · Tür 140 (Windows Feature / Features on Demand) ·
  Durum 152 (ikon + metin) · Hedef 72 (switch) · Boyut 80 (capability kurulum boyutu, mono, sağa).
- Nav'da "Özellikler" rozeti = kuyruktaki özellik değişikliği sayısı; durum çubuğunda "Uygula · n".
- Bağlı imaj yok → EmptyState + "İmajlar'a git". Okunurken → EmptyState (spinner). Hata → EmptyState + "Yeniden dene".

## 3. Veri (motor)
`core::readOptionalFeatures(dism, mountDir)` — tek DISM oturumu: `DismGetFeatures` + `DismGetCapabilities`, her
öğe için `DismGetFeatureInfo` / `DismGetCapabilityInfo` (görünen ad imajın dilinde, açıklama, boyut, yeniden
başlatma). Yüklü olmayan capability'ler listelenmez (çevrimdışı eklemek FoD kaynağı ister). Ada göre sıralı.
Bağlı imaj başına bir kez okunur (`AppState::optionalFeatures`), mount değişince atılır.
CLI: `wlcli optional-features <mountdir> [--json]` (yönetici) — sayfanın gösterdiğinin aynısı + süre logu.

## 4. Kuyruk kuralları (FeatureController)
| Öğe | Hedef tıklanınca | Durum metni |
|---|---|---|
| Özellik açık (Installed) | DisableFeature | Etkin → "Devre dışı bırakılacak" |
| Özellik kapalı (Staged / NotPresent / Removed) | EnableFeature | Devre dışı / Kaldırılmış → "Etkinleştirilecek" |
| Capability yüklü | RemoveCapability (boyut kazancı) | Yüklü → "Kaldırılacak" |
| Capability yok | — (pasif, ipucu) | — |
Tekrar tıklamak kuyruktaki işlemi siler (imajın kendi durumuna döner). Mount değişince kuyruk temizlenir.
Space/Enter/çift tık seçili satırı değiştirir.

## 5. Açık konular (P05'e)
- "Kaldırılmış" (payload silinmiş) özelliği etkinleştirmek ve .NET 3.5 (NetFx3) kaynak ister: Applier
  `DismEnableFeature`'a ISO çalışma klasöründeki `sources\sxs`'i SourcePaths olarak verecek.
- Bağımlılıklar (ör. Hyper-V alt özellikleri) — `enableAll` ile P05'te.
- İlk okuma süresi ölçülecek (dism logunda "features + capabilities read in N ms"); uzunsa disk önbelleği.

## 6. Kabul (kullanıcı testi)
- [ ] Bağlı imaj yokken sayfa "Bağlı imaj yok" gösterir, düğme İmajlar'a götürür.
- [ ] Bir sürüm bağlıyken sayfa açılır → "Özellikler okunuyor" → liste (Türkçe adlar).
- [ ] Bir özelliğin Hedef'ine tıkla → "Etkinleştirilecek"/"Devre dışı bırakılacak"; rozet ve "Uygula · n" artar.
- [ ] Tekrar tıkla → eski duruma döner. "Değişiklikleri sıfırla" hepsini temizler.
- [ ] Arama, Durum filtresi, "Yalnızca değişenler" çalışır; `/` aramaya odaklar.
