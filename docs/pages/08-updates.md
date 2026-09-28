# P08 — Güncellemeler

> Durum: 🟨 bitti, kullanıcı testi bekliyor (2026-09-28). Tasarım: 06.

## 1. Amaç
.msu / .cab güncelleme paketlerini bağlı imaja entegre etmek (kuyruk → P05 Uygula).

## 2. Motor
- `DismSession::addPackage` → `DismAddPackage(IgnoreCheck = FALSE, PreventPending = FALSE)`; DISM uygulanabilirliği
  kendisi denetler, uymayan paket adımda hata verir ve Uygula'da "atlandı" olarak raporlanır.
- `core::analyzeUpdate` (dosya adından; Microsoft katalog adları kararlı): KB, tür (SSU / LCU / .NET / diğer), hedef
  Windows (10/11), mimari, boyut. Unit test'li.
- Sıralama: Planner, Updates fazında `op.value` türüne göre SSU → LCU → .NET → diğer (kararlı sıralama).

## 3. Ekran
- Başlık: Klasör tara… (özyinelemeli *.msu/*.cab), Paket ekle… (çoklu seçim).
- 56 px kompakt DropZone (tıkla = Paket ekle); pencereye .msu/.cab sürükle-bırak bu sayfadayken kuyruğa ekler.
- Tablo: tutamak · Sıra · Paket · Tür · KB · Boyut · Durum (Uyumlu / Sürüm uyumsuz (Windows 10) / Mimari uyumsuz /
  denetlenemedi). Delete seçili paketi kuyruktan çıkarır.
- Nav rozeti: kuyruktaki paket sayısı.

## 4. Sınırlar
- Uyumluluk dosya adından; MSU içindeki metadata (applicability XML) okunmuyor — DISM son sözü söyler.
- Elle sıralama (sürükle) yok; sıra her zaman servis sırası.
- 24H2+ checkpoint güncellemeleri: DISM'in ilgili tüm MSU'ları aynı klasörde görmesi gerekir; klasör taraması bunu
  karşılar (hepsini ekle).

## 5. Kabul
- [ ] .msu sürükle / Paket ekle → listede doğru tür, KB, uyum.
- [ ] Uygula → paketler sırayla eklenir; uyumsuz paket "atlandı".
