# Durum — "Neredeyiz?"

> Her oturumun sonunda güncellenir. En üstte güncel durum; geçmiş en altta kısa satırlar.

## Güncel
- **Faz:** 3 — sayfalar. P01–P04 ✅. P05 Uygula, P06 ISO, P08 Güncellemeler, P09 Sürücüler: 🟨 geliştirme bitti,
  kullanıcı testi bekliyor. P07 Bileşenler: 🟨 v1 (yalnız AppX; CBS paket kaldırma kararı bekliyor).
- **Çalışma şekli:** kullanıcı "her seferinde durma" dedi — sayfa bitince build + test + `-Dist` + yerel commit,
  sonra doğrudan bir sonraki sayfa. Kullanıcı `dist\WinLove.exe`'yi paralel test ediyor.
- **Bir sonraki somut adım:** P10 Servisler — bağlı imajın `Windows\System32\config\SYSTEM` hive'ını çevrimdışı
  yükle (OfflineRegistry, core), `ControlSet001\Services` → servis listesi (Start/Type/ImagePath/DisplayName),
  değişiklik = `SetServiceStart` işlemi.
- **Yönetici gerektiren, terminalden doğrulanamayanlar:** P04 özellik okuma, P05 uygula, P07 AppX, P08 paket,
  P09 sürücü ekleme — gerçek imajda kullanıcı uygulama içinden test ediyor (terminal yönetici değil).
- **Build:** `./build.ps1 -Dist` yeşil, 80 unit test. Kullanıcıya her zaman `dist\WinLove.exe` verilir.
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
- 2026-09-28 — P02–P04 onaylandı; P05, P06, P07 (AppX), P08, P09 geliştirildi (test bekliyor).
- 2026-09-28 — P01 Kaynak geliştirildi (kullanıcı testi bekliyor).
- 2026-09-28 — Faz 2 tamamlandı: log/görevler/yetki, UDF+WIM okuyucular, DISM backend (gerçek imajda test), ChangeSet/Planner/Applier.
- 2026-09-28 — Faz 1 tamamlandı: widget sistemi, temel widget'lar, uygulama kabuğu, galeri, otomatik daralma.
- 2026-09-28 — Faz 0 tamamlandı; UI handoff alındı; proje planı yazıldı.
