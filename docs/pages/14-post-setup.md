# P14 — Kurulum Sonrası

> Durum: 🟨 geliştirme bitti, kullanıcı testi bekliyor (2026-09-30). Tasarım: 12.

## 1. Amaç
Kurulum bittikten sonra kurulu sistemde çalışacak adımları sıralı bir listede toplamak: **winget** ile uygulama
kurma, **komut** çalıştırma, **dosya kopyalama**. Adımlar Uygula ile imaja yazılır; mekanizma D-026 ile aynı
(NTLite yolu): `SetupComplete.cmd` + ilk oturum.

## 2. Ekran
- Başlık eylemleri: **Komut ekle**, **Dosya ekle**, **Uygulama ekle** → türüne göre küçük bir form dialogu
  (winget: hazır uygulamalar listesi + ad + kimlik; komut: ad + komut + "Bitmesini bekle"; kopyala: ad + kaynak
  [Dosya… / Klasör…] + hedef klasör). Geçersiz adımda "Ekle" düğmesi kapalıdır.
- Araç satırı: **Çalıştırma** (İlk oturum (kullanıcı) · Kurulum sonu (SYSTEM)), **Hata olursa devam et**,
  sağda "7 adım · ~3 dk tahmini".
- Tablo: tutamaç · # · Adım (tür ikonu + ad) · Tür · Kaynak (mono; kopyada `kaynak → hedef`) · Bekle (etiket).
  - Enter / çift tık: düzenle · Delete: sil · **Alt+↑ / Alt+↓: taşı** · "Bekle" etiketine tık: komut adımında aç/kapat.
- "Kurulum sonu" modunda winget adımı varsa bilgi çubuğu: winget adımları yine ilk oturumda çalışır.
- Bağlı imaj yoksa boş durum. Nav rozeti: adım sayısı.

## 3. Model
- Tüm plan kuyrukta **tek işlem**: `SetPostSetup`, hedef `post-setup`, değer = planın JSON'u (sıra + seçenekler).
  Durum ayrı tutulmaz (`PostSetupController::plan()` işlemi okur); preset planı taşır; Uygula tek adımda yazar.
- Motor (`core/postsetup/PostSetup`): `applyPostSetup` imaja şunları yazar
  (`Windows\Setup\Scripts\WinLove\`):
  - `postsetup-machine.cmd` — `SetupComplete.cmd`'den çağrılır (SYSTEM, ilk oturumdan önce): "Kurulum sonu"
    modundaki komut / kopya adımlarını çalıştırır, ilk oturum görevi gerekiyorsa kaydeder.
  - `postsetup-user.cmd` — ilk oturumda bir kez, oturum açan kullanıcıyla ve hesabın en yüksek yetkisiyle çalışır
    (zamanlanmış görev: `postsetup-task.xml`, principal = `S-1-5-32-545` + `HighestAvailable`; dil bağımsız).
    winget adımlarından önce winget'in kullanıcıya kaydolması beklenir (en çok 5 dk). Sonunda görev kendini siler.
  - `files\<n>\…` — n. kopya adımının dosyaları (Uygula sırasında bu bilgisayardan imaja kopyalanır);
    kurulu sistemde `robocopy` ile hedefe taşınır.
  - Günlük: kurulu sistemde `%ProgramData%\WinLove\postsetup-machine.log` / `postsetup-user.log`.
- Betikler UTF-8 (`chcp 65001`); komutlar `cmd /d /s /c "…"` ile çalışır (kendi tırnakları korunur); adım adları
  `echo` için cmd özel karakterlerinden arındırılır; winget kimliği yalnız harf / rakam / `. - _ +`.
- "Hata olursa devam et" kapalıysa başarısız adımdan sonra kalanlar (ilk oturum görevi dahil) çalışmaz.
- CLI: `wlcli postsetup <plan.json> <mount>`. Testler: `tests/core/PostSetupTests.cpp`,
  `tests/app/PostSetupControllerTests.cpp`.

## 4. Sınırlar
- OEM ürün anahtarıyla etkinleşen makinelerde Windows `SetupComplete.cmd`'yi çalıştırmaz → adımların hiçbiri çalışmaz
  (D-026 ile aynı sınır).
- Komut satırındaki `%` işaretleri betik içinde genişler (`%SystemRoot%` çalışır; gerçek `%` için `%%`).
- "Bekle: Hayır" komutlarının çıktısı günlüğe girmez. winget ve kopya adımları her zaman beklenir.
- Sürükleyerek sıralama yok (tutamaç görsel; sıralama Alt+↑ / Alt+↓ ile).
- İlk oturum görevi yönetici olmayan ilk kullanıcıda yükseltilmeden çalışır (görev kendini silemez, sonraki
  oturumda yeniden dener).

## 5. Doğrulama durumu
- Üretilen makine betiği bu bilgisayarda **gerçekten çalıştırıldı** (scratch klasörde; `wlcli postsetup` + cmd):
  sıra, tırnaklı komut, başarısız adımın çıkış koduyla günlüğe yazılıp devam edilmesi, boşluklu / sonu `\` hedefe
  robocopy, beklemeyen komut, SetupComplete satırı doğru.
- **Denenmedi (VM gerekir):** `SetupComplete.cmd`'nin kurulum sonunda çağırması, `schtasks /create /xml` ile
  grup principal'li görev, ilk oturumda winget'in hazır olması.

## 6. Kabul
- [ ] Adım ekle / düzenle / sil / taşı; kuyrukta tek işlem, rozet = adım sayısı.
- [ ] Uygula → imajda `Windows\Setup\Scripts\WinLove\postsetup-*.cmd`, `files\`, SetupComplete satırı.
- [ ] VM: kurulum sonunda makine adımları, ilk oturumda kullanıcı adımları çalışır; günlükler yazılır.
- [ ] winget adımı ilk oturumda kurulur (ağ varken).

## 7. Görsel doğrulama / sapmalar
Render: `--demo-postsetup` (tasarım 12'nin adımları; dialoglar `--click-at` ile). Sütun konumları tasarımla örtüşüyor.
- "Çalıştırma" seçenekleri tasarımdaki tek "İlk oturum (SetupComplete)" yerine iki ayrı mod.
- "Bekle" winget / kopya adımlarında her zaman "Evet" (soluk); tasarımdaki "—" yok.
- Alt satırda kısayol ipucu, dialoglar ve bilgi çubuğu tasarımda yok.
