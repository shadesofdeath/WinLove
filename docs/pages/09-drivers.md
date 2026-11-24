# P09 — Sürücüler

> Durum: 🟨 bitti, kullanıcı testi bekliyor (2026-09-28). Tasarım: 07. Kurulum ortamı sekmesi: D-056, D-134 (2026-11-10).

## 1. Amaç
Bir klasördeki (ör. üreticinin sürücü paketi) INF sürücülerini sınıfa göre görüp seçilenleri bağlı imaja eklemek
(kuyruk → P05 Uygula). Üçüncü sekme: kurulumun kendi imajına (boot.wim) girecek sürücüler — kurulumun diski
(Intel VMD / RST, RAID) ya da kablolu ağı görmesi için.

## 2. Motor
- `core::scanDrivers(folder)`: özyinelemeli *.inf; her INF `parseInf` ile okunur (UTF-16 LE BOM / UTF-8 / ANSI).
  `[Version]` → Class, ClassGUID, Provider (`%strings%` çözülür), CatalogFile (yoksa `CatalogFile.NTamd64/.NTarm64/.NTx86`),
  DriverVer (tarih, sürüm); `[Manufacturer]` süslemeleri (NTamd64 / NTarm64 / NTx86) → mimariler (süsleme yoksa hepsi);
  modeller bölümlerinden donanım kimlikleri (`hardwareIds`), `*IfType` (AddReg satırı ya da NetAdapterCx'in
  `*IfType = 71` biçimi).
  Boyut = INF'in klasöründeki dosyaların toplamı / o klasördeki INF sayısı (yaklaşık, paylaşılan klasörler).
- `DismSession::addDriver` → `DismAddDriver(ForceUnsigned = FALSE)`; imzasız sürücü DISM hatası → Uygula'da "atlandı".
- Tarama okuyucu iş parçacığında (`reader()`), DISM kuyruğunu bekletmez. Unit test: `tests/core/DriverTests.cpp`.
- **Kurulum ortamı (D-134):**
  - `core::setupRole(inf)`: Depolama (SCSIAdapter, HDC) · Ağ (Net, bir veri yolunda: PCI\ USB\ SD\ ACPI\ VMBUS\…) ·
    Kablosuz (`*IfType` 71: Windows PE'de WLAN hizmeti yok) · Sanal (yalnız `*VMnetAdapter1`, `ROOT\…` gibi kimlikler:
    bu PC'deki bir yazılımın bağdaştırıcısı) · Diğer. "Bu bilgisayardan al" boot sekmesinde yalnız Depolama + Ağ'ı seçer.
  - `core::patchBootImage` sürücüleri yalnız **önyükleme index'ine (2, "Microsoft Windows Setup")** ekler: medya onu
    açar, kurulum diskleri ondan arar; index 1 (düz Windows PE) medya tarafından hiç başlatılmaz.
  - DISM'e sormadan elenen (`checkBootDriver`): dosya yok · imajın mimarisine modeller bölümü yok (DISM böyle bir INF'i
    sessizce "imzasız paket" olarak alıyordu — lab) · katalog adı yok / katalog dosyası yanında değil. Aynı yol iki kez
    verilirse bir kez eklenir.
  - DISM'e sorulan her paketten önce ve sonra `DismGetDrivers`: yeni yayımlanan ad yok → "imajda zaten var"; yeni paket
    imzasız → `DismRemoveDriver` ile geri alınır, "imzasız (katalog uyuşmuyor)" diye reddedilir (DISM önyükleme için kritik
    olmayan imzasız sürücüyü alıyor, Windows PE onu yüklemez). `0x80070032` "İstek desteklenmiyor" = DISM'in imzasız
    önyükleme-kritik sürücüyü reddi (dism.log): aynı "imzasız (katalog uyuşmuyor)" nedeniyle raporlanır.
  - Rapor: `BootPatchReport::driversAdded / driversPresent / driversRefused` (`DriverRefusal`: neden + ayrıntı).
  - Medyada zaten olanlar: `core::readMediaDrivers(source)` — boot.wim'in kurulum imajı ve install.wim'in her sürümü,
    **bağlamadan** WIM dosya listesinden: `Windows\INF\oemN.inf` + sürücü deposunda aynı SHA-1'li dosya → özgün ad.
    ISO yerinde okunur; ESD okunamaz (`installRead = false`).
  - CLI: `wlcli boot-patch <boot.wim> <mount> --driver=<inf>…` · `wlcli iso <klasör> <iso> --boot-driver=<inf>… [--bypass=…]`
    (ISO Oluştur'un yaptığı: boot.wim kopyası yamalanır, ISO'ya o girer; bir sürücü reddedilirse ISO yine yapılır, çıkış 3) ·
    `wlcli media-drivers <iso|klasör|wim>` · `wlcli driver-scan <klasör> [--arch=x64]` (rol, "pick" = sekmenin seçeceği).

## 3. Ekran
- Başlık: Klasör tara… (birden çok klasör eklenebilir; aynı INF yolu tekrar eklenmez) · Bu bilgisayardan al.
- Araç çubuğu: arama (Ctrl+F), Sınıf, Mimari süzgeci; sağda "{f} klasör · {n} INF · {boyut}".
- Ağaç: sınıf satırı (üç durumlu onay, "Ağ (Net)" gibi dost ad, INF sayısı, toplam boyut) → INF satırları
  (Sağlayıcı · Sürüm, Boyut). Onay = `AddDriver` kuyruğu; nav rozeti kuyruktaki sürücü sayısı.
