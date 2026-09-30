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

## Motor denemeleri (yönetici PowerShell, VM'siz, kaydetmeden)
| Betik | Ne yapar | Çıktı |
|---|---|---|
| `tools\lab_setup.ps1` | `build\lab` klasörlerini kurar, install.wim'i ISO'dan kopyalar (yönetici gerekmez) | — |
| `tools\dism_smoke.ps1` | Salt okunur mount, özellik / paket / capability listeleri, discard | `build\lab\out\dism-smoke.json` |
| `tools\lab_components.ps1 [-Cleanup]` | Kopya imajda OneDrive (gizli CBS paketi) ve Edge'i `wlcli component --remove` ile kaldırır, doğrular, discard. `-Cleanup`: depo temizliği de (5–20 dk) | `build\lab\out\components-test.log` |

Hepsi `wlcli` üzerinden çalışır: bir adım başarısızsa aynı komut elle yinelenebilir (`--verbose` motor logunu da basar).

## VM kabul testi (tek kurulumda en riskli her şey)
Hazırlık: `dist\WinLove.exe` (yönetici) → test ISO'sunu aç → Pro'yu bağla.

**İmajı hazırla** (her satır bir sayfa; hepsi aynı kuyruğa girer):
1. Bileşenler: birkaç uygulama + **OneDrive kurulumu** + **Microsoft Edge** (+ istersen WinRE) + **Bileşen deposu temizliği**.
2. Özellikler: bir özelliği kapat, bir FoD'u (ör. Adım Kaydedici) kaldır.
3. Servisler: DiagTrack → Devre dışı.
4. Ayarlar / Tweaks: "Önerilenleri uygula".
5. Kayıt Defteri: bir "İlk oturumda" tweak'i + küçük bir `.reg` içe aktar (HKLM ve HKCU değeri olsun).
6. Kurulum Sonrası: bir winget adımı (ör. `Mozilla.Firefox` — Edge kaldırıldıysa tarayıcı), bir komut
   (`cmd /c echo ok > C:\winlove-postsetup.txt`), bir dosya kopyalama.
7. Katılımsız Kurulum: dil / klavye / saat dilimi, yerel hesap, "Microsoft hesabını atla", gizlilik sorularını atla;
   "ISO'ya ekle" açık. Disk düzenini ilk denemede **Sor** bırak.
8. Uygula → log'u kaydet (Tamamlandı ekranı) → ISO Oluştur (LZX yeniden paketle, SHA-256).

**Uygula ekranında bak:** depo temizliği güncellemelerden sonra mı; `[cbs] … package(s) unlocked` / `removed …OneDrive…` satırları;
hata / uyarı sayaçları; kazanç.

**VM (Hyper-V / VMware, UEFI + TPM'siz deneme için LabConfig atlamaları açık):**
| Kontrol | Beklenen | Olmazsa bakılacak yer |
|---|---|---|
| ISO açılıyor (UEFI; mümkünse bir de BIOS) | Kurulum başlar | P06, `boot` seçeneği |
| Kurulum soruları | Dil / klavye / hesap / gizlilik sorulmaz | `X:\Windows\Panther\setupact.log`, `autounattend.xml` kökte mi |
| İlk masaüstü | Yerel hesap açık, internet hesabı istenmedi | BypassNRO |
| `C:\Windows\Setup\Scripts\SetupComplete.cmd` çalıştı mı | `C:\ProgramData\WinLove\postsetup-machine.log` var (kurulum sonrası adımı varsa); HKLM değerleri yerinde | OEM anahtarıyla etkinleştirilmiş sürümlerde Windows bu betiği atlar |
| HKLM tweak'leri | `reg query` ile değerler yerinde | `C:\Windows\Setup\Scripts\WinLove\setupcomplete.reg` |
| HKCU tweak'leri (ilk oturum) | Değerler yerinde; ikinci bir kullanıcı açınca onda da | Default profil `RunOnce`, `firstlogon-user.reg` |
| Kurulum sonrası adımlar | `C:\winlove-postsetup.txt` var; winget uygulaması kuruldu (ağ gerekir) | `C:\ProgramData\WinLove\postsetup-user.log`; "WinLove Post-Setup" görevi (çalışınca kendini siler) |
| Kaldırılan uygulamalar | Başlat'ta yok | — |
| OneDrive | Kurulmadı, `System32\OneDriveSetup.exe` yok | — |
| Edge | `Program Files (x86)\Microsoft\Edge` yok; Ayarlar → Uygulamalar'da görünmüyor | Windows Update sonrası geri geldi mi (not et) |
| Servis | `sc qc DiagTrack` → DISABLED | — |
| Sistem sağlığı | `sfc /scannow` ve `dism /online /cleanup-image /scanhealth` temiz; Windows Update bir toplu güncelleme kurabiliyor | Paket sökülen bileşenler (OneDrive) güncellemeyi bozuyorsa D-031'e not |
| WinRE kaldırıldıysa | `reagentc /info` → Disabled | — |

Sorun çıkarsa `C:\Windows\Panther\*.log`, `C:\Windows\Logs\CBS\CBS.log`, `C:\ProgramData\WinLove\*.log`,
`C:\Windows\Setup\Scripts\WinLove\` ve WinLove'un kaydettiği uygulama logunu sakla.
