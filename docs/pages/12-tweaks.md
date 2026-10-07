# P12 — Ayarlar / Tweaks

> Durum: 🟨 geliştirme bitti, kullanıcı testi bekliyor (2026-09-30). Tasarım: 10.

## 1. Amaç
Windows ayarlarını "ayar" olarak göstermek (Reklam kimliği: açık/kapalı, Telemetri seviyesi: …) ve seçimi bağlı
imajın kayıt defteri / servis değişikliklerine çevirmek (kuyruk → P05 Uygula). P11 aynı değerleri "tweak listesi"
olarak gösterirdi; D-067 ile hazır tweak'lerin tek yeri burası (P11 yalnız kullanıcının kendi kayıtları).

## 2. Ekran
- Başlık eylemi: **Önerilenleri uygula** (katalogda önerilen seçeneği olan her ayarı toplu olarak o seçeneğe alır;
  kaç ayarın değiştiği toast ile bildirilir).
- Sekmeler (D-067): Gizlilik · Yapay zekâ · Uygulamalar · Performans · Görünüm · Gezgin · Başlat ve görev çubuğu ·
  Güncelleme · Güvenlik · Sistem (←/→).
- Bölüm başlığı (büyük harf, alt çizgi) + 32 px satırlar: 240 px etiket sütunu, sonra kontrol:
  **toggle** (yanında ipucu metni), **dropdown** (280 px), **radio** grubu.
- İçerik pencereye sığmazsa form kayar (tekerlek, kaydırma çubuğu; klavye odağı görünür alana getirilir).
- Bağlı imaj yoksa boş durum + "İmajlar'a git".
- Nav rozeti: varsayılandan farklı ayar sayısı.

## 3. Veri ve model
- Katalog: `resources/catalog/settings.json` (gömülü `IDR_CATALOG_SETTINGS`), `app/catalog/ImageSettingsCatalog`.
  Sekme → bölüm → ayar → seçenekler. Bir seçenek = kayıt defteri yazımları (+ servis başlangıçları, + imaja
  yazılan metin dosyaları: `"files": [{path, content}]` → `WriteFile` işlemi, D-040).
  Tam olarak bir seçenek "Windows varsayılanı"dır: **hiçbir şey yazmaz**.
- Durum ayrı tutulmaz; **kuyruktan türetilir** (`ImageSettingsController::current`): bir seçeneğin tüm işlemleri
  kendi değerleriyle kuyruktaysa o seçenek seçilidir, yoksa varsayılan. Böylece P11'de işaretlenen bir tweak P12'de,
  P12'de seçilen bir ayar P11'de ve presetlerde kendiliğinden görünür.
- Seçim: ayarın diğer seçeneklerinin kuyruktaki işlemleri çıkarılır, yeni seçeneğinkiler eklenir.
- `"apply": "firstLogon"` ayarları `SetRegistryFirstLogon` (D-026), diğerleri `SetRegistryValue`; servisler
  `SetServiceStart` (P10 ile aynı işlem → Servisler sayfasında da görünür).

### Başlat menüsü temizliği (D-040)
| Ayar | Kapalıyken ne yazılır | Hangi Windows |
|---|---|---|
| Sabitlenmiş uygulamalar ve kutucuklar | `HKLM\SOFTWARE\Microsoft\PolicyManager\current\device\Start\ConfigureStartPins = {"pinnedList":[]}` | 11 |
| | `Users\Default\AppData\Local\Microsoft\Windows\Shell\LayoutModification.xml` (kutucuksuz düzen) | 10 |
| Reklam uygulamalarının otomatik kurulumu | `CloudContent\DisableWindowsConsumerFeatures = 1` + varsayılan profilde `ContentDeliveryManager`: `SilentInstalledAppsEnabled`, `PreInstalledAppsEnabled`, `PreInstalledAppsEverEnabled`, `OemPreInstalledAppsEnabled`, `SystemPaneSuggestionsEnabled`, `SubscribedContent-338388Enabled` = 0 (ilk oturumda yeniden) | 10 + 11 |
| Widget'lar | `Dsh\AllowNewsAndInterests = 0` (11) + `Windows Feeds\EnableFeeds = 0` (10: Haberler ve ilgi alanları) | 10 + 11 |

