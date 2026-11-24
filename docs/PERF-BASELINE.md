# Performans temel ölçümü — 2026-11-10

Optimizasyon turundan **önceki** durum (commit `1259c1e`, Release). "Sonra" tablosu turun sonunda bu dosyanın altına eklenir.

## Nasıl ölçüldü
- Araç: `python -I tools/perf_measure.py <etiket> [--only=startup,pages,render,cli] [--iso=<yol>]`. Ham sonuç:
  `build\lab\perf\<etiket>.json`. Ölçülen ikililer `build\lab\perf\bin\<etiket>\` altında saklanır (sonradan yan yana
  yeniden ölçülebilir).
- Pencereli ölçümler: `--no-elevate --profile=build\lab\perf\profile --size=1360x800`, koyu tema, yönetici değil,
  bağlı imaj yok. Pencere görünür olduktan ve 3 sn bekledikten sonra 5 sn boyunca CPU süresi; sonra bellek sayaçları.
- Render: `--render --theme=dark --scale=1.75 --size=1207x700`, sayfa başına 3 çalıştırma. Süre **sürecin tamamıdır**
  (başlatma + arayüzü kurma + tek kare + PNG yazma), yalnız çizim değil.
- wlcli: komut başına 3 çalıştırma, sürecin tamamı.
- Makine: geliştirme bilgisayarı, Windows 11 Pro 26200, %175 ölçek. Ölçüm sırasında arka planda başka işler de
  vardı; ±%10 altındaki farklar gürültü sayılmalı.
- CLAUDE.md'deki test ISO'su (`Downloads\Win11_25H2_Turkish_x64_v2.iso`) artık yerinde değil; ISO gerektiren ölçümler
  `build\lab\uup\out\x64-9448.iso` (7,4 GB, 26200.9448 Pro tr-tr) üzerinde, yalnız okuyarak yapıldı.

## Ölçülemeyenler (bu yüzden "iyileşti" denmeyecek)
- Bağlı imajla dolu sayfalar (Bileşenler ~1.300 satır, gerçek paket listeleri) ve motorun değiştiren işlemleri
  (mount, apply, unmount): yönetici gerekir. Demo verili render'lar (aşağıda) yalnız küçük örnek listeler içerir.
- Winget dizini (~15.400 paket) yüklüyken Programlar sayfasının arama / kaydırma maliyeti: dışarıdan ölçen bir araç yok.
- Kare süresi (kaydırma, üzerine gelme sırasında): dışarıdan ölçülmedi.
- Uzun oturumda sızıntı (saatlerce açık, sayfa gezme): ölçülmedi; yalnız tek sayfa açılışı sonrası anlık değerler var.

## Derleme
| Ölçü | Değer |
|---|---|
| Temiz Release derlemesi (`build.ps1 -Config Release -Clean`: üreticiler + yapılandırma + derleme) | 151,2 sn |
| `WinLove.exe` | 11.211.264 bayt |
| `wlcli.exe` | 4.292.096 bayt |

## Açılış (5 çalıştırma, Kaynak sayfası)
| Ölçü | Medyan | Çalıştırmalar |
|---|---|---|
| Süreç başlangıcı → pencere görünür | 79 ms | 79, 79, 74, 77, 79 |
| Süreç başlangıcı → girdi beklemeye geçti (`WaitForInputIdle`) | 272 ms | — |

## Sayfa başına bellek ve boşta CPU (pencereli, sayfa başına yeni süreç)
| Sayfa | Private Bytes MB | Working Set MB | Boşta CPU % | Handle | GDI | USER |
|---|---|---|---|---|---|---|
| source | 81,8 | 96,8 | 0,0 | 575 | 16 | 16 |
| download | 88,3 | 104,0 | 0,0 | 687 | 16 | 17 |
| images | 82,4 | 97,1 | 0,0 | 575 | 16 | 16 |
| components | 82,9 | 98,2 | 0,0 | 575 | 16 | 16 |
| apps | 82,0 | 96,9 | 0,0 | 575 | 16 | 16 |
| programs | 93,4 | 117,9 | 0,0 | 960 | 16 | 17 |
| features | 95,4 | 109,7 | 0,0 | 573 | 16 | 16 |
| updates | 82,2 | 97,4 | 0,0 | 575 | 16 | 16 |
| languages | 82,9 | 98,0 | 0,0 | 577 | 16 | 16 |
| drivers | 81,8 | 96,9 | 0,0 | 575 | 16 | 16 |
| registry | 82,3 | 97,4 | 0,0 | 575 | 16 | 16 |
| services | 81,8 | 97,1 | 0,0 | 575 | 16 | 16 |
| tasks | 82,0 | 96,8 | 0,0 | 575 | 16 | 16 |
| tweaks | 84,3 | 98,0 | 0,0 | 573 | 16 | 16 |
| startmenu | 82,3 | 97,0 | 0,0 | 575 | 16 | 16 |
| hosts | 82,1 | 97,1 | 0,0 | 575 | 16 | 16 |
| branding | 82,2 | 97,4 | 0,0 | 575 | 16 | 16 |
| icons | 84,5 | 98,2 | 0,0 | 575 | 16 | 16 |
| unattended | 111,8 | 122,4 | 0,0 | 656 | 16 | 16 |
| postsetup | 81,8 | 96,9 | 0,0 | 575 | 16 | 16 |
| files | 82,1 | 97,1 | 0,0 | 575 | 16 | 16 |
| apply | 82,2 | 97,0 | 0,0 | 575 | 16 | 16 |
| iso | 96,8 | 111,6 | 0,0 | 575 | 16 | 16 |
| presets | 114,7 | 99,6 | 0,0 | 549 | 16 | 16 |
| logs | 81,8 | 97,0 | 0,0 | 575 | 16 | 17 |
| settings | 99,4 | 98,3 | 0,0 | 573 | 16 | 16 |
| about | 87,1 | 100,9 | 0,0 | 575 | 16 | 16 |
| images + ISO açık | 127,9 | 112,4 | 0,0 | 575 | 16 | 16 |

Gözlemler: boşta CPU her sayfada %0 (hedef tutuyor). Boş sayfanın tabanı ~82 MB Private Bytes. Tabandan belirgin
ayrılanlar: ISO açıkken Görüntü (+46 MB), Ön Ayarlar (+33), Katılımsız (+30), Ayarlar (+18), ISO (+15), Özellikler (+14),
Programlar (+12, 960 handle).

## Render süresi (sürecin tamamı, ms)
| Sayfa | Medyan | En az | En çok | Tepe WS MB |
|---|---|---|---|---|
| source | 112 | 109 | 211 | 101,8 |
| download | 899 | 809 | 1113 | 104,8 |
| images | 117 | 114 | 119 | 101,5 |
| components | 108 | 107 | 113 | 102,0 |
| apps | 110 | 109 | 115 | 101,5 |
| programs | 116 | 109 | 1278 | 113,4 |
| features | 117 | 114 | 120 | 114,0 |
| updates | 111 | 107 | 112 | 101,4 |
| languages | 129 | 128 | 131 | 102,5 |
| drivers | 110 | 109 | 111 | 101,8 |
| registry | 109 | 107 | 111 | 101,5 |
| services | 109 | 107 | 110 | 101,5 |
| tasks | 110 | 110 | 118 | 101,6 |
| tweaks | 110 | 109 | 111 | 101,7 |
| startmenu | 112 | 109 | 122 | 101,7 |
| hosts | 109 | 108 | 111 | 101,4 |
| branding | 110 | 110 | 112 | 101,5 |
| icons | 115 | 113 | 115 | 101,5 |
| unattended | 160 | 157 | 162 | 123,7 |
| postsetup | 120 | 113 | 123 | 101,5 |
| files | 109 | 106 | 114 | 101,5 |
| apply | 108 | 107 | 110 | 101,5 |
| iso | 124 | 123 | 128 | 114,9 |
| presets | 128 | 125 | 129 | 102,3 |
| logs | 111 | 111 | 112 | 101,4 |
| settings | 137 | 137 | 143 | 104,4 |
| about | 122 | 120 | 124 | 101,8 |
| components `--demo-components` | 134 | 132 | 135 | 113,0 |
| features `--demo-features` | 118 | 114 | 121 | 101,9 |
| registry `--demo-registry` | 117 | 117 | 117 | 102,2 |
| services `--demo-services` | 112 | 110 | 116 | 101,6 |
| tasks `--demo-tasks` | 126 | 126 | 129 | 102,9 |
| tweaks `--demo-tweaks` | 140 | 140 | 144 | 102,5 |
| drivers `--demo-drivers` | 136 | 132 | 142 | 106,1 |
| postsetup `--demo-postsetup` | 125 | 124 | 128 | 102,7 |
| unattended `--demo-unattended` | 148 | 148 | 156 | 123,8 |
| presets `--demo-presets` | 130 | 128 | 130 | 100,8 |
| logs `--demo-logs` | 121 | 119 | 122 | 103,8 |
| hosts `--demo-hosts` | 120 | 115 | 120 | 102,0 |
| apply `--demo-features --demo-apply=running` | 132 | 128 | 134 | 103,2 |

Gözlemler: taban ~108 ms. Öne çıkan: **download 899 ms** (diğerlerinin 8 katı), unattended 160, settings 137,
programs'ta tek seferlik 1278 ms. `--demo-languages` ölçüme alınmadı: sonsuz döngüye girdiği STATUS.md'de kayıtlı.

## wlcli (sürecin tamamı, ms)
| Komut | Medyan | En az | En çok | Tepe WS MB |
|---|---|---|---|---|
| help | 9 | 9 | 10 | 9,7 |
| info `<iso>` | 10 | 9 | 20 | 10,1 |
| info `<iso>` --json | 9 | 9 | 9 | 10,0 |
| ls `<iso>` | 8 | 8 | 9 | 9,8 |
| ls `<iso>` sources | 9 | 9 | 9 | 9,9 |
| verify `<iso>` (7,4 GB, her akışın SHA-1'i) | 9.593 | 9.131 | 11.841 | 57,0 |
| plan sample-changeset.json | 9 | 8 | 9 | 10,0 |

---

# Sonra — 2026-11-10 (aynı gün, düzeltmelerden sonra)

Aynı araç, aynı makine, aynı ISO. Etiketler: `baseline` = commit `1259c1e`, `after` = çalışma ağacı (commit yok).
İkililer: `build\lab\perf\bin\baseline\`, `build\lab\perf\bin\after\`. ±%10 altı farklar gürültü sayıldı.

## Dışarıdan ölçülenler (önce → sonra)
| Ölçü | Önce | Sonra | Yorum |
|---|---|---|---|
| `WinLove.exe` boyutu | 11.211.264 | 11.250.176 bayt | +38.912 (yeni kod) |
| Açılış: pencere görünür (medyan) | 79 ms | 79 ms | değişmedi |
| Açılış: girdi beklemeye geçti | 272 ms | 267 ms | gürültü |
| Boşta CPU (her sayfa) | %0 | %0 | değişmedi |
| Boş sayfa tabanı (source, Private Bytes) | 81,8 MB | 82,4 MB | değişmedi |
| Render: `download` (medyan) | 899 ms | 495 ms | −%45; kalan süre aşağıda |
| Render: diğer 39 senaryo | 108–160 ms | 109–158 ms | gürültü içinde |
| `wlcli icon-patch` imageres.dll.mun (24 MB): tepe çalışma kümesi | 289,1 MB | 196,5 MB | −%32 |
| `wlcli icon-patch` aynı: süre (medyan) | 583 ms | 648 ms | **+%11 (yavaşladı)** |
| `wlcli verify` 7,4 GB ISO | 8.899 ms | 8.801 ms | gürültü (bu yola dokunulmadı) |
| `wlcli info / ls / plan` | 8–9 ms | 8–9 ms | değişmedi |

Notlar:
- `download` render'ının 899 ms'si sayfa kurulurken başlatılan liste isteğinin, süreç kapanırken beklenmesiydi
  (`TaskRunner` çalışan işi iptal etmeden `join` ediyordu). Artık iptal ediliyor; kalan ~380 ms eşzamanlı WinHTTP
  çağrısının iptali görmesini beklemek (AUDIT OPS-07, yapılmadı).
- Simge yamasında bellek kazancı, kopyaların daha erken bırakılmasından; 65 ms'lik yavaşlama ölçüldü, nedeni
  ayrıştırılmadı (büyük blokların bırakılıp yeniden ayrılması olası).
- Sayfa tablosunda `programs` (93,4 → 83,3 MB, 960 → 576 handle), `settings` (99,4 → 91,1) ve "images + ISO açık"
  (127,9 → 115,5) düştü, `components` (82,9 → 87,7) ve `about` (87,1 → 92,7) arttı. Bunların hiçbiri bir düzeltmeye
  bağlanamadı (o sayfaların açılış yoluna dokunulmadı; `programs` farkı büyük olasılıkla winget dizininin o an
  yenilenip yenilenmemesi): **kazanç sayılmıyor**.

## Süreç içinden ölçülenler (mikro ölçümler)
`build\x64-release\bin\wl_tests.exe -tc="perf*" --no-skip -s` (`tests/PerfBench.cpp`). **"Önce" eski build'in ölçümü
değil**: eski algoritmanın aynı ikilide, aynı veriyle yeniden yazılmış hâli. Veriler yapay (900 adımlık kuyruk, 1.300
satır, 31 koruma); gerçek sayfadaki toplam kazanç bu sayıların bir parçası kadardır.

| Ne | Önce | Sonra | Kat |
|---|---|---|---|
| `ChangeSet::find` — 1.300 satırın bir geçişi, 900 adımlık kuyruk | 4,50 ms | 0,25 ms | 18× |
| Uygula sonunda 850 adımın kuyruktan çıkması (`queue.json` yazımıyla, dinleyicisiz) | 1.162 ms | 0,75 ms | ~1.550× |
| Uyumluluk sorusu — 1.300 bileşen satırı (bir `updateChecks`) | 74,97 ms | 2,62 ms | 29× |
| 20.000 log satırının saat metni (Loglar'ın bir yeniden kurulumu) | 70,74 ms | 6,88 ms | 10× |
| Hosts içe aktarma, 30.000 yeni ad | 899,8 ms | 39,1 ms | 23× |

"850 adım" satırı dinleyicisizdir: uygulamada her bildirim ayrıca Shell'in ve açık sayfanın dinleyicilerini de
çalıştırıyordu, yani gerçek "önce" bundan büyüktü; ölçülmedi.

## Ölçülemeyen değişiklikler ("iyileşti" denmiyor)
| Değişiklik | Neden ölçülemedi | Elde olan |
|---|---|---|
| UI-01: iş sürerken pencerenin her karede yeniden çizilmesi | Çalışan bir iş yönetici / tıklama gerektirir | Birim testi: tick içindeki `invalidate()` artık kare istemiyor, 200 ms'lik zamanlayıcı kuruluyor |
| OPS-03: geri alma geçmişinin belleği | Uzun oturum + karşılama kuyruğu gerekir | Birim testi: ~1 MB'lık adımla geçmiş ≤ 64 durum (önce 200) |
| IMG-05: Uygula sonrası kalan analiz önbellekleri | Bağlı imajda Uygula (yönetici) gerekir | Yok; kod okunarak yapıldı. Büyüklük (100 MB üstü) denetimin **tahminiydi** |
| CTL-08: winget listelerinin önbelleği | Dizin yüklü sayfada tıklama gerekir | Yok |
| PG-04 / APP-05: toplu işaretlemede tek bildirim | Dolu liste (bağlı imaj) gerekir | Birim testi: 100 satır = 1 bildirim, 1 geri alma adımı |

## Boş sayfanın ~82 MB'ı nereden (araştırma, değişiklik yok)
`baseline` ikilisinde, Kaynak sayfası açıkken bölge dökümü: 73,4 MB özel + 6,7 MB yazılabilir imaj sayfası.
En büyük tek özel ayırma 30,7 MB (sıfırla dolu, bir yığın tabanı değil), sonra 9,0 MB; yazılabilir imaj sayfalarının
4,8 MB'ı `igc-default64.dll` (Intel gölgelendirici derleyicisi). Tümleşik ekran kartında doku belleği sistem
RAM'inden gelir; 30,7 MB'lık blok büyük olasılıkla sürücünün / takas zincirinin (kanıtlanmadı: sahibi belirlenmedi).
WinLove'un kendi yığınlarında boşta öne çıkan bir tüketici görülmedi; bu turda dokunulmadı.

---

# İkinci tur ve lab — 2026-11-10 (gece)

**Yukarıdaki pencereli, render ve wlcli tabloları ikinci turdan (D-130 · D-131 · D-132) ve arayüz cilasından (D-133)
sonra yeniden ölçülmedi.** "Sonra" birinci turun sonudur; bugünkü `dist\WinLove.exe` için geçerli sayılmamalı
(özellikle `download` render'ı: kalan ~380 ms'nin nedeni sayılan OPS-07 düzeltildi, etkisi ölçülmedi). Yenilemek için aynı araç:
`python -I tools/perf_measure.py <etiket>`.

## Yönetici laboratuvarı: D-131 (`build\lab\r2\REPORT.md`)
26200.8037 Pro'nun (`build\lab\setup\sources\install.wim` sürüm 4) dışa aktarılmış kopyası, her koşu taze kopyada; aynı
30 adımlık değişiklik kümesi (18 depo kopyası, 15'i servisli; CBS paketli 3 tarif; appx'li 2 tarif; 2 RemoveAppx;
1 yetenek; 1 özellik; 2 kayıt değeri; 1 servis). Eski = commit `1259c1e` (`build\lab\perf\bin\baseline\wlcli.exe`),
yeni = ikinci tur (`build\lab\perf\bin\round2\wlcli.exe`). Her ikili **bir kez** koştu; makinede başka işler vardı.

| Ölçü | Eski | Yeni |
|---|---|---|
| `wlcli apply` (30 adım) | 152,4 sn | 126,0 sn |
| Depo kopyası adımı, medyan (18 adım) | 1,360 sn | 0,508 sn |
| Depo kopyası adımlarının toplamı | 31,0 sn | 15,9 sn |
| Sonuç: 30 / 30, ScanHealth, yüklü kovan | temiz | temiz |
| Kalan dosya / servis anahtarı / CBS / appx | 59.316 / 682 / 3.403 / 43 | aynı (fark 0) |

Yeni ikilide depo kopyası adımı, adımın kendi işi kadar sürüyor (`perf store copies` satırı: açılış analizi ~0,4 sn,
gerisi bağlantılar / içe aktarımlar / dosyalar); eskide aradaki fark DISM oturumunun yeniden açılmasıydı. D-131
taslağının "< 0,5 sn" ölçütü kıl payı tutmadı; kalan süre DISM değil, açılış analizi (IMG-02, atlandı).

## Görsel fark aracı: `tools/render_diff.py`
`python -I tools/render_diff.py <eski WinLove.exe> <yeni WinLove.exe> [--iso=<yol>]`: bütün sayfalar ve demo durumları,
koyu + açık, `--scale=1.75 --size=1207x700`, pencere açmadan; farklı kareler PNG olarak kalır, çıkış kodu 1. Çizimi
değiştirmemesi gereken işler (önbellek, culling) için.
- **Tarihçe:** ilk sürüm RGBA görüntüyü Pillow'un `getbbox()`'ına veriyordu; bu yalnız alfa kanalına bakar, kareler
  opak olduğu için her şey "aynı" çıktı ve kullanıcıya iki kez yanlışlıkla "174 kare piksel piksel aynı" dendi. Aynı gün
  RGB karşılaştırmasına çevrildi.
- **Düzeltilmiş koşu** (ikinci turdan önceki ve sonraki ikili, 174 kare): 171'i aynı. Dil ekle diyaloğu (koyu + açık)
  gerçek gerileme: kurucuda saklanan boyut bayatlıyor, satır başına ~0,6 MB fazla gösteriyordu → boyut yine çizimde
  hesaplanıyor (AUDIT SUB-05). Loglar demosunun açık karesi yalnız saatin saniyesi (demo o anki saati yazıyor).
  Düzeltmeden sonra araç yeniden koşmadı; arayüz cilası (D-133) çizimi bilerek değiştirdiği için bundan sonraki
  karşılaştırmalar tasarım farkını da gösterir.

# Üçüncü tur — 2026-11-11 (sabah, oturumun sonu)
Ölçülen ikili: oturumun son `dist` sürümü (09:22; D-134…D-141 dahil, D-135 geri alınmış hâli), Release.
Komut: `python -I tools/perf_measure.py after3 --config=release --only=render,cli --iso=build\lab\uup\out\x64-9448.iso`
(sonuç `build\lab\perf\after3.json`). Karşılaştırma: `baseline.json` (commit `1259c1e`) ve `after.json` (birinci tur).

**Koşullar — dikkat:** makine boşta değildi: sahibi aynı anda WinLove'u pencereli kullanıyordu ve bir WIM bağlıydı
(DISM işi sürüyor olabilir). Açılış ve sayfa başına bellek / boşta CPU (pencereli ölçümler) **bu yüzden yapılmadı**:
sahibinin ekranına pencere açmamak için. O tablolar birinci turdaki hâliyle kalır; yeni hâl ölçülmedi.

## Render (sürecin tamamı, 40 sayfa / demo durumu, her biri 3 koşu)
| Ölçü | Temel | Birinci tur | Üçüncü tur |
|---|---|---|---|
| Medyanların medyanı | 117 ms | 115,5 ms | 122 ms |
| Tepe çalışma kümesi, medyan | 101,9 MB | 101,9 MB | 107,1 MB |
| Windows indir sayfası | 899 ms | 495 ms | 117 ms |
| Diller sayfası | 129 ms | 125 ms | 146 ms |
| Sürücüler sayfası | 110 ms | 111 ms | 138 ms |
| `WinLove.exe` boyutu | 11,21 MB | 11,25 MB | 12,02 MB |

- Windows indir'deki düşüş üç koşuda da tutarlı (en çok 125 ms). Nedeni büyük olasılıkla gece isteklerinden "Seç…"
  gecikmesinin düzeltmesi (`WindowsDownloadController::m_appLists`, uygulama listeleri bir kez okunuyor); ayrıca
  doğrulanmadı.
- Geri kalan sayfalar ~5 ms (%5) yavaş ve ~5 MB fazla tepe bellekle çiziliyor. Bu turda arayüze çok şey eklendi
  (Özet, ayrıntı kartları, yeni metinler, VHD, güncelleme denetimi); farkın hangi parçadan geldiği **araştırılmadı**.
  Diller (+21 ms) ve Sürücüler (+27 ms) en çok artanlar; tek ölçüm turu ve meşgul makine, kesin değil.
- Bu sayılar sürecin açılışı + tek kare + PNG yazımıdır; canlı penceredeki kare süresi değildir.

## wlcli (sürecin tamamı, ms)
| Komut | Temel | Birinci tur | Üçüncü tur |
|---|---|---|---|
| help / info / ls / plan | 8–9 | 8–9 | 8–10 |
| `verify <iso>` (her akışın SHA-1'i) | 8.899 | 8.801 | 11.186 (en çok 16.255) |
| `icon-patch` 24 MB: süre / tepe WS | 583 ms / 289 MB | 648 ms / 197 MB | 587 ms / 173 MB |

- `verify` disk okuması; makine aynı anda bağlı imajla çalışırken ölçüldü, sapma büyük (11,1–16,3 sn). Gerileme
  diye okunmamalı; boş makinede yeniden ölçülmeli.
- `icon-patch` tepe belleği 289 → 173 MB (−%40) iki ölçümde de tutarlı.

## Hâlâ ölçülmeyenler
Açılış süresi ve sayfa başına bellek (pencereli), canlı pencerede kare süresi, takas zinciri tampon sayısı (2 → 3),
gerçek bağlı imajda ~1.300 satırlı Bileşenler ve Özet sayfalarının maliyeti, VHD'de Uygula süresi (laboratuvarda tek
koşu: 10 adım 38 sn, `build\lab\vhd\lab-run.log`).
