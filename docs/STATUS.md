# Durum — "Neredeyiz?"

> Her oturumun sonunda güncellenir. En üstte güncel durum; geçmiş en altta kısa satırlar.

## Güncel
- **Faz:** 3 — sayfalar. P01–P04 ✅. P05 Uygula, P06 ISO, P08 Güncellemeler, P09 Sürücüler, P10 Servisler, P11 Kayıt Defteri,
  P12 Ayarlar / Tweaks, P13 Katılımsız Kurulum: 🟨 geliştirme bitti, kullanıcı testi bekliyor. P07 Bileşenler: 🟨 v1 (yalnız AppX; CBS paket
  kaldırma kararı bekliyor).
- **Çalışma şekli:** kullanıcı "her seferinde durma" dedi — sayfa bitince build + test + `-Dist` + yerel commit,
  sonra doğrudan bir sonraki sayfa. Kullanıcı `dist\WinLove.exe`'yi paralel test ediyor.
- **Bir sonraki somut adım:** P14 Kurulum Sonrası (tasarım 12) — sıralı adım tablosu (winget / komut / dosya kopyala).
  Spec yok: önce `docs/pages/14-post-setup.md`. Mekanizma D-026 ile aynı klasör (`Windows\Setup\Scripts\WinLove\`,
  SetupComplete.cmd + ilk oturum RunOnce); işlemler bağlı imaja yazılacağı için yeni OpKind gerekir.
- **P13 Katılımsız Kurulum (2026-09-30):** `core/unattend` (üret / oku / doğrula), `UnattendController`,
  `UnattendedPage` (adım çubuğu = çapalar, `ui::FormView`, canlı XML önizleme + değişen satır vurgusu),
  "ISO'ya ekle" → `IsoOptions::rootFiles` ile köke bellekten (D-028). ISO üretimi + geri okuma unit testte gerçek
  IMAPI ile doğrulandı. **VM'de kurulum denenmedi:** üretilen XML'in Setup tarafından kabul edildiği kullanıcı
  testi bekliyor (özellikle disk düzeni ve BypassNRO).
- **P12 Ayarlar / Tweaks (2026-09-30):** `resources/catalog/settings.json` (5 sekme, 13 bölüm, 39 ayar; toggle /
  dropdown / radio), `ImageSettingsController` — seçili seçenek **kuyruktan türetilir** (ayrı durum yok), bu yüzden
  P11 tweak'leri, Servisler ve presetlerle kendiliğinden tutarlı. Form kaydırılabilir. Sınır: imajdaki mevcut değer
  okunmuyor (form Windows varsayılanını gösterir). Kayıt değerleri bilgiye dayalı, gerçek kurulumda doğrulanmadı.
- **Yönetici gerektiren, terminalden doğrulanamayanlar:** P04 özellik okuma, P05 uygula, P07 AppX, P08 paket,
  P09 sürücü ekleme, P10 servis okuma/yazma, P11 kayıt defteri + ilk oturum dosyaları — gerçek imajda kullanıcı uygulama içinden test ediyor (terminal yönetici değil).
- **Ön okuma (D-027, 2026-09-30):** mount / geri yükleme biter bitmez `PreloadController` Özellikler → Bileşenler →
  Servisler listelerini sırayla okur; İmajlar şeridinde ve durum çubuğunda ikinci ilerleme. Unit testli, render'da
  doğrulandı (`--operation=read`); gerçek imajda kullanıcı testi bekliyor (yönetici gerekir).
- **İlk oturumda sıfırlanan kayıt değerleri (karar, 2026-09-30):** NTLite ile aynı yol kalır — SetupComplete.cmd +
  ilk oturum RunOnce (D-026 eki); unattend / Active Setup alternatifleri seçilmedi. İçe aktarılan .reg dosyalarının
  **her değeri** artık çevrimdışı yazım + kurulum sonrası yeniden içe aktarım. .reg düzeltmeleri: değersiz `[anahtar]`
  (CreateKey), "sil + varsayılan değeri yaz" kalıbında silmenin kuyrukta ezilmesi, tekrarlanan değerde sıra, HKCC
  ve HKU\S-1-5-18/19/20 kökleri. `reg.exe import` davranışı yerelde doğrulandı (ENGINE saha notu). **Gerçek kurulumda
  (VM) doğrulanmadı:** SetupComplete / RunOnce içe aktarımının kurulum sonunda çalışması kullanıcı testi bekliyor.
- **Build:** `./build.ps1 -Dist` yeşil, 123 unit test. Kullanıcıya her zaman `dist\WinLove.exe` verilir.
- **Kurallar:** kullanıcının diskinde klasör açma (lab = `build\lab`, çalışma kökü `%LOCALAPPDATA%\WinLove`),
  "Son kullanılanlar"a test yolu yazma, DISM'e giden yolları `nativePath` ile ver, asla push etme.
- **Açık konular / sonraya:** P06 USB sekmesi; güncellemelerde sürükle-sırala; imajdaki mevcut sürücüleri
  listeleme/kaldırma; `C:\WinLove` eski klasörü (kullanıcı unmount sonrası silebilir); DComp/dirty-rect (D-011),
  UIA (Faz 4).

## Son eklenenler (P01)
- Motor: klasör kaynağı, `WindowsRelease` (sürüm adları), `LiveSystem`; `wlcli live`, `wlcli info <klasör>`.
- UI: `DropZone`, `InfoBar`, `Dialog` + Host modal katmanı, çok satırlı metin, Türkçe-duyarlı büyük harf, `DropTarget` (OLE), `FileDialog`.
- Uygulama: `AppState`, `RecentSources`, `Format`, `SourcePage` (+ `RecentList`, `LiveCard`), çok parçalı breadcrumb, `WinLove.exe <yol>`.

## Ortam doğrulaması (2026-09-28)
VS 2026 Community (MSVC 14.50/14.51), Windows SDK 10.0.26100, ADK Deployment Tools, dismapi.dll 10.0.26100, Python 3.14 (fonttools, pillow, playwright), Git. CMake yalnızca VS içinde. C: ~323 GB boş (lab ~7 GB). Ana ekran 144 DPI. PowerShell betik politikası kısıtlı (`-ExecutionPolicy Bypass`). UAC istemiyle yönetici betiği çalıştırılabiliyor.

## Geçmiş
- 2026-09-30 — Ön okuma: mount sonrası ikinci ilerleme ile sayfa listeleri önceden okunuyor (D-027).
- 2026-09-30 — İçe aktarılan .reg dosyaları kurulum sonrası da uygulanıyor; .reg ayrıştırma / kuyruk düzeltmeleri.
- 2026-09-30 — P12 Ayarlar / Tweaks geliştirildi (test bekliyor).
- 2026-09-30 — P13 Katılımsız Kurulum geliştirildi (test bekliyor); `ui::FormView` ortak form bileşeni.
- 2026-09-28 — Baştan sona inceleme (4 alan, paralel): ~35 hata düzeltildi. Öne çıkanlar: junction üzerinden ana
  makine ACL'si değişebilmesi (FileLocks), commit edilmeyen unmount'un başarılı raporlanması, DISM oturum yenileme,
  UDF çıkarmada yol dışına yazma + sınır dışı okuma, tıklamada yok edilen widget (use-after-free), Enter ile devre
  dışı onay düğmesi, iş sürerken pencere kapatma, uygulama sırasında kuyruk düzenleme, bozuk preset/ayar dosyasında
  çökme, argüman tırnaklama / yeniden başlatma döngüsü, REGEDIT4 ANSI, INF x86, servis adlarında imaj dili.
- 2026-09-28 — P02–P04 onaylandı; P05, P06, P07 (AppX), P08, P09 geliştirildi (test bekliyor).
- 2026-09-28 — P01 Kaynak geliştirildi (kullanıcı testi bekliyor).
- 2026-09-28 — Faz 2 tamamlandı: log/görevler/yetki, UDF+WIM okuyucular, DISM backend (gerçek imajda test), ChangeSet/Planner/Applier.
- 2026-09-28 — Faz 1 tamamlandı: widget sistemi, temel widget'lar, uygulama kabuğu, galeri, otomatik daralma.
- 2026-09-28 — Faz 0 tamamlandı; UI handoff alındı; proje planı yazıldı.
