# P11 — Kayıt Defteri

> Durum: 🟨 bitti, kullanıcı testi bekliyor (2026-09-28). Tasarım: 08.

## 1. Amaç
Hazır tweak kategorilerini ve kullanıcının .reg dosyalarını bağlı imajın çevrimdışı hive'larına yazmak
(kuyruk → P05 Uygula).

## 2. Motor (`core/image/RegistryEdit`)
- `RegistryWrite` canlı sistem yoluyla ifade edilir; `mapOfflineKey` imaj dosyasına çevirir:
  HKLM\SOFTWARE → config\SOFTWARE, HKLM\SYSTEM → config\SYSTEM (CurrentControlSet → Select\Current),
  HKCR → SOFTWARE\Classes, HKCU\Software\Classes → Default\…\UsrClass.dat, HKCU → Users\Default\NTUSER.DAT
  (yeni hesaplara geçer), HKU\.DEFAULT ve HKU\S-1-5-18 → config\DEFAULT. HKCC ve HKU\S-1-5-19/-20 için imajda
  hive yok: yalnız kurulum sonrası içe aktarılır. SAM/SECURITY/HARDWARE/başka SID'ler reddedilir.
- `.reg` ayrıştırıcı: "Windows Registry Editor Version 5.00" / REGEDIT4, UTF-16/UTF-8/ANSI, `[-anahtar]`,
  `"ad"=-`, `@=`, `dword:`, `hex:`, `hex(N):`, satır devamı `\`, değersiz `[anahtar]` (anahtar oluşturur).
  Hatalar satır numarasıyla.
- ChangeSet: `SetRegistryValue` target = `<anahtar>::<ad>`, value = .reg sözdizimi (`[-]` anahtar sil, `-` değer sil,
  `[+]` anahtar oluştur) → presetler okunabilir kalır. Anahtar işlemlerinin hedefi ayrıdır (`<anahtar>\\::` sil,
  `<anahtar>\::` oluştur): "anahtarı sil, sonra varsayılan değerini yaz" kalıbında ikisi de kuyrukta kalır.
- `OfflineRegistry`: Uygula boyunca hive'lar bir kez yüklenir (servis işlemleri de aynı oturumu kullanır — aynı hive
  iki kez yüklenemez), sonda boşaltılır. ACL'li anahtarlarda `REG_OPTION_BACKUP_RESTORE` (SeRestore) ile yazar.
- **İlk oturumda yeniden uygulama (D-026):** `SetRegistryFirstLogon` — çevrimdışı yazım + `Windows\Setup\Scripts\WinLove\`
  altında setupcomplete.reg (SetupComplete.cmd) / firstlogon-user.reg (Default RunOnce). Katalogda `firstLogon`
  işaretli tweak'ler **ve içe aktarılan .reg dosyalarının tüm değerleri** bu yoldan gider (NTLite ile aynı yol).
  `DeferredRegistry`: her değer dosyaya eklenir (son girdi kazanır), Uygula sonunda dosya sıkıştırılır.
- CLI: `wlcli reg <dosya.reg> [<mount>] [--first-logon]`. Testler: `tests/core/RegistryTests.cpp`,
  `tests/app/RegistryImportTests.cpp`.

## 3. Ekran
- Başlık: .reg içe aktar… (çoklu seçim; sayfadayken sürükle-bırak). İçe aktarılan dosya "Özel .reg" altında,
  tüm değerleri kuyrukta; onay kutusu ile aç/kapat, ✕ ile kaldır. İmajda karşılığı olmayan değerler atlanır ("n atlandı").
  Satırda "kurulumdan sonra yeniden uygulanır" notu (her değer kurulum sonunda yeniden içe aktarılır).
- 2 sütun kategori kartı: ad, açıklama, "{s} / {n} seçili", seçili kartta vurgu çizgisi; ok tuşlarıyla gezinme.
- Tablo: Tweak (onay kutusu; Space/Enter) · Anahtar (ilk anahtar, "+n") · Kapsam (Sistem / Kullanıcı; "İlk oturumda" etiketi).
- Katalog: `resources/catalog/tweaks.json` (gömülü IDR_CATALOG_TWEAKS), 5 kategori, 34 tweak. Nav rozeti = seçili tweak + .reg.
- Render: `--demo-registry`.

## 4. Sınırlar
- İmajdaki mevcut değerler okunur (D-045): imajda olan tweak işaretli ve "imajda" etiketli görünür; işareti
  kaldırmak değerleri silen işlemleri kuyruğa koyar ("imajdan geri alınacak"). Yalnız silmelerden oluşan tweak
  imajdan tanınmaz. İçe aktarılan .reg dosyaları imajla karşılaştırılmaz.
- HKCU yalnız Default profile (yeni hesaplar); kurulumda oluşturulan ilk hesap da buradan türetilir.
- OEM anahtarlı sürümlerde SetupComplete.cmd çalışmaz (D-026).
- Kurulum sonrası içe aktarma normal yetkiyle çalışır: TrustedInstaller'a ait HKLM anahtarları ve (yükseltilmemiş
  oturumda) `HKCU\Software\Policies` yeniden yazılamaz — bu girdiler atlanır, kalanı uygulanır; çevrimdışı yazılan
  değer zaten imajdadır.
- Bir .reg dosyasında aynı değer iki kez geçerse yalnız sonuncusu (kendi sırasında) kuyruğa girer — içe aktarmanın
  sonucu da budur.
- Anahtar adında `::` geçen girdiler desteklenmez (ChangeSet hedef ayırıcısı).

## 5. Kabul
- [ ] Tweak işaretle → Uygula → `wlcli reg` ile / yeniden bağlayınca değerler hive'da.
- [ ] "İlk oturumda" tweak'i → imajda `Windows\Setup\Scripts\WinLove\*.reg` ve SetupComplete.cmd satırı; kurulumdan sonra değer kalıcı.
- [ ] .reg sürükle-bırak → Özel .reg'de görünür, hatalı dosyada satır numaralı hata.
