# P06 — ISO Oluştur / USB

> Durum: 🟨 ISO sekmesi bitti, kullanıcı testi bekliyor (2026-09-28). USB sekmesi yazıldı (D-047, 2026-09-30), gerçek yazma lab testi bekliyor. Tasarım: 16.

## 1. Amaç
Açık kaynaktan (kurulum klasörü veya ISO) önyüklenebilir Windows kurulum ISO'su üretmek.

## 2. Motor (D-025)
`core::buildIso` — Windows'un kendi **IMAPI2FS** COM bileşeni (ADK/oscdimg yok, yönetici gerekmez):
- UDF 1.02 (install.wim 4 GB'tan büyük olabildiği için ISO 9660 yok), birim etiketi ≤ 32.
- El Torito: BIOS `boot\etfsboot.com` (x86, emülasyonsuz, 8 sektör) + UEFI `efi\microsoft\boot\efisys.bin`
  (platform 0xEF, 2880 sektör) — Microsoft ISO'larıyla aynı katalog düzeni (doğrulandı: BRVD "EL TORITO
  SPECIFICATION", doğrulama girişi + 0x91 bölüm başlığı). `efisys_noprompt.bin` seçeneği.
- IMAPI görüntüyü okunurken üretir: yazma = oluşturma; ilerleme + iptal; `.part` dosyası tamamlanınca yerine taşınır.
- İsteğe bağlı SHA-256 (CNG) → `<iso>.sha256`.
- `repackInstallImage`: install.wim'in tüm sürümlerini seçilen sıkıştırmayla yeni dosyaya export edip değiştirir
  (servis sonrası boşluğu geri kazanır; LZMS → install.esd). Yalnızca WinLove çalışma klasörlerinde.
- Ölçüm (2026-09-28): 8,1 GB kurulum klasörü → ISO 17 sn (SSD, önbellekte); kendi UDF okuyucumuz 6 sürümü okudu.
- CLI: `wlcli iso <klasör> <çıktı.iso> [--label=] [--boot=both|uefi|bios] [--sha256] [--no-prompt]`.
- **Kurulum ortamı (D-038):** `core::patchBootImage` — `sources\boot.wim`'in önyükleme index'ine
  `HKLM\SYSTEM\Setup\LabConfig` atlamalarını yazar (yönetici + DISM). ISO üretimi bunu dosyanın bir kopyasında yapar ve
  `IsoOptions::replacedFiles` ile ISO'ya kopyayı koyar; kurulum klasörü değişmez. CLI: `wlcli boot-patch`.

## 3. Ekran
Sekmeler ISO / USB · ÇIKTI (dosya adı, klasör + gözat, birim etiketi) · ÖNYÜKLEME (UEFI+BIOS / UEFI / BIOS radyo,
install.wim sıkıştırma: Olduğu gibi / LZX / XPRESS / ESD, "tuşa basın" istemi) · DOĞRULAMA (SHA-256, bitince
klasörü aç) · sağda 320 px özet (kaynak, önyükleme, tahmini ISO, süre). Başlık: "ISO Oluştur" (çalışırken "İptal").
Engeller InfoBar'da: kaynak yok / tek WIM / bağlı imaj / başka işlem. ISO kaynağı önce çalışma klasörüne açılır.
ÖNYÜKLEME'de ayrıca **Kurulum ortamı**: "Gereksinim atlamalarını boot.wim'e de yaz" (varsayılan açık). Katılımsız
Kurulum'da atlama seçili değilse pasif + ipucu. Özette "Kurulum ortamı: N gereksinim denetimi atlanıyor / değiştirilmiyor";
çalışırken "Kurulum ortamı (boot.wim) hazırlanıyor" aşaması (~30 sn). Yama başarısızsa ISO üretilmez (hata bandı).
Sapma: tasarımdaki "4 GB üstü WIM → ESD" satırı sıkıştırma listesindeki ESD seçeneğine taşındı; yerine önyükleme
istemi seçeneği geldi (USB/FAT32 konusu USB sekmesiyle ele alınacak).

## 3b. USB sekmesi (D-047)
- Motor `core/usb/UsbMedia`: `listUsbDisks` (USB / SD / MMC; sistem diski asla), `planUsbCopy` (yer, 4 GB denetimi),
  `writeUsb` (diskpart + bootsect + kopya + `splitWim`). CLI: `wlcli usb-list [--all]`, `wlcli usb-write <disk>
  <klasör> --yes [--gpt] [--label=] [--unattend=]`. Lab: `tools\lab_usb.ps1` (VHDX, yönetici).
- Ekran: USB BELLEK (disk açılır listesi + yenile, FAT32 etiketi ≤ 11), ÖNYÜKLEME (MBR — BIOS + UEFI / GPT — yalnız
  UEFI, install.wim sıkıştırma, kurulum ortamı), BİTİNCE (sürücüyü aç). Uyarı bandı her zaman: "Seçilen USB
  bellekteki her şey silinir"; sığmıyorsa hata bandı. Özet: kaynak, disk, bölüm düzeni, install.wim (olduğu gibi /
  .swm), yanıt dosyası, kurulum ortamı, süre (~40 MB/sn). Başlık düğmesi "USB'ye yaz" → onay dialogu → aynı ilerleme
  satırı ("USB belleğe yazılıyor"). Yönetici değilse UAC.

## 4. Kabul
- [ ] Kaynak açıkken ISO Oluştur → ilerleme → ISO + .sha256; klasör açılır.
- [ ] ISO'yu VM'de UEFI ve BIOS ile başlat → Windows kurulumu açılır.
- [ ] Uygula sonrası LZX yeniden paketle → install.wim küçülür.
- [ ] Katılımsız Kurulum'da TPM / Secure Boot atlaması seçili, "ISO'ya ekle" kapalı → ISO üret → log'da
      `boot.wim index 2: N requirement check(s) switched off`; TPM'siz VM'de Setup gereksinim uyarısı vermez.
- [ ] Aynı kaynakla kutu kapalı ISO → boot.wim özgün (kurulum klasöründeki dosyanın boyutu / tarihi hiç değişmedi).
- [ ] `tools\lab_usb.ps1` (MBR) ve `-Gpt`: ALL PASSED.
- [ ] Gerçek USB bellek: uygulamada yaz → aynı bellekten bir bilgisayar / VM UEFI ve BIOS ile kurulum başlatır;
      install.swm'den sürüm listesi gelir.
