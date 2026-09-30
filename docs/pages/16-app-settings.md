# P16 — Uygulama Ayarları

> Durum: 🟨 geliştirme bitti, kullanıcı testi bekliyor (2026-09-30). Tasarım: 19. Erişim: `Ctrl+,`.

## 1. Amaç
WinLove'un görünümünü, dilini ve çalışma klasörlerini ayarlamak. Her değişiklik anında `settings.json`'a yazılır
(`%LOCALAPPDATA%\WinLove\settings.json`) ve hemen uygulanır.

## 2. Ekran
- Başlık eylemi: **Varsayılanlara dön** (görünüm + dil her zaman; klasörler yalnız değiştirilebiliyorsa).
- **GÖRÜNÜM**
  - Tema: Koyu · Açık · Yüksek kontrast · Sistem. "Sistem": Windows yüksek kontrasttaysa HC, değilse uygulama
    açık / koyu modu; Windows ayarı değişince canlı izlenir. `Ctrl+Shift+T` de ayarı değiştirir.
  - Vurgu rengi (D-042): ekran 19'daki beş kare — Bakır (varsayılan) · Deniz · Nar · Gök · Zeytin; 20×12, seçili
    olanda 1 px text.primary halka, yanında adı; tıkla ya da ←/→. Anında uygulanır, ayar dosyasına `accent` yazılır.
    Yüksek kontrast teması kendi vurgusunu korur. `--accent=` render için.
  - Hareketi azalt: açık = her zaman; kapalı = "Sistem ayarını izle" (Windows "animasyonları göster").
- **DİL** — Arayüz dili: Türkçe · English. Seçince arayüz o dilde yeniden kurulur (durum — bağlı imaj, kuyruk,
  yanıtlar — korunur). Bir iş sürerken değiştirilemez.
- **ÇALIŞMA ORTAMI**
  - Çalışma klasörü (ISO'ların açıldığı yer) ve Bağlama klasörü (boş = çalışma klasöründe `\mount`): yol kutusu +
    klasör seç düğmesi. Yazılan yol **Enter** ile uygulanır (yarım yazılmış yol klasör olmasın); tam yol değilse
    kırmızı ipucu. Bağlı imaj ya da süren iş varken kilitli.
  - DISM yolu: motorun yüklediği `dismapi.dll` ve sürümü (salt okunur — D-017: DISM sistemden yüklenir).

## 3. Model
- `AppSettings`: `theme`, `accent`, `reduceMotion`, `language`, `workRoot`, `mountFolder`, `isoFolder`. Eksik / bozuk alanlar
  alan alan varsayılana düşer (eski sürüm dosyaları okunur).
- `AppState::setSettings` → kaydet + `Change::Settings` → Shell → `App::applySettings`: tema (pencere çerçevesi
  dahil), hareket, dil (yeniden kurma `post` ile: değişiklik yok edilecek bir kontrolün içinden gelir).
- Komut satırı `--theme=` / `--lang=` ayarların önüne geçer; `--render` ayar dosyasını hiç okumaz.
- Preset kitaplığı çalışma klasörüyle taşınmaz: hep `%LOCALAPPDATA%\WinLove\presets`.

## 4. Bu sürümde olmayanlar (tasarım 19'da var)
- ~~Vurgu rengi~~ — eklendi (D-042, kullanıcı isteği): diğer dört rengin hover / pressed / subtle / açık tema
  değerleri tasarımda yoktu, türetildi (sapma kaydı D-042'de). "özel…" renk seçici yok.
- **Yoğunluk** (Comfortable 28 px): kontrol / satır ölçüleri derleme zamanı sabiti; çalışma zamanı yoğunluğu tüm
  widget'lara dokunan ayrı bir iş.
- **İşlem sonrası mount'u çöz**: motor destekliyor (`ApplyJobOptions::commitAndUnmount`), ama Uygula sayfasının
  özet / bitiş ekranları "kaydedildi ve çözüldü" varsayıyor; P05 kullanıcı testinden sonra eklenmeli.

Sayfa artık sol menüde de var (son grupta "Uygulama ayarları"); önceden yalnız `Ctrl+,` ve komut paletiyle
açılıyordu ve kullanıcı tema ayarını bulamadı (D-042).

## 5. Kabul
- [ ] Tema değiştir → pencere hemen değişir; uygulama yeniden açılınca aynı tema. "Sistem" Windows'u izler.
- [ ] Dil değiştir → arayüz o dilde; bağlı imaj / kuyruk yerinde; yeniden açılınca aynı dil.
- [ ] Çalışma klasörünü değiştir (imaj bağlı değilken) → sonraki ISO açma / mount yeni klasörü kullanır.
- [ ] İmaj bağlıyken klasör kutuları kilitli ve nedeni yazıyor.
- [ ] Varsayılanlara dön.

## 6. Görsel doğrulama
Render: `--page=settings` (dark, light/EN + bağlı imaj); çalışma zamanı dil değişimi `--switch-lang=en` ile
(durum korunarak) doğrulandı. Bölüm başlığı / satır ölçüleri tasarımla örtüşüyor.
