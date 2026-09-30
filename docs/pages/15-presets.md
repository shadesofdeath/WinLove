# P15 — Presetler

> Durum: 🟨 geliştirme bitti, kullanıcı testi bekliyor (2026-09-30). Tasarım: 17.

## 1. Amaç
Bir ayar kümesini (kuyruk + katılımsız kurulum yanıtları) adla kaydetmek, sonra başka bir imaja yüklemek, dosya
olarak paylaşmak ve iki kümeyi karşılaştırmak.

## 2. Ekran
- Başlık eylemleri: **İçe aktar…** (bir `.wlpreset` / `.json` dosyasını kitaplığa alır), **Yeni preset** (ad sorar;
  geçerli kuyruğu ve yanıt dosyasını kaydeder).
- **Kitaplık** (sol, 360 px): Preset · Değişiklik (adlandırılmış öğe sayısı). Altında **Yükle** · **Dışa aktar** · **Sil**.
  Enter / çift tık = Yükle, Delete = Sil (onay ister). Seçilen preset karşılaştırmanın A'sı olur.
- **Karşılaştır** (sağ): A ve B (her preset ya da **Geçerli kuyruk**), yer değiştir düğmesi, **Aynıları göster**,
  özet "+9 eklendi · −26 kaldırıldı · 4 değişti". Satırlar: işaret (+ yeşil = yalnız B'de, − kırmızı = yalnız A'da,
  ~ amber = ikisinde farklı) · Kategori · Öğe · A · B ("—" = yok). Kategoriler menü sırasıyla.

## 3. Model
- **Dosya** (`app/state/Preset`): ChangeSet JSON'u + `name` + isteğe bağlı `unattend` (`includeInIso`, `xml` —
  yanıt dosyası kendi XML'iyle taşınır). Eski okuyucular ve `wlcli apply` dosyayı ChangeSet olarak okumaya devam eder;
  adsız düz ChangeSet dosyaları da preset olarak içe aktarılır (ad = dosya adı).
- **Kitaplık**: `%LOCALAPPDATA%\WinLove\presets\*.wlpreset` (çalışma klasörü taşınsa da burada kalır); ada göre sıralı.
  Aynı adla kaydetmek eskisinin yerine yazar.
- **Öğeler** (`PresetController::items`): bir presetin değişiklikleri okunur adlarla —
  P12 ayarı olan kayıt / servis işlemleri "Reklam kimliği: Kapalı" gibi tek öğe; kalan kayıt değerleri ham;
  AppX paket kimliğiyle; kurulum sonrası planı adım adım; yanıt dosyasının yanıtlanmış alanları (parola ve ürün
  anahtarı yalnızca "ayarlı" olarak).
- **Fark**: öğeler anahtarla eşleşir; bir tarafta ayar olarak, diğer tarafta düz işlem olarak duran aynı değer tek
  "değişti" satırıdır.
- **Yükle**: işlemler kuyruğa **eklenir** (kuyruktakiler kalır; aynı yuvadakiler presetin değeriyle değişir),
  yanıt dosyası presetinkiyle değişir. Kuyruk bağlı imaja ait olduğu için işlem içeren preset mount ister;
  yalnız yanıt dosyası taşıyan preset mount'suz yüklenir.
- Bileşenler sayfasındaki "Preset yükle" ve Uygula sayfasındaki "Preset olarak kaydet" aynı dosya biçimini kullanır.

## 4. Sınırlar
- Parola preset dosyasında yanıt dosyasındaki gibi (Base64, şifreli değil) durur: paylaşırken dikkat.
- Sürücü / güncelleme işlemleri dosya yolu taşır; preset başka bir bilgisayarda o yolları bulamayabilir.
- Yeniden adlandırma yok (dışa aktar → içe aktar ya da yeniden kaydet).
- "Yükle" kuyruğu değiştirmez, üstüne ekler (temiz başlangıç için önce imajı yeniden bağla).

## 5. Kabul
- [ ] Yeni preset → kitaplıkta görünür; uygulama yeniden açılınca hâlâ orada.
- [ ] Yükle (başka bir imaj bağlıyken) → kuyruk ve Katılımsız Kurulum formu dolar; P12 / P14 sayfaları presetle aynı.
- [ ] Dışa aktar → dosya; başka bir kitaplıkta İçe aktar → aynı preset.
- [ ] Karşılaştır: A / B / Geçerli kuyruk; özet ve satırlar doğru; "Aynıları göster".
- [ ] Sil → onay → dosya gider.

## 6. Görsel doğrulama / sapmalar
Render: `--demo-presets` (bellekte tasarım 17 benzeri kitaplık; kullanıcının kitaplığına dokunmaz).
- Liste altındaki Yükle / Dışa aktar / Sil düğmeleri ve "Geçerli kuyruk" seçeneği tasarımda yok.
- Liste ve tablo başlıkları diğer tablolar gibi (büyük harf değil).
- "Aynıları göster" bileşen notunda var (diff bölümü), ekran SVG'sinde yok.
