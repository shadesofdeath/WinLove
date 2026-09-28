# P09 — Sürücüler

> Durum: 🟨 bitti, kullanıcı testi bekliyor (2026-09-28). Tasarım: 07.

## 1. Amaç
Bir klasördeki (ör. üreticinin sürücü paketi) INF sürücülerini sınıfa göre görüp seçilenleri bağlı imaja eklemek
(kuyruk → P05 Uygula).

## 2. Motor
- `core::scanDrivers(folder)`: özyinelemeli *.inf; her INF `parseInf` ile okunur (UTF-16 LE BOM / UTF-8 / ANSI).
  `[Version]` → Class, ClassGUID, Provider (`%strings%` çözülür), CatalogFile, DriverVer (tarih, sürüm);
  `[Manufacturer]` süslemeleri (NTamd64 / NTarm64 / NTx86) → mimariler (süsleme yoksa hepsi).
  Boyut = INF'in klasöründeki dosyaların toplamı / o klasördeki INF sayısı (yaklaşık, paylaşılan klasörler).
- `DismSession::addDriver` → `DismAddDriver(ForceUnsigned = FALSE)`; imzasız sürücü DISM hatası → Uygula'da "atlandı".
- Tarama okuyucu iş parçacığında (`reader()`), DISM kuyruğunu bekletmez. Unit test: `tests/core/DriverTests.cpp`.

## 3. Ekran
- Başlık: Klasör tara… (birden çok klasör eklenebilir; aynı INF yolu tekrar eklenmez).
- Araç çubuğu: arama (Ctrl+F), Sınıf, Mimari süzgeci; sağda "{f} klasör · {n} INF · {boyut}".
- Ağaç: sınıf satırı (üç durumlu onay, "Ağ (Net)" gibi dost ad, INF sayısı, toplam boyut) → INF satırları
  (Sağlayıcı · Sürüm, Boyut). Onay = `AddDriver` kuyruğu; nav rozeti kuyruktaki sürücü sayısı.
- Bağlı imaj yoksa "İmajlar'a git" boş durumu. Render: `--demo-drivers`.

## 4. Sınırlar
- İmajdaki mevcut 3. parti sürücüleri listeleme / kaldırma (DismGetDrivers / DismRemoveDriver) henüz yok.
- İmza/katalog doğrulaması yapılmıyor; ForceUnsigned seçeneği sunulmuyor.
- Mimari uyumsuz INF seçilebilir (süzgeç var ama engel yok); DISM reddeder.

## 5. Kabul
- [ ] Klasör tara → sınıflar ve INF'ler doğru sağlayıcı/sürümle listelenir.
- [ ] Seçilen sürücüler Uygula'da eklenir; imaj içinde `Windows\System32\DriverStore\FileRepository` altında görünür.
