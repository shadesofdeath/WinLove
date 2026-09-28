# Durum — "Neredeyiz?"

> Her oturumun sonunda güncellenir. En üstte güncel durum; geçmiş en altta kısa satırlar.

## Güncel
- **Faz:** 3 — sayfalar. **P01 Kaynak: geliştirme bitti, kullanıcı testi bekliyor** (spec §10, aşağıda).
- **Aktif iş:** P01 kullanıcı onayı. Onay gelince P02 İmajlar.
- **Bir sonraki somut adım (P02):** `docs/pages/02-images.md` spec'i (tasarım 02, 03, s2, s3). Gerekecekler: tablo (DataGrid-lite + checkbox), Inspector paneli, mount ilerleme şeridi (03), yönetici akışı (mount'ta UAC), status bar mount segmenti + CTA görünürlüğü, ESD→WIM/dışa aktar (wimgapi `WIMExportImage` → `wlcli export`), breadcrumb "iso › sürüm › durum".
- **Build:** `./build.ps1 -Test` yeşil: 53 test / 1803 assertion.
- **P01 kullanıcı testi** (`build\x64-debug\bin\WinLove.exe`, yönetici OLMADAN):
  1. "Dosya aç…" → `Downloads\Win11_25H2_Turkish_x64_v2.iso` → İmajlar sayfası, başlıkta ISO adı.
  2. Kaynak'a dön → listede ISO: "bugün HH:MM", ISO, "11 25H2 · 26200.8037", "7,56 GB".
  3. Masaüstünden .txt sürükle → kırmızı "Desteklenmeyen dosya"; bırakınca hiçbir şey olmamalı.
  4. ISO'yu Explorer'dan sürükle-bırak → açılmalı. "Klasör…" → `C:\WinLoveLab\iso` → açılmalı.
  5. "Canlı sistemi düzenle" → yönetici dialogu; Esc/Vazgeç kapatır; "Yönetici olarak yeniden başlat" UAC ister.
  6. Uygulamayı kapat/aç → son kullanılanlar duruyor.
- **Bilinen sorunlar / açık konular:**
  - ScrollBar/ScrollView, Inspector, DataGrid yok → P02.
  - Canlı sistem düzenleme Faz 4 (yöneticiyken buton devre dışı + tooltip).
  - AppX, sürücü, güncelleme, registry, servis işlemleri Applier'da `Unsupported`.
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
