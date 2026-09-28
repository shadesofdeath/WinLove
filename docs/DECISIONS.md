# Karar Kaydı (ADR)

> Format: `## D-NNN — Başlık (tarih)` → Bağlam · Karar · Sonuç. Kararlar silinmez; değişirse yeni kayıt "D-xxx'i geçersiz kılar" der.

## D-001 — Custom-draw UI: D2D + DirectWrite + DirectComposition (2026-09-28)
Bağlam: Özgün, kompakt, Fluent olmayan bir arayüz isteniyor. Hazır toolkit'ler kendi görünümünü dayatıyor.
Karar: Tüm UI kendi framework'ümüz `wl::ui` ile çizilir. Win32 kontrolleri yalnızca sistem diyalogları (dosya aç/kaydet) için.
Sonuç: Widget'lar, erişilebilirlik (UIA) ve metin düzenleme bizim sorumluluğumuzda; zaman maliyeti Faz 1'de ödenir.

## D-002 — Motor soyutlaması `wl::image` (2026-09-28)
Bağlam: DISM C API'si eksik (AppX yok, kalıcı paketler kaldırılamaz), dism.exe çıktı ayrıştırma kırılgan.
Karar: Uygulama yalnızca `wl::image` arayüzünü kullanır; arkada dismapi/wimgapi/dism.exe/kendi native kodumuz. (`ENGINE.md`)

## D-003 — ChangeSet modeli (2026-09-28)
Karar: Hiçbir değişiklik anında imaja yazılmaz; hepsi `Operation` olarak ChangeSet'e girer ve Uygula ile işlenir. Presetler ChangeSet dosyasıdır.

## D-004 — Sayfa sırası: uçtan uca hat önce (2026-09-28)
Karar: Kaynak → İmajlar → Loglar → Özellikler → Uygula → ISO, ardından Bileşenler. Gerekçe: zor sayfalardan önce seç-değiştir-uygula-ISO-kur hattının gerçek imajda çalışması.

## D-005 — Uygulama `asInvoker` başlar (2026-09-28)
Karar: Admin yalnızca imaj işlemleri için gerekir; gerektiğinde "yönetici olarak yeniden başlat". UI geliştirme ve render testleri admin'siz çalışır.

## D-006 — Bağımlılıklar vendored (2026-09-28)
Karar: vcpkg yok; doctest, nlohmann/json, pugixml `third_party/` altında. Gerekçe: her AI oturumunda sıfır kurulumla build.

## D-007 — UI metinlerinin yaşayan kaynağı `resources/strings/` (2026-09-28)
Bağlam: Handoff'taki `07_copy/strings.*.json` başlangıç seti; sayfalar geliştikçe yeni anahtar gerekecek, handoff ise salt okunur.
Karar: Metinler `resources/strings/{tr,en}.json`'a kopyalandı ve bundan sonra orada yaşar. `tools/gen_strings.py` iki dilin anahtar ve `{yer tutucu}` eşitliğini denetler, `Str::` enum'u üretir. Tokenlar ve ikonlar ise doğrudan handoff'tan üretilir.

## D-008 — `base` katmanı (2026-09-28)
Bağlam: `ui` de hata tipine (`Result/Error`) ve UTF-8 yardımcılarına ihtiyaç duyuyor, ama `core`'a bağımlı olamaz.
Karar: `src/base` (`wl_base`, namespace `wl`) eklendi; `core` ve `ui` yalnızca buna bağımlı. İçine sadece gerçekten ortak olan girer (Result, UTF-8; ileride Log).

## D-009 — C++23 ve statik CRT (2026-09-28)
Karar: `std::expected` için C++23 (`/std:c++latest`). CRT statik (`/MT`): exe'ler USB/WinPE üzerinde yeniden dağıtılabilir paket olmadan çalışsın.

## D-010 — Üretilen kod depoya girer (2026-09-28)
Karar: `src/*/generated/*.g.h` commit edilir; build Python olmadan da çalışır. `build.ps1` her build'de üreticileri çalıştırır (değişiklik yoksa dosyaya dokunmaz), böylece JSON değişip üretim unutulamaz.

## D-011 — Önce HWND flip swapchain, DirectComposition sonra (2026-09-28)
Bağlam: Tasarım rehberi DComp görsel ağacı öneriyor (panel/dialog animasyonları için). İlk pencere için gereksiz karmaşıklık.
Karar: Şimdilik tek HWND flip-sequential swapchain, her karede tam çizim (yalnızca `invalidate` ile; boşta 0 CPU ölçüldü). DComp ve dirty-rect, animasyon ve widget invalidation'ı gerektiğinde (Faz 1.4) gelir.

## D-012 — Geçici TitleBar (2026-09-28)
Karar: `app/shell/TitleBar` widget ağacı olmadan kendini çizip hit-test ediyor ki pencere ve Snap Layouts erken doğrulanabilsin. Faz 1.4'te IconButton/Kbd/SearchTrigger widget'larına bölünecek; davranış (hit-test bölgeleri, hover durumları) korunacak.

## D-013 — Görsel doğrulama yalnızca kendi penceremiz (2026-09-28)
Bağlam: Ekran görüntüsüyle doğrulama denemesi kullanıcının önündeki başka bir pencereyi yakaladı (görüntüler hemen silindi).
Karar: Tüm ekranı yakalamak yasak. Yalnızca `--render` (offscreen) ve `tools/capture_window.py` (`PrintWindow`, yalnızca WinLove HWND'si).
