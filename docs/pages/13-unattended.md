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
  - OOBE: Lisans sözleşmesini kabul et, Gizlilik sorularını atla, Microsoft hesabı zorunluluğunu kaldır
    (açık = BypassNRO yazılır), Çevrimiçi kurulumu atla.
  - Ürün anahtarı: anahtar, Kurulacak sürüm (kaynağın index'leri). Anahtar boşsa ve kurulacak sürüm belliyse
    (imajda tek sürüm var ya da "Kurulacak sürüm" seçili) o sürümün **genel anahtarı** yazılır: Setup sormadan o
    sürümü kurar, etkinleştirme kurulumdan sonraya kalır. Sürüm belli değilse Setup anahtar sayfasını gösterir.
  - Gereksinimler: TPM 2.0 / Secure Boot / RAM / işlemci / disk boyutu denetimini atla — açık = LabConfig yazılır
    (`BypassTPMCheck`, `BypassSecureBootCheck`, `BypassRAMCheck`, `BypassCPUCheck`, `BypassStorageCheck`).
  - Bütün düğmeler aynı yönde okunur: açık = WinLove bunu XML'e yazar (D-032; ilk sürümde bu satırlar ters çalışıyordu).
  - Geçersiz değerde satırın ipucu kırmızı olur (bilgisayar adı, hesap adı, ürün anahtarı, hesapsız otomatik oturum).
- **Canlı önizleme** (sağ, ~%45): dosyanın gerçek metni, satır numaralı mono; son düzenlemeyle değişen satırlar
  vurgu renginde; uzun satırlar kaydırılır (devam satırı numarasız); tekerlek / kaydırma çubuğu.
  Panel başlığında **ISO'ya ekle** onay kutusu. **İlk yanıt verildiğinde (ve XML içe aktarılınca) kendiliğinden
  işaretlenir** (D-034); elle kapatılırsa sonraki düzenlemeler onu açmaz. ISO sayfasının özetinde "Yanıt dosyası"
  satırı her zaman durur: `autounattend.xml` / "yok" / turuncu "eklenmiyor · "ISO'ya ekle" kapalı".

## 3. Motor (`core/unattend/Unattend`)
- `UnattendOptions` → `buildUnattendXml`: yalnız istenen ayar yazılır (boş seçenekler = hiç `<settings>` yok).
  - `windowsPE`: `International-Core-WinPE` (dil), `Windows-Setup` (LabConfig `RunSynchronous`, `DiskConfiguration`,
    `ImageInstall` — `/IMAGE/INDEX` + `InstallTo`, `UserData` — anahtar, `AcceptEula`).
    **`UserData` hiçbir zaman `ProductKey`'siz yazılmaz** (D-036): kullanıcının anahtarı, yoksa kurulacak sürümün
    genel anahtarı (`genericProductKey`, `WillShowUI=OnError`), o da bilinmiyorsa `00000-…-00000` + `WillShowUI=Always`
    (Setup sorar). Kurulacak sürüm seçeneklerde değil, açık kaynaktan türetilir (`UnattendOptions::editionId`,
    mimari gibi). Dosyadan okunan genel anahtar / yer tutucu "kullanıcının anahtarı" sayılmaz.
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

## 3b. Karşılama ekranı (D-084)
- Hesap bölümünün başında **Karşılama ekranı**: yanıt dosyası geçici `WinLoveSetup` hesabını (rastgele parola) bir kez
  açtırır; WinLove'un tam ekran sihirbazı hesabı, bilgisayar adını, tema / vurguyu, gizliliği sorar ve kurar. Seçenekler:
  sorulacak sayfalar, önerilen tema ve gizlilik, parolasız hesap; **Önizle**. Açıkken Yerel hesap / Parola / Otomatik oturum
  kapalı. Betik kuyruğa girer (Uygula yazar); ISO Oluştur betik imajda yoksa reddeder.
- Motor `core/unattend/Welcome`; betik `resources/scripts/oobe.ps1`; render `--demo-unattended=welcome`.

## 4. Sınırlar
- Disk düzeni yalnız "disk 0'ı sil" kalıpları; var olan bölüme kurma / çoklu disk yok.
- Tek yerel hesap (yönetici). Microsoft hesabı / etki alanına katılma yok.
- Kurulum dili imajda bulunan bir dil olmalı (liste kaynaktan gelir; elle yazılan XML'de denetlenmez).
- BypassNRO kayıt değeri 25H2'de hâlâ çalışıyor varsayımıyla yazılır; yerel hesap + "Çevrimiçi kurulumu atla"
  birlikte kullanıldığında zaten gerekmez.
- Yanıt dosyası artık oturumlar arasında da tutulur (D-037): her değişiklikte `%LOCALAPPDATA%\WinLove\answers.dat`
  dosyasına yazılır, açılışta geri yüklenir. Dosya parolayı da içerdiği için tamamı Windows kullanıcısının anahtarıyla
  (DPAPI) korunur: başka bir hesap ya da dosyanın kopyası okuyamaz. Hiç yanıt yoksa dosya silinir.

## 5. Kabul
- [ ] Form değiştikçe önizleme güncellenir, değişen satırlar vurgulanır.
- [ ] XML kaydet → dosya; XML içe aktar → form aynı değerlerle dolar.
- [ ] Formu doldur (kutuya dokunmadan) + ISO Oluştur → ISO kökünde `autounattend.xml`; çalışma klasöründe dosya yok.
      (2026-09-30: kullanıcının ilk ISO'sunda dosya yoktu — kutu varsayılan kapalıydı ve fark edilmedi; D-034.
      Akış unit testte gerçek ISO üretilerek doğrulanıyor; logda `answer file: …` satırı.)
- [ ] VM: üretilen ISO ile kurulum soruları atlanır (dil, disk, hesap, OOBE); TPM'siz VM'de kurulum başlar.
      (2026-09-30, Windows 10 22H2 tek sürüm: Setup "yanıt dosyasından <ProductKey> ayarını okuyamıyor" dedi —
      `UserData` anahtarsız yazılıyordu; D-036 ile düzeltildi, yeniden denenecek.)
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
