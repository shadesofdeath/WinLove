# Durum — "Neredeyiz?"

> Her oturumun sonunda güncellenir. En üstte güncel durum; geçmiş en altta kısa satırlar.

## Güncel
- **Faz:** 2 (motor temeli) ✅ → Faz 3 başlıyor: **P01 Kaynak sayfası**.
- **Aktif iş:** yok.
- **Bir sonraki somut adım:** `docs/pages/01-source.md` spec'i (`_TEMPLATE.md`'den; tasarım 01, s1, s4). Sonra:
  - motor: `openSource` + son kullanılanlar listesi (`%LOCALAPPDATA%\\WinLove\\recent.json`);
  - widget'lar: DropZone, ScrollView/ScrollBar, basit tablo satırı, Dialog (s4 "yönetici gerekli");
  - pencere: `IDropTarget`, dosya açma diyaloğu (`IFileOpenDialog`);
  - sayfa: `app/pages/SourcePage`, kaynak açılınca `AppState`'e yazılır ve İmajlar'a (P02) geçilir.
- **Build:** `./build.ps1 -Test` yeşil: 47 test / 1773 assertion (gerçek ISO testleri dahil; ISO yoksa atlanır).
- **Yönetici testi:** `tools/dism_smoke.ps1` 2026-09-28'de geçti (mount 39 s, unmount 90 s, 0 bağlama kaldı).
- **Kullanıcı kontrolü (elle):** Faz 1 listesi hâlâ geçerli (Snap Layouts, menü, kısayollar). Ek: yönetici terminalinde `build\x64-debug\bin\wlcli.exe mounts` → "no mounted images".
- **Bilinen sorunlar / açık konular:**
  - ScrollBar/ScrollView, Inspector, Dialog, DropZone yok → P01/P02 ile.
  - AppX (provisioned), sürücü, güncelleme, registry, servis işlemleri Applier'da `Unsupported` → ilgili sayfalarla.
  - Tek index'lik "golden" WIM yok; testler 6 index'lik install.wim'i salt okunur bağlıyor.
  - DComp/dirty-rect (D-011), UIA (Faz 4), F6, Türkçe büyük harf.

## Faz 2 özeti
- `base/Log` (dosya/halka/stdout), `core/tasks` (tek motor thread'i, iptal + Win32 event, ilerleme), `Window::post`.
- **Kendi okuyucularımız:** `UdfImage` + `WimFile` → ISO'yu bağlamadan, yönetici olmadan sürüm listesi (72 ms); wimgapi.dll ile doğrulandı.
- **DISM:** ADK'sız, `dismapi.dll` çalışma anında (D-017); mount/unmount/mounts/cleanup + packages/features/capabilities + feature aç/kapat, paket/capability kaldır.
- **Değişiklik modeli:** ChangeSet (undo/redo, JSON preset), Planner, Applier.
- `wlcli`: info, ls, extract, plan, mount, unmount, mounts, cleanup, packages, features, capabilities, apply.
- Laboratuvar: `C:\\WinLoveLab` (install.wim kopyası), `tools/lab_setup.ps1`, `tools/dism_smoke.ps1`.

## Ortam doğrulaması (2026-09-28)
VS 2026 Community (MSVC 14.50/14.51), Windows SDK 10.0.26100, ADK Deployment Tools (dismapi.h burada; build'de kullanılmıyor), dismapi.dll 10.0.26100, Python 3.14 (fonttools, pillow, playwright), Git. CMake yalnızca VS içinde. C: ~323 GB boş (lab ~7 GB). Ana ekran 144 DPI. PowerShell betik politikası kısıtlı (`-ExecutionPolicy Bypass`). UAC istemiyle yönetici betiği çalıştırılabiliyor (`Start-Process -Verb RunAs`).

## Geçmiş
- 2026-09-28 — Faz 2 tamamlandı: log/görevler/yetki, UDF+WIM okuyucular, DISM backend (gerçek imajda test), ChangeSet/Planner/Applier.
- 2026-09-28 — Faz 1 tamamlandı: widget sistemi, temel widget'lar, uygulama kabuğu, galeri, otomatik daralma.
- 2026-09-28 — Faz 1.1–1.3 + 1.5: ilk pencere, özel başlık çubuğu, render altyapısı, görsel doğrulama araçları.
- 2026-09-28 — Faz 0 tamamlandı (iskelet, build, üreticiler, fontlar, marka, testler).
- 2026-09-28 — UI handoff paketi alındı (palet "Bakır", 115 ikon, 27 ekran). Proje planı ve dokümanlar yazıldı.