- Bağlı imaj yoksa "İmajlar'a git" boş durumu. Render: `--demo-drivers`.
- **Kurulum ortamı (boot.wim) sekmesi** (bağlı imaj gerekmez; onay = boot.wim'e, ISO / USB oluşturulurken):
  - En üstte "Kurulum ortamında olanlar (boot.wim)": medyanın boot.wim'indeki üçüncü parti paketler (soluk, işaretli,
    değiştirilemez; Boyut sütununda `oemN.inf`). Okuyucu iş parçacığında, kaynak başına bir kez.
  - Her INF satırının sağında ne olduğu: depolama · kablolu ağ · "kablosuz · kurulumda çalışmaz" (uyarı rengi) ·
    sanal bağdaştırıcı. Başka mimariye ait INF ("arm64 için", uyarı rengi) ve boot.wim'de aynı sürümü olan ("boot.wim'de
    var") soluk, seçilemez; ipucu nedenini söyler.
  - Sağ üst: "{n} sürücü eklenecek · ≈ {boyut} · boot.wim'de {m} var" (boyut: medyanın büyüyeceği kadar, INF klasörleri).
  - Alt satır: ne zaman eklendiği ("ISO / USB oluşturulurken … Uygula'da değil"); bir depolama sürücüsü imaj bağlıyken
    işaretlenince imaj kuyruğuna da eklenir ve bu satır söyler; imaja gitmeyen depolama sürücüsü uyarı rengiyle yazılır.
  - "Bu bilgisayardan al" bu sekmedeyken: dışa aktarılan paketlerden depolama + kablolu ağ (medyanın mimarisinde, medyada
    olmayan) kendiliğinden işaretlenir; bildirim "… kurulum ortamı için {p} sürücü seçildi".
  - Render: `--demo-drivers --demo-boot-drivers`, `--demo-boot-drivers=host`, `--demo-drivers --demo-boot-drivers=storage-only`,
    `--page=iso --demo-boot-drivers=iso | iso-done`.
- **ISO Oluştur (D-134):** boot.wim sürücüleri "Gereksinim atlamaları" kutusundan bağımsız (önceden kutu kapatılınca
  sessizce düşüyorlardı). Özet: "Kurulum ortamı: N denetim · M sürücü", "Ortam sürücüleri: a.inf, b.inf". Yalnız
  boot.wim'e giden depolama sürücüsü (kuyrukta, son Uygula'da ve install.wim'de yok) → uyarı bandı + "Sürücüler".
  Bitince reddedilen sürücü varsa band uyarı rengiyle adını ve nedenini söyler, ISO yine hazırdır.

## 4. Sınırlar
- İmzasız sürücü eklenmez (ForceUnsigned sunulmuyor) — boot.wim'de Windows PE onu zaten yüklemez.
- Sürücüler boot.wim'in yalnız kurulum imajına (index 2) girer; index 1'i kullanan özel bir WinPE / WDS düzeni için ayrı
  araç gerekir.
- **WinRE:** install.wim içindeki `Winre.wim`'e sürücü eklenmiyor. Sıfırlama / kurtarma ortamı yalnız kutudan gelen
  sürücülerle açılır: VMD / RAID diskini göremeyebilir. (D-080'deki `updateWinRe` hattı paket için var; sürücü için
  genelleştirilmedi.)
- Wi-Fi sürücüsü engellenmez, yalnız işaretlenmez ve "kurulumda çalışmaz" diye yazılır.
- Medyada zaten olanlar ESD kaynakta install.wim için okunamaz (uyarı yine verilir).

## 5. Kabul
- [ ] Klasör tara → sınıflar ve INF'ler doğru sağlayıcı/sürümle listelenir.
- [ ] Seçilen sürücüler Uygula'da eklenir; imaj içinde `Windows\System32\DriverStore\FileRepository` altında görünür.
- [x] boot.wim: gerçek sürücüler (VMware pvscsi, Intel VMD iaStorVD, Realtek GbE) index 2'ye girer, DISM listeler, index 1
      değişmez, akışlar sağlam; ARM64 / katalogsuz / kataloğu uyuşmayan elenir (`tools\lab_boot_drivers.ps1`, 2026-11-10).
- [x] boot.wim, önyüklemede: diski yalnız pvscsi denetleyicide olan VM'de kutudaki pvscsii'si index 2'den çıkarılmış
      boot.wim ile kurulum disk listesini boş gösterir; aynı kopyaya `wlcli iso --boot-driver` ile VMware pvscsi eklenince
      Windows'u kurar, masaüstü açılır (index 1'de kutudaki pvscsii dururken bile negatif disk görmedi: kurulum yalnız
      index 2'yi kullanır) — `lab_boot_drivers.ps1 -Vm`, 2026-11-10, kareler `out\vm-bd-neg|bd-pos`.
- [ ] Kullanıcı gerçek bir VMD / RAID makinesinde ISO'dan kurar.
