# P20 — Simgeler

**Durum:** 🟨 geliştirme bitti, motor kanıtlandı, kullanıcı testi bekliyor (2026-10-05) · **Tasarım:** handoff'ta karşılığı
yok — liste + ızgara, mevcut sayfaların dili (24 px kontroller, 12 px metin, vurgu çerçevesi = değişen). Kararlar: D-065
(yönlendirme), D-068 (dosya yaması).

## 1. Amaç
Windows'un bütün simgelerini imajda değiştirmek; iki yol:
1. **Sistem simgeleri (dosya yaması, D-068):** Windows'un simge dosyalarının (`Windows\SystemResources\*.mun` — Windows 10
   1903+'ta imageres, shell32, DDORes … simgelerinin asıl yeri) içindeki herhangi bir simge, dosyanın içinde değişir.
2. **Masaüstü ve Gezgin (yönlendirme, D-065):** 14 yuva (Bu Bilgisayar, Geri Dönüşüm, klasör, sürücüler, kısayol oku …)
   kayıt defteriyle `ProgramData\WinLove\Icons\*.ico`'ya yönlendirilir; Windows dosyasına dokunulmaz.

## 2. Ekran
- Başlık eylemleri: **İkon paketi yükle…** (bir klasör iki türü birden taşıyabilir; her tür kendi payını alır),
  **Kısayol okunu kaldır / geri getir**, **Paketi dışa aktar…** (kuyruktaki dosya yamalarını paket klasörüne),
  **Tümünü sıfırla** (iki sekmenin kuyruğu).
- Sekmeler: "Sistem simgeleri (dosya yaması)" · "Masaüstü ve Gezgin (yönlendirme)" (son seçilen hatırlanır).
- **Sistem simgeleri:** üstte bir satırlık uyarı (orijinal yedeklenir, WinSxS'e dokunulmaz, güncelleme / sfc geri alabilir).
  Solda 272 px dosya listesi + arama; satırda ad ("imageres.dll") ve rozet: "n değişiklik" (vurgu), "geri yüklenecek"
  (uyarı), "imajda yamalı" (soluk). Sağda başlık (ad · n simge · imajdaki yol) ve düğmeler: **Değiştir…**,
  **Orijinale döndür**, **Simgeyi kaydet…** (.ico olarak), imajda yamalıysa **İmajdaki orijinali geri yükle**.
  Altında ızgara (84 × 88 hücre, 48 px simge, altında "#3" / ad): imajın simgesi ya da seçilen yenisi (vurgu çerçeve +
  vurgu etiket); geri yükleme kuyruktaysa yedekteki orijinal gösterilir. Tıkla = seç, çift tık / Enter = değiştir,
  Delete = orijinale döndür, oklar / Home / End; ipucu: "imageres.dll,-3 · 8 boyut, en büyük 256 px" ya da "… → dosya".
- Kaynak: .ico olduğu gibi; PNG / JPEG / BMP → 16, 20, 24, 32, 40, 48, 64 px 32 bit bitmap + 256 px PNG (kare içine
  sığdırılır, kenarlar saydam) — Windows'un kendi simgeleriyle aynı yapı.
- Nav rozeti: iki sekmenin değişiklik sayısı.

## 3. Veri
| Veri | Kaynak | Ne zaman | Önbellek |
|---|---|---|---|
| Dosya listesi | `Windows\SystemResources\**\*.mun` (dizin taraması) | sekme ilk açılınca | bağlama başına |
| Bir dosyanın grupları | kendi PE ayrıştırıcımız (`core/image/icons`) | dosya seçilince, okuyucu iş parçacığında | bağlama başına |
| İmajda yamalı | `Windows\WinLove\IconBackup` altında yedek var mı | ilk soruşta | bağlama başına; Uygula bitince yenilenir |
| Kuyruk | `PatchIcons` işlemi: hedef = dosya, değer = `{"groups": {"#3": kaynak}}` ya da `{"restore": true}` | — | kuyruk |

Paket (yama türü): `<paket>\<dosya>\<id>.ico|png` (`imageres.dll`, `imageres` ya da `imageres.dll.mun`; `3.ico`, `#3.png`,
adlı gruplar için `<AD>.ico`) ve / veya `iconpack.json` → `{"name", "author", "files": {"imageres.dll": {"3": "a.ico"}}}`;
paketten çıkan yol reddedilir. Dışa aktarma aynı düzeni yazar.

## 4. Motor (D-068)
Uygula'da Ayarlar aşamasında (güncellemelerden sonra): kaynaklar yüklenir → dosyanın şimdiki hâli temel alınır (önceki
Uygula'nın simgeleri kalır) → ilk yamada orijinal `Windows\WinLove\IconBackup\<yol>`'a kopyalanır → kaynak bölümü
yeniden kurulur (simge dışı kaynaklar bayt bayt aynı; denetlenir) → yeni dosya hedefin yanına geçici adla, orijinalin
sahibi (TrustedInstaller) / grubu / DACL'iyle yazılır → Windows'un yükleyicisi (`LoadLibraryEx` + `CreateIconFromResourceEx`)
her grubun her görüntüsünü açabilmeli → yeniden adlandırma ile yerine konur (WinSxS'teki sabit bağlantı orijinal kalır) →
`restore-icons.cmd` güncellenir. Kod içeren dosya (explorer.exe gibi), WinSxS / servicing / boot / drivers / config reddedilir.
CLI: `wlcli icons | icon-extract | icon-patch | icon-verify | icon-image`.

## 5. Kabul
- [ ] Bir dosya seç → bütün simgeleri ızgarada; bir simgeyi .ico / PNG ile değiştir → vurgu, rozet, nav rozeti; Delete → geri.
- [ ] Uygula → imajı yeniden bağla: dosyada yeni simge, "imajda yamalı" rozeti; "İmajdaki orijinali geri yükle" → Uygula → orijinal.
- [ ] Paket yükle (iki tür birden) / dışa aktar → aynı klasör yeniden yüklenince aynı kuyruk.
- [ ] VM: yamalı imaj kurulur, masaüstüne ulaşır, simgeler yeni (`tools\lab_icons_vm.ps1`).

## 6. Bilinçli sapmalar / sınırlar
- Çalışan sistem yamalanmaz (D-021: yalnız çevrimdışı imaj); yama kurulumdan itibaren geçerlidir.
- Program dosyalarının (kod içeren PE) simgeleri yamalanmaz: imzası / katalog karması bozulur, Akıllı Uygulama Denetimi
  veya WDAC çalıştırmayı engelleyebilir. Bunların simgeleri için yönlendirme sekmesi.
- Bir toplu güncelleme dosyanın yeni sürümünü getirirse simgeler Microsoft'unkine döner (yedek eski sürüm kalır); `sfc`
  de geri alır. `restore-icons.cmd` WinRE'den de çalışır.
