# Test Stratejisi

## Katmanlar
| Tür | Nerede | Admin | Ne zaman |
|---|---|---|---|
| Unit (core) | `tests/core` — doctest | Hayır | Her build (`./build.ps1 -Test`) |
| Unit (ui) | `tests/ui` — layout hesapları, hit-test, text ölçüm, tema | Hayır | Her build |
| Render | `WinLove.exe --render=<png> --page=<anahtar>` ile tek kare + `tools/compare_design.py` (tasarım \| render \| fark); otomatik piksel eşiği yok, gözle onaylanır | Hayır | Her UI değişikliğinde |
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
1. `WinLove.exe --render=out.png --theme=dark --scale=1.5 [--lang=en] [--page=<anahtar>] [--hover-at=x,y] [--click-at=x,y] [--maximized]`: pencere açmadan tek kare (bayrakların tam listesi: `src/app/App.h`). Hata olursa diyalog açmaz, konsola yazar ve 1 ile çıkar.
2. `python tools/compare_design.py 01-welcome-source --theme=dark [--crop=x,y,w,h] [--zoom=2]`: tasarım SVG'si ve WinLove render'ı alt alta + fark satırı → `build/visual/<ekran>-<tema>.png`.
3. `python tools/capture_window.py out.png [--maximized] [-- <uygulama argümanları>]`: gerçek HWND'yi açar, **yalnızca kendi penceresini** `PrintWindow` ile yakalar (üstünde başka pencere olsa bile), `WM_CLOSE` ile kapatır ve çıkış kodunu yazar. Özel çerçeve, DPI ve ekranı kaplama davranışı bununla doğrulanır.
4. `python tools/gui.py …`: **uygulamayı içinden sınamak** (kullanıcı yokken de). İlk komut yönetici bir sunucu başlatır
   (bu makinede UAC sormadan yükseltir); tıklama / tuş / metin pencereye ileti olarak gider (kullanıcının faresi ve klavyesi
   kullanılmaz), görüntü yalnız WinLove penceresinden (`PrintWindow`, istemci alanı, DIP). Uygulama `build\lab\gui\bin`'deki
   kopyadan, `--profile=build\lab\gui\profile` ile çalışır: son kullanılanlar / ayarlar / yanıtlar kullanıcınınkilere
   dokunmaz, çalışma klasörü `build\lab\gui\work`. Komutlar: `start [--dist] [-- <kaynak>]`, `shot <png> [--crop=x,y,w,h]
   [--hires]`, `click X Y [--right|--double]`, `wheel`, `key enter ctrl+a…`, `type`, `dialog <yol>` (Windows'un aç / kaydet /
   klasör seçicisini doldurur), `windows`, `mark` + `wait-log <regex>` (oturumun günlüğünde bir satırı bekler), `log`,
   `exec -- <komut>` (yönetici konsol komutu), `close | kill | quit`. Koordinatlar `shot`'ın piksel = DIP'i.
   Sunucu komutları **sırayla** işler: uzun bir `exec` (ör. 45 dk'lık `lab_vm.ps1`) sürerken sonraki komut bekler. Uzun
   laboratuvarı `Start-Process -Verb RunAs` ile ayrı başlat (bu makinede UAC sormaz); günlüğü `build\lab\out\vm-<etiket>-test.log`.
4b. Çökme: uygulama yakalanmayan istisnada `%LOCALAPPDATA%\WinLove\logs\crash-<zaman>.txt` (sembollü yığın) + `.dmp` yazar;
   render'lar dahil (çıkış kodu 0xC0000005 görülürse önce bu dosyaya bak).
4c. `lab_vm.ps1 -Diag`: kurulan sistemde ilk oturumdan sonra `-DiagMinutes` boyunca kayıt tutar, çıktı
   `build\lab\out\vm-<etiket>\diag`: `timeline.txt`, `WinLove\` (Kurulum Sonrası / Programlar günlükleri), `smoke.txt`
   (disk kullanımı, WinSxS klasör sayısı, sistem programları ve kurulan programlar açılıyor mu, SideBySide olayları,
   Defender imzaları). Laboratuvar bitince sanal diski siler: günlük yalnız `-Diag` ile kalır.
4d. Kurulum ortamı (D-080): `tools\lab_winre.ps1 [-Keep]` (WinRE güncellemesi), `tools\lab_media.ps1 [-ForVm]` (boot.wim +
   kurulum dosyaları + ISO içeriği), `lab_vm.ps1 -IsoArgs @('--setup-du=…','--boot-files=…')` (ISO'yu o yoldan kurar).
   Kurulum WinPE'de başarısızsa `tools\lab_setup_logs.ps1 -Tag x -SetupFolder <ortam> -InstallWim <wim>`: Setup'ı bir
   betiğe sarar, günlükleri (`$WINDOWS.~BT` her sürücüde, `X:\Windows\Panther`) `lab_vm.ps1 -LogDisk` diskine yazar →
   `build\lab\out\vm-setuplogs-<Tag>\diag`.
4f. Karşılama ekranı (D-084): `wlcli unattend <xml> --welcome > a.xml` (genel ürün anahtarı yalnız sürüm bilinince yazılır:
   laboratuvarda anahtarı elle koy), `wlcli welcome c.json --strings=resources\strings\tr.json --auto=<yanıtlar.json>`
   (`{"name","password","computer","theme","accent","privacy","check":true,"pause":9}`), sonra `lab_vm.ps1 -Changes c.json
   -AnswerFile a.xml -LogDisk`. `check` için `ProgramData\WinLove\Oobe\oobe-check.ps1` değişiklik kümesine eklenir: yeni
   hesabın ilk oturumunda sonucu WLDIAG diskine yazıp kapatır → `out\vm-<Tag>\diag\oobe\check.txt`, `oobe.log`.
   Önizleme: Katılımsız Kurulum › Önizle (pencere, hiçbir şey yapmaz).
4e. Modlu Windows (D-081): `wlcli host-check` (çıkış 3: sorun), `wlcli wimgapi <wim|esd>` (seçilen kopya, katı kaynaklar).
   Yedeği kanıtlamak: `wlcli export <esd> 2 <out.wim> --wimgapi=C:\Windows\System32\version.dll --verbose` (bozuk kopya
   → ADK'nınki); `wlcli mount … --dism=adk` (yönetici) ADK'nın DISM'iyle. Arayüz: `--render … --page=source --demo-host-dism`.
5. **Yasak:** tüm ekranı yakalamak (`ImageGrab`, ekran görüntüsü). Kullanıcının diğer pencereleri görüntüye girer (D-013).
6. Farklar sayfa spec'ine not edilir; bilinçli sapmalar `DECISIONS.md`'ye.
7. Render testleri (`tests/ui/RenderTests.cpp`) piksel düzeyinde kontrol eder: token rengi, 1px çizginin %150'de tam bir fiziksel satır olması.

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
| `tools\lab_editions.ps1 [-Keep "Windows 11 Pro"]` | Lab WIM'inin kopyasında sürüm siler (`wlcli delete-index`): reddedilen istekler, tek sürüm, "yalnız bunu tut"; 7-Zip varsa akış testi. Kopyayı siler. **Yönetici gerekmez**, ~14 GB boş alan, ~3 dk | konsol (PASS / FAIL) |
| `tools\lab_appx.ps1` | **Yönetici.** Kopya imajda: bir uygulamayı DISM ile kaldırıp tarifimizin adını verdiği her şeyin gittiğini doğrular (tarif = DISM'in yaptığı); bir uygulamayı yerel kaldırır; DISM'in reddettiği SecHealthUI ve DesktopAppInstaller'ı DISM → yerel yoluyla kaldırır; DISM hâlâ listeliyor mu bakar; discard. ~5 dk | `build\lab\out\appx-test.log` |
| `tools\lab_boot.ps1 [-Driver <inf>]` | **Yönetici.** Test ISO'sundan boot.wim'i çıkarır, `wlcli boot-patch --bypass=all` ile LabConfig yazar (isteğe bağlı sürücü), commit; yeniden bağlayıp beş değeri okur, index sayısı / önyükleme index'i / akış doğrulaması. ~2 dk (2026-09-30: 14 / 14, sürücüsüz) | `build\lab\out\boot-test.log` |
| `tools\lab_legacy_setup.ps1 [-Vm]` | **Yönetici.** Test ISO'sunun boot.wim'ine `wlcli boot-patch --legacy-setup`, geri bağlayıp `Setup\CmdLine`'ı okur, akış doğrulaması (~1 dk). `-Vm` (VMware, ~1 sa): Winre.wim'i silinmiş sürümle iki ISO; özgün boot.wim (yeni kurulum) hata vermeli, yamalı (önceki kurulum) masaüstüne kurmalı; VNC kareleri `out\vm-legacy-new|legacy` | `build\lab\out\legacy-setup-test.log` |
| `tools\lab_edition.ps1 [-Target Professional]` | **Yönetici.** Kopya imajda Home'u `wlcli edition --set` (`dism /Set-Edition`) ile Pro'ya çevirir, commit eder, WIM'in yeni sürüm kimliğini okur, adını değiştirir, `wlcli verify` ile her akışı doğrular. Kopyayı siler. ~10 dk | `build\lab\out\edition-test.log` |
| `tools\lab_usb.ps1 [-Gpt] [-Keep]` | **Yönetici.** Önce `wlcli extract-all <test.iso> build\lab\setup` (yönetici gerekmez). `build\lab\usb\stick.vhdx` (16 GB, genişleyen) oluşturup bağlar, `wlcli usb-write --allow-virtual --yes` ile yazar; bölüm stili / etkin bölüm, FAT32, BOOTMGR önyükleme kodu, dosyalar, .swm parçaları (DISM okuyor mu), 4 GB üstü dosya yok. VHDX'i ayırır ve siler (`-Keep` bırakır: VM'de önyükleme için). ~5 dk | `build\lab\out\usb-test.log` |
| `tools\lab_features.ps1 [-LanguageFolder <medya>]` | **Yönetici.** Lab WIM'inden iki sürümlük kopya (Home + Pro), bağlar: imajdaki sürücüler (listele, bu bilgisayardan birini ekle, `DismRemoveDriver` ile kaldır), `pnputil` dışa aktarma, `/Get-Intl` + saat dilimi / klavye yazıp geri okuma, isteğe bağlı dil paketi, varsayılan ilişkilendirme içe aktarma, Windows Terminal provision (`build\lab\appx`), görev + hosts + dosya kuyruğu commit ve `--also=2` ile ikinci sürüme; ikinci sürümü salt okunur bağlayıp betik / hosts / dosyaya bakar. Her şeyi siler. ~15 dk, ~25 GB (2026-10-01: ALL PASSED, dil paketi atlandı) | `build\lab\out\features-test.log` |
| `tools\lab_branding.ps1 [-SkipScanHealth]` | **Yönetici.** Lab WIM'inden Pro kopyası: varsayılan görseller (masaüstü, kilit ekranı, hesap resmi, OEM logo) değiştirilir — boyut aynı, yeni dosya (tek bağlantı), WinSxS eşi değişmez; imajda olmayan bir yazı tipi, OEM değerleri, Wi-Fi adımı, WinRE kaldırma; `reg-check` ile kayıt değerleri; DISM `/ScanHealth`. Discard + siler. ~10 dk (2026-10-01: ALL PASSED) | `build\lab\out\branding-test.log` |
| `tools\lab_imagetools.ps1 [-Iso <test.iso>]` | Yönetici (yalnız yakalama için; gerisi gerekmez). ISO SHA-256 (eşleşme / büyük harf + önek / eşleşmeme exit 3); lab WIM'inden iki sürümlük kopyada sürüm çoğaltma (akışlar ortak), XPRESS → ESD → LZX yeniden sıkıştırma, SWM'e bölme ve geri birleştirme + verify, ISO'dan ve SWM'den sürüm ekleme + verify, klasör yakalama (ikinci yakalama yeni sürüm) + verify; siler. ~25 dk (2026-10-01: ALL PASSED) | `build\lab\out\imagetools-test.log` |
| `tools\lab_scan_components.ps1 [-Index 4]` | **Yönetici.** Lab WIM'ini salt okunur bağlar; CBS paket listesi, özellikler, servisler, hive kopyaları, .mum dosyaları, klasör ağacı / WinSxS / büyük dosya / sürücü deposu / yazı tipi boyutları (`tools\scan_image_tree.py`); discard. Sonra `python tools\analyze_cbs.py <çıktı> [--json] [--files] [--groups]`: paket başına tek sahipli bayt (D-059). ~3 dk | `build\lab\out\scan\index<N>\` |
| `tools\lab_cbs_removal.ps1 [-Recipes <klasör>] [-SkipScanHealth]` | **Yönetici.** Lab WIM'inden tek sürüm kopyası; katalogdaki paketli her girdi (ya da klasördeki tarifler) `wlcli component --remove` ile tek tek kaldırılır, her paket ailesinin CBS listesinden gittiği denetlenir; DISM `/ScanHealth`, commit, yeni export ile kazanç, verify; siler. ~25 dk (2026-10-02: ALL PASSED, 30 girdi, −844 MB) | `build\lab\out\cbs-removal-test.log` |
| `tools\lab_deep_removal.ps1 [-Lcu <msu> [-LcuFirst]]` | **Yönetici.** Lab WIM'inden tek sürüm kopyası; katalogdaki her derin girdi (driverClasses) `wlcli component --remove` ile kaldırılır, sonra hiç sürücü kalmadığı, WinSxS / DriverStore'da modem izi olmadığı, `/ScanHealth` temizliği; `-Lcu` ile bir toplu güncelleme (sonra ya da `-LcuFirst` ile önce) + ScanHealth; commit, export, verify; siler. ~10 dk, `-Lcu` ile ~25 dk (2026-10-02: önce LCU → ALL PASSED; sonra LCU → 0x80070002, D-060) | `build\lab\out\deep-removal-test.log` |
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
