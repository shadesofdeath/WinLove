# DataGrid
- Sütun başlığı 24px: 11px medium `text.tertiary`; sıralı sütunda text.primary + 12px ok (chevron-up/down); tık = sırala, ikinci tık ters, üçüncü kaldır. Alt 1px line.subtle.
- Satır 24 (28 comfortable), ayırıcı 1px line.subtle (tablolarda), radius yok. hover bg.raised · selected accent.subtle · focused 1px accent inset. Çoklu seçim: checkbox sütunu 28px.
- Hücre tipleri: metin (body), mono (11, sağa hizalı sayı), durum (ikon 16 + 11px), toggle (24×12 ortada), dropdown kompakt (20px), tag (16px, 1px line.strong, r1), drag-handle (16, text.tertiary, sürükleyerek sıralama), progress (2px).
- Sütun yeniden boyutlandırma: başlık sınırında 6px hit alanı, hover 1px line.strong dikey çizgi, sürüklerken accent; min 48. Çift tık = içeriğe sığdır.
- Sanal kaydırma; yatay taşmada son sütun sabit değil, tablo kayar; ilk sütun 200 min.
- Boş: EmptyState hücre alanında. Yükleme: skeleton satırları (10px bar bg.raised).
