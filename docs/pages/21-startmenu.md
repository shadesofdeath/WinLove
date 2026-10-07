# P21 — Başlat menüsü

**Durum:** 🟨 geliştirme bitti, VM'de kanıtlandı (Pro), kullanıcı testi bekliyor (2026-10-05) · **Tasarım:** handoff'ta
karşılığı yok — mevcut sayfaların dili; sağdaki önizleme Windows 11 Başlat'ının "Sabitlenenler" ızgarası (6 sütun). Karar: D-069.

## 1. Amaç
Windows 11'in Başlat menüsünü imajda özelleştirmek: Microsoft'un sabitlemelerini ve reklam yer tutucularını (Spotify,
WhatsApp, Instagram …) kaldırmak, kullanıcının seçtiği uygulamaları kendi sırasıyla sabitlemek, öneri / reklam ayarlarını
aynı yerden yönetmek. Windows 10'un `LayoutModification.xml` yöntemi Windows 11'de çalışmıyor.

## 2. Ekran
- Sekmeler: **Sabitlenenler** · **Başlat ayarları** (Ayarlar / Tweaks kataloğunun "Başlat ve görev çubuğu" sekmesi, aynı
  form, aynı kuyruk).
- Sabitlenenler: mod (radyo) **Windows varsayılanı / Boş / Kendi listem**, **Kullanıcı sonra değiştirebilsin**
  (`applyOnce`; kapalıysa liste her oturum açılışında geri gelir), modun açıklaması.
- Kendi listem: solda imajın uygulamaları (arama; simgeler imajdan: paket logoları, kısayolların hedef simgeleri; ✓ =
  sabit), çift tık / Enter / **Ekle**; sağda Başlat önizlemesi = sabitleme listesi (6 sütun, sıra = Başlat'taki sıra):
  seç, Delete kaldırır, Ctrl+←/→ taşır; **Öne al**, **Geriye al**, **Kaldır**, **Temizle**.
- Nav rozeti: plan kuyruktaysa 1.

- **Görev çubuğu (D-083):** aynı düzen — mod Windows varsayılanı / Boş / Kendi listem, **Kullanıcı kaldırabilsin**
  (`PinGeneration`); soldaki listenin başında Dosya Gezgini; sağdaki ızgara soldan sağa sıra. Windows 10 sürümünde uyarı
  (ilke orada Başlat kutucuklarını kilitler). Motor: `core::taskbarPinsOperations` (Başlangıç Düzeni ilkesi +
  `ProgramData\WinLove\TaskbarLayout.xml`), geri okuma `taskbarPlanFromOperations`. Render: `--demo-startmenu=taskbar`.

## 3. Veri
| Veri | Kaynak | Ne zaman |
|---|---|---|
| Sabitlenebilir uygulamalar | `core::listStartApps`: `Program Files\WindowsApps\*\AppxManifest.xml` (aile adı klasör adından, en yeni sürüm; çerçeve / kaynak paketi / `AppListEntry="none"` elenir), `Windows\SystemApps\*`, `Windows\ImmersiveControlPanel` (Ayarlar), ProgramData ve Default profildeki Başlat kısayolları, Edge (`MSEdge`) | sayfa açılınca, okuyucu iş parçacığında, bağlama başına |
| Plan | kuyruk: `ProgramData\WinLove\StartPins.json` WriteFile işleminden geri okunur | — |

## 4. Motor (D-069)
Plan → işlemler (`core::startPinsOperations`):
- `HKLM\SOFTWARE\Microsoft\PolicyManager\current\device\Start\ConfigureStartPins` = JSON (MDM ilkesi),
- `HKLM\SOFTWARE\Policies\Microsoft\Windows\Explorer\ConfigureStartPins` = 1 + `ConfigureStartPinsJSON` =
  `%ProgramData%\WinLove\StartPins.json` (yeni build'lerin Grup İlkesi biçimi) + o dosya,
- her modda boş Başlat durumu `Users\Default\…\StartMenuExperienceHost_cw5n1h2txyewy\LocalState\start2.bin`
  (Win11Debloat, MIT, `WriteFile base64:`): hiçbir sürümde Microsoft sabitlemesi / reklam yer tutucusu gelmez,
- "Boş": Windows 10 için kutucuksuz `LayoutModification.xml`.
- Özel liste 26100/26200 UBR ≥ 9457'de uygulanır (ölçüldü); daha eskiyse sayfa uyarır.
JSON Microsoft'un biçimi: `{"applyOnce":true,"pinnedList":[{"packagedAppId":…},{"desktopAppLink":"%APPDATA%\…lnk"},{"desktopAppId":"MSEdge"}]}`.
CLI: `wlcli start-apps <mount>`.

## 5. Kabul
- [x] VM, Pro 26200.8037: boş liste → Sabitlenenler boş, reklam yok (`tools\lab_vm.ps1`, deney 1).
- [x] VM, Pro + KB5129195 (yeni Başlat): özel liste birebir, sırasıyla; Önerilenler gizli (deney 2).
- [x] VM, Home + KB5129195: özel liste birebir (deney 4, 8); Home 26200.8037: ilke yok sayılıyor, boş şablonla Başlat boş ve
      reklamsız (deney 3, 5, 7). OEM `LayoutModification.json` Microsoft'unkilerin yanına ekliyor — kullanılmadı (deney 6).
- [x] Pro 26200.8037: özel liste uygulanmıyor (deney 9–11) → sayfa uyarır, Başlat boş açılır.
- [ ] Uygulamanın içinden: liste kur, Uygula, VM'de kurulum.
- [ ] `applyOnce`: kullanıcı bir sabitlemeyi kaldırır, oturumu kapatıp açar → geri gelmez.
