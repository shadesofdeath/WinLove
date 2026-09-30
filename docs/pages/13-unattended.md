# P13 — Katılımsız Kurulum

> Durum: 🟨 geliştirme bitti, kullanıcı testi bekliyor (2026-09-30). Tasarım: 11.

## 1. Amaç
Windows Kurulumu'nun sorularını önceden yanıtlayan `autounattend.xml` dosyasını formdan üretmek: dil, hesap, disk,
OOBE sayfaları, ürün anahtarı / sürüm, Windows 11 gereksinim denetimleri. Dosya kaydedilebilir, içe aktarılabilir ve
istenirse bir sonraki ISO'nun köküne yazılır.

## 2. Ekran
- Başlık eylemleri: **XML içe aktar…**, **XML kaydet**.
- **Adım göstergesi** (6 adım): Dil ve bölge · Hesap · Disk · OOBE · Ürün anahtarı · Gereksinimler. Adımlar sihirbaz
  sayfası değil, tek kaydırılan formun **çapaları**: tıklayınca (ya da ←/→) o bölüme kayar; geçerli adım vurgu,
  öncekiler yeşil, sonrakiler soluk.
- **Form** (sol, 220 px etiket sütunu; `ui::FormView`):
  - Dil ve bölge: Kurulum dili (kaynağın dilleri), Bölge biçimi, Klavye, Saat dilimi — ilk seçenek "Kurulumda sor".
  - Hesap: Yerel hesap (yönetici), Parola (maskeli), Otomatik oturum açma (ilk açılışta, 1 kez), Bilgisayar adı.
  - Disk: Kurulumda sor · Disk 0'ı sil UEFI (GPT) · Disk 0'ı sil BIOS (MBR) — silme seçeneklerinde turuncu uyarı.
  - OOBE: Lisans sözleşmesini kabul et, Gizlilik sorularını atla, Microsoft hesabı zorunluluğu (kapalı = BypassNRO),
    Çevrimiçi kurulumu atla.
  - Ürün anahtarı: anahtar, Kurulacak sürüm (kaynağın index'leri).
  - Gereksinimler: TPM 2.0 / Secure Boot / RAM denetimi — açık = denetim kalır, kapalı = atlanır (LabConfig).
  - Geçersiz değerde satırın ipucu kırmızı olur (bilgisayar adı, hesap adı, ürün anahtarı, hesapsız otomatik oturum).
- **Canlı önizleme** (sağ, ~%45): dosyanın gerçek metni, satır numaralı mono; son düzenlemeyle değişen satırlar
  vurgu renginde; uzun satırlar kaydırılır (devam satırı numarasız); tekerlek / kaydırma çubuğu.
  Panel başlığında **ISO'ya ekle** onay kutusu.

## 3. Motor (`core/unattend/Unattend`)
- `UnattendOptions` → `buildUnattendXml`: yalnız istenen ayar yazılır (boş seçenekler = hiç `<settings>` yok).
  - `windowsPE`: `International-Core-WinPE` (dil), `Windows-Setup` (LabConfig `RunSynchronous`, `DiskConfiguration`,
    `ImageInstall` — `/IMAGE/INDEX` + `InstallTo`, `UserData` — anahtar, `AcceptEula`).
  - `specialize`: `Shell-Setup` (ComputerName, TimeZone), `Deployment` (BypassNRO `reg add`).
  - `oobeSystem`: `International-Core`, `Shell-Setup` (OOBE, LocalAccount — Administrators, AutoLogon 1 kez).
  - Parola açık yazılmaz: Setup'ın kendi kodlaması (UTF-16LE + "Password", Base64, `PlainText=false`).
    Bu gizleme değildir; dosyayı okuyan parolayı çözer.
  - `processorArchitecture` açık kaynağın mimarisinden (kaynak yoksa amd64).
- `parseUnattendXml`: kendi dosyamız ve başka araçlarınki; bilmediğimiz ayarlar **atılır** (içe aktar → kaydet,
  o dosyadaki FirstLogonCommands vb.'yi taşımaz).
- `validateUnattend`: bilgisayar adı, hesap adı, anahtar biçimi, hesapsız otomatik oturum.
- ISO: `IsoOptions::rootFiles` — dosya imaj köküne **bellekten** eklenir; kaynak klasöre dokunulmaz, klasördeki aynı
  adlı dosyanın yerine geçer (D-028). Geçersiz dosya ISO'ya girmez: ISO sayfası engel gösterir.
- CLI: `wlcli unattend <dosya.xml>` (oku → WinLove'un yazacağı hali bas). Testler: `tests/core/UnattendTests.cpp`,
  `tests/core/IsoTests.cpp` (gerçek ISO üretir, kendi UDF okuyucumuzla doğrular), `tests/app/UnattendControllerTests.cpp`.

## 4. Sınırlar
- Disk düzeni yalnız "disk 0'ı sil" kalıpları; var olan bölüme kurma / çoklu disk yok.
- Tek yerel hesap (yönetici). Microsoft hesabı / etki alanına katılma yok.
- Kurulum dili imajda bulunan bir dil olmalı (liste kaynaktan gelir; elle yazılan XML'de denetlenmez).
- BypassNRO kayıt değeri 25H2'de hâlâ çalışıyor varsayımıyla yazılır; yerel hesap + "Çevrimiçi kurulumu atla"
  birlikte kullanıldığında zaten gerekmez.
- Yanıt dosyası oturumda tutulur (uygulama kapanınca gider); kalıcılık P15 Presetler ile.

## 5. Kabul
- [ ] Form değiştikçe önizleme güncellenir, değişen satırlar vurgulanır.
- [ ] XML kaydet → dosya; XML içe aktar → form aynı değerlerle dolar.
- [ ] "ISO'ya ekle" + ISO Oluştur → ISO kökünde `autounattend.xml`; çalışma klasöründe dosya yok.
- [ ] VM: üretilen ISO ile kurulum soruları atlanır (dil, disk, hesap, OOBE); TPM'siz VM'de kurulum başlar.
- [ ] Geçersiz bilgisayar adı → kırmızı ipucu; "ISO'ya ekle" açıkken ISO sayfası engel gösterir.

## 6. Görsel doğrulama / bilinçli sapmalar
Render: `--demo-unattended` (tasarım 11'in yanıtları). Adım çubuğu, bölüm başlığı, satır ve önizleme dikey ölçüleri
tasarımla örtüşüyor (2026-09-30; dark, light/EN + gerçek ISO, 1440×560'ta adım tıklama).
- Tasarım OOBE adımında hesap alanlarını ve Gereksinimler'i birlikte gösteriyor; biz alanları kendi adımlarına
  koyduk ve adımları çapa yaptık (tasarımdaki karışık örnek tek kaydırılan formla tutarlı).
- Önizleme dosyanın gerçek metnidir (tasarımdaki kısaltılmış `<component name="…">` yerine tüm öznitelikler,
  sarılmış satırlarla).
- Geçerli adım etiketi Caption (400) — tokenlarda 11 px / 500 stil yok.
- "ISO'ya ekle" kutusu, "Lisans sözleşmesi", dil / saat dilimi / bilgisayar adı / sürüm alanları tasarımda yok.