İki değer de her imaja yazılır (katalog sürüme göre ayrılmıyor): diğer Windows'ta karşılığı olmayan değer / dosya
etkisizdir. Kullanıcı sonradan kendi sabitlemelerini yapabilir (düzen kilitlenmez).

### Yeni ayarlar (2026-09-30, D-041)
| Sekme › bölüm | Ayar | Ne yazılır |
|---|---|---|
| Başlat menüsü › Görev çubuğu | ~~Sabitlenmiş uygulamalar~~ → Başlat menüsü › Görev çubuğu sekmesi (D-083; OEM yolu 24H2+'da yalnız ekliyordu) | — |
| | Sohbet, toplantı ve Cortana düğmeleri | `TaskbarMn`, `ShowCortanaButton`, `People\PeopleBand` = 0; `HideSCAMeetNow` = 1 (ilk oturumda da) |
| Gizlilik › Copilot ve yapay zekâ | Copilot · Recall | `WindowsCopilot\TurnOffWindowsCopilot` (HKCU + HKLM), `ShowCopilotButton` = 0 · `WindowsAI\DisableAIDataAnalysis` = 1, `AllowRecallEnablement` = 0 |
| Gizlilik › Microsoft Edge | İlk çalıştırma · masaüstü kısayolu · arka planda çalışma | Edge ilkeleri (`HideFirstRunExperience`, `DefaultBrowserSettingEnabled`, `StartupBoostEnabled`, `BackgroundModeEnabled`), `EdgeUpdate\CreateDesktopShortcutDefault`, `DisableEdgeDesktopShortcutCreation` |
| Gizlilik › Şifreleme | Otomatik cihaz şifreleme (BitLocker) | `Control\BitLocker\PreventDeviceEncryption` = 1 |
| Güncelleme (yeni sekme) › Windows Update | Otomatik güncelleme (bildir / kapalı) · oturum açıkken yeniden başlatma · WU sürücüleri · özellik güncellemesi ertele (180 / 365 gün) · teslim iyileştirme | `Policies\…\WindowsUpdate(\AU)`, `DriverSearching\SearchOrderConfig`, `DeliveryOptimization\DODownloadMode` |
| Görünüm › Masaüstü | "Bu bilgisayar" · kullanıcı klasörü simgesi · **duvar kağıdı** | `HideDesktopIcons` · JPEG → `ProgramData\WinLove\wallpaper.jpg` + varsayılan profilde `Control Panel\Desktop\Wallpaper` |
| Görünüm › Kilit ekranı | **Kilit ekranı resmi** | JPEG → `ProgramData\WinLove\lockscreen.jpg` + `PersonalizationCSP` (ilke: kullanıcı değiştiremez) |
| Görünüm › OEM bilgisi | Üretici, model, destek sitesi / telefonu / saatleri | `CurrentVersion\OEMInformation` (yazılan metin) |

İki yeni denetim türü: **metin** (yazılan değer REG_SZ olarak katalogdaki anahtar / adlara) ve **dosya** (bu
bilgisayardaki bir JPEG, `CopyFile` işlemiyle imajdaki sabit yola + onu gösteren değerler). Boş değer = Windows
varsayılanı. Dosya kutusuna JPEG olmayan / olmayan bir yol yazılırsa hiçbir şey kuyruğa girmez, ipucu kırmızı.

### D-067 ile eklenenler (GitHub araştırması, 2026-10-05)
winutil, Win11Debloat, Winhance, Sophia Script, Optimizer, AtlasOS, ReviOS, xd-AntiSpy ve schneegans üreticisinden,
bizde olmayan ve yalnız kayıt değeri / servis başlangıcı olan 90 aday; kullanıcı hepsini seçti → 154 satır (katalog 283).
| Sekme | Öne çıkanlar |
|---|---|
| Gizlilik | Program Uyumluluk Yardımcısı / uyumluluk motoru, deneme özellikleri ve Insider, KMS telemetrisi, el yazısı verisi, dmwappush / tanılama hub servisleri, ayar / mesaj eşitleme, yakın paylaşım, cihazlar arası devam, pano geçmişi (dropdown), kamera / mikrofon / hesap / tanılama / sesle etkinleştirme izinleri, "kuruluşum yönetsin" istemi, eski çevrimiçi sihirbazlar, web'den yazdırma |
| Yapay zekâ | Click to Do, WSAIFabricSvc (elle / kapalı), yapay zekâ ajanları, model erişimi, Copilot kalıntıları, Not Defteri / Paint / Edge yapay zekâsı |
| Uygulamalar | Edge reklam ve telemetri, OneDrive klasör yedekleme teklifi, Office / Visual Studio / Chrome / Firefox / NVIDIA telemetrisi, varsayılan terminal, PowerShell yürütme ilkesi, geliştirici modu |
| Performans | Hızlı kapanış, uygulamaları zorla kapat, uykuda ağ, ön plan önceliği, NTFS son erişim / 8.3, Depolama Algısı, küçük resim önbelleği, otomatik bakım / uyandırma / zamanlanmış tanılama, MMCSS oyun önceliği, MPO, Xbox servisleri |
| Görünüm | Görsel efektler (en iyi performans), kaydırma çubukları, kilit ekranı / kilit ekranında kamera, uygulamaları yeniden aç, netplwiz kutusu, duvar kağıdı kalitesi, başlangıç sesi, sistem sesleri, görüşmede ses kısma |
| Gezgin | Onay kutuları, boş sürücüler, ayrı işlem, klasörleri geri yükle, açılır açıklamalar, paylaşım sihirbazı, ayrıntılı kopyalama, klasör türü algılama, bulut dosyaları, İndirilenler gruplaması, sürücü harfleri, çift çıkarılabilir sürücü, Bu Bilgisayar klasörleri, sağ tık temizliği / eklemeleri, "Birlikte aç", bozuk kısayol araması, düşük disk uyarısı |
| Başlat ve görev çubuğu | Telefon Bağlantısı paneli, "Tüm uygulamalar" görünümü / listesi, en çok kullanılan / son eklenen, güç menüsü, rozetler, yanıp sönme, masaüstünü göster, pencere paylaşma, zil, pil yüzdesi, simge boyutu, çoklu monitör, son etkin pencere; **Pencereler**: yaslama, Aero Shake, Alt+Tab'da Edge sekmeleri, paylaşma tepsisi |
| Güncelleme | "En son güncellemeleri hemen al", diğer Microsoft ürünleri, ölçülü bağlantı, yeniden başlatma bildirimi, "Güncelleştir ve kapat" |
| Güvenlik | Windows Güvenliği sayfaları, UAC seviyesi, ARSO, İnternet işareti (MotW), Akıllı Uygulama Denetimi, HVCI, VBS, LSA koruması, anonim erişim, WPBT |
| Sistem | Ayarlar'da gizlenen sayfalar (+ özel liste), M365 reklamları, Print Screen, düzen kısayolu, erişilebilirlik kısayolları, Caps Lock, dokunmatik klavye, yazma içgörüleri, üç bildirim, USB bildirimleri, varsayılan yazıcı, Yazdırma Biriktiricisi, sensör servisleri, üretici uygulamaları, SMB bant kısıtlaması, mavi ekran ayrıntısı / yeniden başlatma / döküm, UTC saati |

Kurallar: iki ayar aynı değeri yalnız bilerek yazar (birim testi izin listesiyle denetler); imajda olmayan servis atlanır
(anahtar oluşturulmaz); kurulumda sıfırlanabilen HKLM değerleri (Akıllı Uygulama Denetimi, HVCI, VBS) ilk oturumda da yazılır.
Kanıt: `tools\lab_settings_d067.ps1` — katalogdaki her ayar varsayılan dışı hâliyle gerçek imaja uygulanıp geri okunur.

### D-044 ile eklenenler
Sistem › **Ağ**: LLMNR, IPv6 (varsayılan / IPv4'ü tercih et / kapalı), Wi-Fi etkin noktalarına bağlanma, yeni ağda keşif
sorusu, parolasız (konuk) SMB, SMB imzalama zorunluluğu. Sistem › Diğer: klasik Windows Fotoğraf Görüntüleyicisi.
Güç planı ve güvenlik duvarı komut olduğu için Kurulum Sonrası › Hazır komutlar'da.

### D-043 ile eklenenler
Gezgin › **Sağ tık menüsü ve simgeler**: "Sahipliği al" (Yok / Türkçe / İngilizce), "Klasöre kopyala / taşı", "Paylaş".
Sistem › **Bildirimler**: uygulama bildirimleri, kilit ekranında bildirimler, bildirim sesleri, güvenlik ve bakım.
Performans › **Oyun**: Xbox Game Bar, Oyun modu, donanım hızlandırmalı GPU zamanlaması (varsayılan / açık / kapalı),
pencereli oyun iyileştirmeleri.

## 4. Sınırlar
- İmajdaki mevcut değer okunur (D-045): form imajın durumunu gösterir, ipucu "imajda" der. İmajdaki seçenekten
  "Windows varsayılanı"na dönmek yazılan değerleri siler; servis / dosya / silme içeren seçeneklerde geri dönüş yok
  ("burada geri alınamaz", denetim imajdaki konumda kalır). Yalnız silmelerden oluşan seçenek imajdan tanınmaz.
  Metin ayarlarında imajdaki dize yer tutucu olarak görünür; resim ayarları imajdan okunmaz.
- HKCU ayarları Default profile yazılır (kurulumdan sonra açılan hesaplar).
- "Güvenlik" telemetri seviyesi yalnız Enterprise / Education'da etkilidir (diğerlerinde "Gerekli" gibi davranır).

## 5. Kabul
- [ ] Ayar değiştir → kuyrukta ilgili işlemler, nav rozeti artar; varsayılana dönünce işlemler çıkar.
- [ ] P11'de karşılığı olan tweak işaretli görünür (ve tersi).
- [ ] Başlat menüsü sekmesi: "Sabitlenmiş uygulamalar ve kutucuklar" + "Reklam uygulamalarının otomatik kurulumu"
      + "Widget'lar" kapalı → Uygula → VM kurulumu: Windows 10'da Başlat'ta kutucuk yok, görev çubuğunda hava durumu
      yok; Windows 11'de sabitlenenler boş; reklam uygulamaları (Candy Crush, Spotify…) inmiyor.
- [ ] VM: görev çubuğunda yalnız Dosya Gezgini; Copilot / sohbet düğmesi yok; Edge sihirbazsız açılır; kurulumda
      BitLocker kendiliğinden açılmaz; duvar kağıdı, kilit ekranı resmi ve Sistem › Hakkında'daki OEM satırları
      seçilenler.
- [ ] "Önerilenleri uygula" → önerilen ayarlar seçilir; ikinci kez basınca "hepsi zaten seçili".
- [ ] Uygula → değerler hive'da (`wlcli reg` / yeniden bağlama ile), servis başlangıçları Servisler sayfasında.
- [ ] Klavye: Tab ile kontroller, ←/→ sekme ve radio, Space toggle, Enter dropdown.

## 6. Görsel doğrulama
Render: `--demo-tweaks` (tasarım 10'daki seçimlerle); `tools/compare_design.py 10-tweaks -- --demo-tweaks`.
Sekme çubuğu, bölüm başlığı, çizgi ve satır dikey ölçüleri tasarımla piksel olarak örtüşüyor (2026-09-30; dark,
light/EN, 1280×520'de kaydırma, tıklama ile toggle / radio / dropdown). `TabBar` seçili sekme zemini (bg.raised)
ve 8 px içerden alt çizgi tasarım 10 / 16'ya göre düzeltildi.

## 7. Bilinçli sapmalar
- İçerik tasarımdaki örnek 7 satır yerine gerçek katalog (283 ayar, 10 sekme); "Cortana ve arama" bölümü "Arama" oldu
  (Windows 11'de Cortana yok), konumun üçüncü seçeneği "Uygulama bazlı" yerine "Yalnız sistem" (HKCU izni kapalı).
- Her satırın yanında durumu yazar (D-032): toggle'da "Açık · Windows varsayılanı" / "Kapalı · değiştirilecek"
  (+ katalog ipucu), dropdown / radio'da yalnız işaret; değişenler vurgu renginde. Toggle, özelliğin kurulan
  Windows'taki durumudur (açık = özellik açık kalır) — sayfa açıklaması da bunu söyler.
- Sayfa içeriği diğer sayfalar gibi x=216'da başlar (tasarım 217).
