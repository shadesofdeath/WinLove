# Test Stratejisi

## Katmanlar
| Tür | Nerede | Admin | Ne zaman |
|---|---|---|---|
| Unit (core) | `tests/core` — doctest | Hayır | Her build (`./build.ps1 -Test`) |
| Unit (ui) | `tests/ui` — layout hesapları, hit-test, text ölçüm, tema | Hayır | Her build |
| Render | `tests/render` — `--render-page` çıktısının referans PNG'ye piksel farkı (eşik) | Hayır | Her build (referans onaylandıktan sonra) |
| Integration | `tests/integration` — gerçek imajda `wl::image` + `wlcli --json` | **Evet** | `./build.ps1 -Test -Integration`, sayfa kapanışında zorunlu |
| Uçtan uca | Oluşturulan ISO'nun VM'de kurulması (Hyper-V) | Evet | P06'dan itibaren her motor değişikliğinde |
| Manuel | Sayfa spec'indeki "Kullanıcı test senaryosu" | Kullanıcı | Sayfa kapanışı |

## Test laboratuvarı: `build\lab\`

> 2026-09-28: Laboratuvar repo içindeki `build\lab\` altına taşındı (git'e girmez). Kullanıcının
> diskinde, özellikle `C:\` kökünde test klasörü açılmaz; test yolları kullanıcının "Son
> kullanılanlar" listesine yazılmaz (pencere testlerinde ayrı bir `recent` dosyası kullan).
```
build\lab\
├─ iso\         Win11_25H2_Turkish_x64_v2 çıkarılmış içerik (salt okunur referans)
├─ golden\      install.wim tek index export'ları (ör. pro.wim) — testler buradan KOPYA alır
├─ work\        test başına geçici kopyalar (testten sonra silinir)
├─ mount\       mount noktaları (0,1,2...)
└─ out\         üretilen ISO'lar
```
- Kaynak ISO: `C:\Users\shades\Downloads\Win11_25H2_Turkish_x64_v2.iso` — **asla değiştirilmez**.
- `tools/lab_setup.ps1` laboratuvarı hazırlar (Faz 2.6). Golden WIM tek index (Pro) → testler hızlı.
- Her integration testi: golden → work kopyası → mount → işlem → doğrula → **discard** unmount → kopyayı sil. Test sonunda `wlcli mounts` boş olmalı.
- Disk: tam ISO çıkarımı + golden + work ≈ 25–30 GB. (Şu an C: üzerinde ~330 GB boş.)

## Görsel doğrulama
1. `WinLove.exe --render=out.png --theme=dark --scale=1.5 [--lang=en] [--hover=close] [--maximized]`: pencere açmadan tek kare. Hata olursa diyalog açmaz, konsola yazar ve 1 ile çıkar.
2. `python tools/compare_design.py 01-welcome-source --theme=dark [--crop=x,y,w,h] [--zoom=2]`: tasarım SVG'si ve WinLove render'ı alt alta + fark satırı → `build/visual/<ekran>-<tema>.png`.
3. `python tools/capture_window.py out.png [--maximized] [-- <uygulama argümanları>]`: gerçek HWND'yi açar, **yalnızca kendi penceresini** `PrintWindow` ile yakalar (üstünde başka pencere olsa bile), `WM_CLOSE` ile kapatır ve çıkış kodunu yazar. Özel çerçeve, DPI ve ekranı kaplama davranışı bununla doğrulanır.
4. **Yasak:** tüm ekranı yakalamak (`ImageGrab`, ekran görüntüsü). Kullanıcının diğer pencereleri görüntüye girer (D-013).
5. Farklar sayfa spec'ine not edilir; bilinçli sapmalar `DECISIONS.md`'ye.
6. Render testleri (`tests/ui/RenderTests.cpp`) piksel düzeyinde kontrol eder: token rengi, 1px çizginin %150'de tam bir fiziksel satır olması.

## Sayfa testleri
- `tests/app/SourceTests.cpp`: biçimleme, son kullanılanlar kalıcılığı, sürükleme kuralları ve **Shell → motor → UI akışı** (gerçek ISO, `postToUi` sahte kuyruğu + `engine().drain()` ile deterministik). Yeni sayfalar aynı kalıbı izler.
- Render fixture: `tests/integration/fixtures/recent-sample.json` (`--recent-file=`), kullanıcının gerçek geçmişinden bağımsız görüntüler için.

## Test verisi sabitleri
Integration testleri beklenen değerleri (index sayısı, sürüm adları, build no) `tests/integration/fixtures/win11_25h2_tr.json`'dan okur. ISO değişirse yalnızca bu dosya güncellenir.
