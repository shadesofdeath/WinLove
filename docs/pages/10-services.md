# P10 — Servisler

> Durum: 🟨 bitti, kullanıcı testi bekliyor (2026-09-28). Tasarım: 09.

## 1. Amaç
Bağlı imajdaki Win32 servislerinin başlangıç türünü değiştirmek (kuyruk → P05 Uygula).

## 2. Motor
- `core::OfflineHive` (yeni, ENGINE.md "OfflineRegistry"): `RegLoadKey(HKLM\WinLove_<dosya>_<pid>_<n>)`,
  SeBackup/SeRestore açılır; yıkıcıda `RegFlushKey` + `RegUnLoadKey` (10 × 100 ms deneme). `RegKey` RAII —
  hive boşaltılmadan önce tüm alt anahtarlar kapanmalı. Kalıntı hive'ları unmount öncesi
  `MountHealth::unloadHivesUnder` temizler.
- `core::readServices(mount)`: SYSTEM hive → `Select\Current` → `ControlSetNNN\Services`; yalnız Type & 0x30
  (sürücüler hariç). Start + DelayedAutostart → Önyükleme / Sistem / Otomatik / Gecikmeli / El ile / Devre dışı.
  DisplayName/Description `@%SystemRoot%\…dll,-id` biçimindeyse imaj içindeki dosyaya çevrilip
  `SHLoadIndirectString` ile çözülür (MUI, ana makinenin dil tercihiyle aranır; çözülemezse anahtar adı).
- `core::setServiceStart`: Start (+ Gecikmeli için DelayedAutostart=1, diğerlerinde 0).
- Applier: `SetServiceStart` (value = `startTypeKey`) → hive yükle, yaz, boşalt. Motor iş parçacığında (DISM ile yarışmaz).
- CLI: `wlcli services <mount> [--set=Ad=auto|autoDelayed|manual|disabled]`.
- Unit test: `tests/app/ServiceTests.cpp` (anahtarlar, yol çevirme, bağımlılar, kuyruk eşlemesi, risk).

## 3. Ekran
- Başlık: Varsayılanlara dön (yalnız servis işlemlerini kuyruktan çıkarır).
- Araç çubuğu: arama (Ctrl+F, "/"), Başlangıç süzgeci (hedef başlangıç türüne göre), Yalnızca değişenler;
  sağda "{n} öğe · {n} değişiklik kuyrukta".
- Tablo: Servis · Ad (mono) · Varsayılan (imajdaki) · Yeni başlangıç (satır içi menü; değişenler vurgu kenarlı) · Risk.
  Enter satırın menüsünü açar. İmajdaki değeri seçmek işlemi kuyruktan çıkarır.
- Devre dışı bırakılan servise bağımlı (dolaylı dahil) ve hâlâ açık servisler varsa uyarı toast'ı.
- Risk: `resources/catalog/services.json` (gömülü, IDR_CATALOG_SERVICES); bilinmeyen = Orta; Önyükleme/Sistem = Yüksek;
  açma yönündeki değişiklik en fazla Orta. Render: `--demo-services`.

## 4. Sınırlar
- Katalog notları (neden riskli) henüz ekranda gösterilmiyor (ipucu/inspector sonra).
- Kullanıcı servis şablonları (Type 0x50/0x60) listede; örnek başına (_xxxx) ayarlar yok (çevrimdışı imajda örnek yok).
- Kurcalama korumalı servisler (WinDefend vb.) ilk açılışta Windows tarafından geri alınabilir.

- **Uyumluluk (D-082):** açık bir korumanın servisi (Windows Update → wuauserv, Yazdırma → Spooler …) "Devre dışı"
  yapılamaz: seçim kutusunda kilit, seçilirse uyarı ("Korunuyor: …", Bileşenler › Uyumluluk'tan değişir). El ile /
  otomatik serbest. Koruma sonradan açılırsa kuyruktaki devre dışı bırakma çıkar.

## 5. Kabul
- [ ] Bağlı imajda liste gelir, adlar Türkçe çözülür.
- [ ] DiagTrack → Devre dışı → Uygula → `wlcli services <mount>` (veya yeniden bağla) "disabled" gösterir.
- [ ] Uygulama sonrası unmount "hive açık" hatası vermez.
