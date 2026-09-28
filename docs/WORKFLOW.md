# Çalışma Yöntemi

## 1. Oturum protokolü (her AI oturumu)
**Başlangıç**
1. `CLAUDE.md` → `docs/STATUS.md` → aktif sayfa spec'i (`docs/pages/NN-*.md`) oku.
2. `./build.ps1 -Test` çalıştır; kırıksa önce onu düzelt.
3. STATUS'taki "Bir sonraki adım"ı kullanıcıya bir cümleyle söyle ve başla.

**Bitiş**
1. Build + testler yeşil (değilse STATUS'a açıkça yaz).
2. Mount kalmadı (`wlcli mounts` boş).
3. `docs/STATUS.md` güncelle; yeni öğrenilen motor bilgisini `ENGINE.md`'ye, alınan kararı `DECISIONS.md`'ye yaz.
4. Aktif sayfa spec'indeki kabul kriteri kutucuklarını güncelle.

## 2. Sayfa döngüsü (her sayfa bu 8 adımdan geçer, sırayla)

| # | Adım | Çıktı | Kim onaylar |
|---|---|---|---|
| 1 | **Spec** | `docs/pages/NN-*.md` (`_TEMPLATE.md`'den): amaç, veri, aksiyonlar, durumlar, gereken motor API'si, gereken widget'lar, string anahtarları, kabul kriterleri | **Kullanıcı** |
| 2 | **Motor** | `core` API + unit test + `wlcli` komutu | Integration testi test ISO'sunda geçer |
| 3 | **Widget'lar** | Eksik widget'lar `ui/widgets`'a; dev galeri sayfasında tüm state'leriyle görünür | Galeri render'ı `components-gallery.html` ile karşılaştırılır |
| 4 | **Görünüm** | Sayfa `app/pages` altında; gerçek veriyle bağlı | — |
| 5 | **Görsel doğrulama** | `--render-page` PNG'leri (dark/light, %100/%150) + tasarım SVG karşılaştırması | Fark raporu `docs/pages/NN-*.md`'ye |
| 6 | **Durumlar** | boş / yükleniyor / hata / admin gerekli / iptal; klavye gezinmesi; TR/EN | Kontrol listesi |
| 7 | **Gerçek test** | Kullanıcı test senaryosunu uygulamada çalıştırır (spec'te yazılı) | **Kullanıcı** |
| 8 | **Kapanış** | ROADMAP'te ✅, STATUS güncel, commit (kullanıcı isterse) | **Kullanıcı** |

Bir adım bitmeden sonrakine geçilmez. Döngü sırasında başka sayfaya ait iş çıkarsa ROADMAP'teki o sayfanın "notlar"ına yazılır, **yapılmaz**.

## 3. Bitti Tanımı (Definition of Done) — sayfa için
- [ ] Spec'teki tüm kabul kriterleri ✅
- [ ] Motor işlemleri `wlcli` ile test ISO'sunda doğrulandı; integration testi var
- [ ] Unit testler var ve geçiyor
- [ ] Dark + Light + High Contrast render'ları tasarımla uyumlu (fark raporunda açıklanmamış sapma yok)
- [ ] %100, %150, %200 ölçeklemede keskin
- [ ] Boş/yükleniyor/hata durumları çalışıyor; uzun işlemler iptal edilebilir; UI donmuyor
- [ ] Tamamen klavyeyle kullanılabiliyor, focus ring görünür
- [ ] Tüm metinler string anahtarlarından; TR ve EN eksiksiz
- [ ] Log kayıtları anlamlı (Loglar sayfasında okunabilir)
- [ ] Kullanıcı gerçek imajda denedi ve onayladı

## 4. Hata ayıklama sırası
Motor sorunu → `wlcli` ile yeniden üret → log → `ENGINE.md` saha notu. UI sorunu → `--render-page` ile yeniden üret → düzelt → tekrar render. Tahminle "düzelttim" deme; yeniden üretip doğrula.
