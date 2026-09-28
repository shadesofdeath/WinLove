# LogConsole · KeyValue inspector satırı · Diff görünümü · Command Palette · DropZone
## LogConsole
Zemin bg.input, 1px line.subtle, padding 8 12; satır 20 (mono 11); sütunlar: zaman (text.tertiary, 8 char) · seviye (5 char, medium; DBG text.tertiary, INFO status.info, WARN status.warning, ERR status.error) · kaynak (text.secondary) · mesaj (text.secondary; ERR satırı text.primary + status.errorSubtle tam satır zemini). Filtre: seviye dropdown, kaynak dropdown, arama (Ctrl F, eşleşme accent.subtle). Otomatik kaydır toggle; kullanıcı yukarı kaydırınca kapanır, "↓ n yeni satır" 20px chip alt ortada. Ctrl+C seçili satırları kopyalar; sağ tık: kopyala, dışa aktar, temizle. Canlı imleç: 7×14 accent blok.
## KeyValue satırı (inspector)
24px; anahtar 11px text.secondary genişlik 96; değer 11px text.primary (mono ise JetBrains Mono); risk değeri 6×6 kare + etiket; durum değeri ikon 16 + etiket. Bölümler 8px dikey padding, 1px line.subtle; bölüm başlığı type.section 24px ("BAĞIMLILIKLAR · 3").
## Diff (preset karşılaştırma)
Satır 24, sol 2px durum çubuğu (+ success / − error / ~ warning), 18px mono işaret aynı renk, kategori text.secondary 132, öğe esnek, A ve B sütunları 140 (yok ise "—" text.tertiary). Üstte A/B dropdown'ları ve özet "+9 · −26 · 4". Yalnızca farklar; "Aynıları göster" toggle'ı.
## Command Palette (Ctrl K)
Scrim + 560 genişlik, üstten 120; bg.overlay, 1px line.strong, r3, elevation.dialog. Arama satırı 40px (ikon 16, 12px input, kbd Esc), alt 1px line.subtle. Gruplar: type.section başlık; Sonuçlar 32px (ikon + ad + caption alt satır, seçili bg.raised + kbd ↵), Komutlar 24px (ikon + ad + kbd). ↑↓ gezinme, Enter çalıştır, Esc kapat; yazarken 60 ms debounce; eşleşme mark'ı accent.subtle. Boş sonuç: EmptyState kısa.
## DropZone
1px line.strong, r2, ortalı ikon 24 (download, text.tertiary) + bodyStrong + caption. Sürükleme üzerinde: sınır accent, zemin accent.subtle, ikon accent. Geçersiz tür: sınır status.error, metin "desteklenmeyen dosya". Kompakt 56px yatay varyant (güncellemeler).
