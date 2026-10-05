# P11 — Kayıt Defteri

> Durum: 🟨 bitti, kullanıcı testi bekliyor (2026-09-28; D-067 ile yalnız özel kayıtlar, 2026-10-05). Tasarım: 08.

## 1. Amaç
Kullanıcının kendi kayıt defteri girdilerini — elle yazdığı değerleri ve .reg dosyalarını — bağlı imajın çevrimdışı
hive'larına yazmak (kuyruk → P05 Uygula). Hazır tweak'ler burada değil, **Ayarlar / Tweaks** (P12) sayfasında (D-067).

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
  altında setupcomplete.reg (SetupComplete.cmd) / firstlogon-user.reg (Default RunOnce). Ayarlar kataloğunda `firstLogon`
  işaretli ayarlar, **içe aktarılan .reg dosyalarının tüm değerleri** ve öyle işaretlenmiş elle eklenen değerler bu yoldan gider (NTLite ile aynı yol).
  `DeferredRegistry`: her değer dosyaya eklenir (son girdi kazanır), Uygula sonunda dosya sıkıştırılır.
- CLI: `wlcli reg <dosya.reg> [<mount>] [--first-logon]`. Testler: `tests/core/RegistryTests.cpp`,
  `tests/app/RegistryImportTests.cpp`.

## 3. Ekran
- Başlık eylemleri: **.reg içe aktar…** (çoklu seçim; sayfadayken sürükle-bırak) ve **Değer ekle…**.
- "ÖZEL KAYITLAR" başlığı, sağda "{s} / {n} açık". Tek tablo: Girdi (onay kutusu + ad) · Anahtar · Değer · Kapsam
  (Sistem / Kullanıcı rozeti, "Kurulumdan sonra da" etiketi, satır sonunda ✕).
  - Elle eklenen değer: ad (boşsa "(Varsayılan)", anahtar silmede "(anahtarın tamamı)"), anahtar, `REG_DWORD  0x1` gibi değer.
  - .reg dosyası: dosya adı + "n değer · m atlandı", ilk anahtar ("+n"), değer sütununda değer sayısı.
- Onay kutusu / Boşluk: aç-kapa (işlemler kuyruktan çıkar, girdi listede kalır). Enter / çift tık: elle eklenen değeri
  düzenle (.reg dosyasında aç-kapa). Delete / ✕: kaldır.
- **Değer ekle / düzenle diyaloğu:** Anahtar (`HKLM\…`, `HKEY_LOCAL_MACHINE\…` de olur), Ad (boş = varsayılan değer),
  Tür (REG_SZ, REG_EXPAND_SZ, REG_MULTI_SZ, REG_DWORD, REG_QWORD, REG_BINARY, Değeri sil, Anahtarı sil), Veri (türe göre:
  metin; `;` ile satırlar, `;;` = `;`; ondalık ya da `0x` onaltılık; `01 0a ff` baytlar), "Kurulumdan sonra yeniden uygula"
  (varsayılan açık). Altta türün biçim ipucu ya da kırmızı hata ("anahtar HKLM\… ile başlamalı", "veri bu türe uymuyor",
  "bu anahtar imaja yazılamaz"); hata varken Ekle / Kaydet kapalı. Çeviri `core/image/RegistryInput` (gidiş-dönüş testli).
- Boş liste: "Henüz kayıt yok…" ve "Hazır tweak'ler Ayarlar / Tweaks sayfasında".
- Nav rozeti = açık girdi sayısı. Render: `--demo-registry`, `--demo-registry=dialog`.

## 4. Sınırlar
- İçe aktarılan .reg değerleri her zaman kurulumdan sonra da yazılır (D-026); elle eklenen değerde seçim kullanıcının.
- Girdiler imajla karşılaştırılmaz (D-045'in imaj okuması P12'de).
- HKCU yalnız Default profile (yeni hesaplar); kurulumda oluşturulan ilk hesap da buradan türetilir.
- OEM anahtarlı sürümlerde SetupComplete.cmd çalışmaz (D-026).
- Kurulum sonrası içe aktarma normal yetkiyle çalışır: TrustedInstaller'a ait HKLM anahtarları ve (yükseltilmemiş
  oturumda) `HKCU\Software\Policies` yeniden yazılamaz — bu girdiler atlanır, kalanı uygulanır; çevrimdışı yazılan
  değer zaten imajdadır.
- Bir .reg dosyasında aynı değer iki kez geçerse yalnız sonuncusu (kendi sırasında) kuyruğa girer. Aynı dosyayı yeniden
  içe aktarmak eski kopyanın yerini alır.
- Anahtar adında `::` geçen girdiler desteklenmez (ChangeSet hedef ayırıcısı).
- Girdiler oturumda tutulur; kalıcı olan kuyruktur (preset). Preset'ten gelen değerler bu listede görünmez.

## 5. Kabul
- [ ] Değer ekle (her tür) → Uygula → yeniden bağlayınca / `wlcli reg-check` ile değer hive'da.
- [ ] "Kurulumdan sonra yeniden uygula" açık değer → imajda `Windows\Setup\Scripts\WinLove\*.reg`; kurulumdan sonra kalıcı.
- [ ] Değeri düzenle → kuyrukta eskisinin yerine yenisi; kapalıyken düzenlenen kapalı kalır.
- [ ] .reg sürükle-bırak → listede görünür, hatalı dosyada satır numaralı hata.
