# Durum — "Neredeyiz?"

> Her oturumun sonunda güncellenir. En üstte güncel durum; geçmiş en altta kısa satırlar.

## Güncel
- **Faz:** 3 — sayfalar. **P01 ✅, P02 İmajlar ✅ (kullanıcı onayı 2026-09-28).** Aktif: **P03 Loglar**.
- **Bir sonraki somut adım:** `docs/pages/03-logs.md` spec'i (tasarım 18). Uygulama artık oturum logunu
  `%LOCALAPPDATA%\WinLove\logs\` altına yazıyor (dism.log yanında); Loglar sayfası bu klasörü okuyacak.
- **Kullanıcının tekrar test edeceği:** P02 — imaj bağlıyken uygulamayı kapat/aç → geri yüklenmeli
  (otomatik; olmazsa sayfada "Devam et").
- **Build:** `./build.ps1 -Dist` yeşil. Kullanıcıya her zaman `dist\WinLove.exe` verilir (çalışıyorsa
  betik eskisini `.old` yapar).
- **Kurallar (bu oturumda öğrenildi):** kullanıcının diskinde klasör açma (lab = `build\lab`), kullanıcının
  "Son kullanılanlar" listesine test yolu yazma, DISM'e giden yolları `nativePath` ile ver.
- **Bilinen sorunlar / açık konular:**
  - Tasarımdaki arama/filtre (İmajlar) ertelendi; ScrollView hâlâ yok (uzun listeler P04'te gerekecek).
  - Restart Manager yalnız ilk iki seviye + hive dosyalarına bakar; konsolun çalışma klasörü tespit edilmez.
  - DComp/dirty-rect (D-011), UIA (Faz 4), F6.

## Son eklenenler (P01)
- Motor: klasör kaynağı, `WindowsRelease` (sürüm adları), `LiveSystem`; `wlcli live`, `wlcli info <klasör>`.
- UI: `DropZone`, `InfoBar`, `Dialog` + Host modal katmanı, çok satırlı metin, Türkçe-duyarlı büyük harf, `DropTarget` (OLE), `FileDialog`.
- Uygulama: `AppState`, `RecentSources`, `Format`, `SourcePage` (+ `RecentList`, `LiveCard`), çok parçalı breadcrumb, `WinLove.exe <yol>`.

## Ortam doğrulaması (2026-09-28)
VS 2026 Community (MSVC 14.50/14.51), Windows SDK 10.0.26100, ADK Deployment Tools, dismapi.dll 10.0.26100, Python 3.14 (fonttools, pillow, playwright), Git. CMake yalnızca VS içinde. C: ~323 GB boş (lab ~7 GB). Ana ekran 144 DPI. PowerShell betik politikası kısıtlı (`-ExecutionPolicy Bypass`). UAC istemiyle yönetici betiği çalıştırılabiliyor.

## Geçmiş
- 2026-09-28 — P01 Kaynak geliştirildi (kullanıcı testi bekliyor).
- 2026-09-28 — Faz 2 tamamlandı: log/görevler/yetki, UDF+WIM okuyucular, DISM backend (gerçek imajda test), ChangeSet/Planner/Applier.
- 2026-09-28 — Faz 1 tamamlandı: widget sistemi, temel widget'lar, uygulama kabuğu, galeri, otomatik daralma.
- 2026-09-28 — Faz 0 tamamlandı; UI handoff alındı; proje planı yazıldı.
