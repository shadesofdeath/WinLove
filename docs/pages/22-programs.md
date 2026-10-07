# P22 — Programlar

**Durum:** 🟨 geliştirme bitti, gerçek pencerede denendi, VM'de ağla kuruldu; kullanıcı testi bekliyor (2026-10-07) ·
**Tasarım:** handoff'ta karşılığı yok — mevcut sayfaların dili (Bileşenler / Uygulamalar tablosu + sağ inceleme sütunu).
Karar: D-078.

## 1. Amaç
Kurulan Windows'a ilk oturumda internetten kurulacak programları seçmek (Ninite gibi), winget'in bütün deposundan
(~15 400 paket, canlı veri). Kurulum ilk oturumda WinLove'un kendi penceresinde, tek tek, görünür biçimde olur; internet
yoksa pencere bağlantıyı bekler (sonraki oturuma ertelenmez).

## 2. Görünüm bölgeleri
| Bölge | İçerik |
|---|---|
| Başlık eylemleri | **Kurulum penceresini önizle** (bu bilgisayarda, hiçbir şey kurmadan), **Listeyi yenile** (dizini şimdi indir) |
| Araç çubuğu | Arama ("/", bütün depo, ≥ 2 harf, en iyi 200), **Kategori** (Tümü · 12 kategori · Bütün depo), **Yalnızca seçili**, sağda "N program seçili · ilk oturumda kurulur" |
| Hazır paketler | 6 kart (Temel, Oyuncu, Geliştirici, Ofis ve okul, İçerik üretici, Gizlilik): simge, ad, "9 program" / "4 / 9 seçili" / "Hepsi seçili" (vurgu çerçevesi + tik). Tık / Space / Enter: eksikleri ekler; hepsi seçiliyse çıkarır (tek kuyruk düzenlemesi). İpucu = açıklama. Dar alanda kartlar eşit satırlara bölünür |
| Tablo | Program (onay kutusu, simge ya da harf rozeti, ad; aramada eşleşme vurgusu) · Kimlik (mono) · Sürüm (mono, sağa). Gruplar: kategoriler; "Tümü"de her kategorinin öne çıkanları + "→ N program daha — hepsini göster" satırı (kategoriyi açar). Bir kategori: "Öne çıkanlar" + "Depodaki diğerleri" (winget etiketleri). "Bütün depo": hepsi, ada göre. Katalogda olmayan seçimler "Seçtiklerin" grubunda en sonda |
| Sağ sütun (ProgramInspector) | Simge 32, ad, yayımcı, kimlik; Açıklama; Ayrıntılar (Sürüm, Lisans, Kurulum türü · kapsam, Mimari); Etiketler (Store ürün kimlikleri elenir); **Ekle / Çıkar**, **Ana sayfa** (yalnız https) |
| Bant | App Installer (winget) Uygulamalar'da kaldırılacaksa ve seçim varsa kırmızı: programlar kurulamaz |

## 3. Veri
| Veri | Kaynak | Ne zaman | Önbellek |
|---|---|---|---|
| Paket dizini | `cdn.winget.microsoft.com/cache/source2.msix` → imza (Microsoft Corporation) → `Public\index.db` (SQLite, winsqlite3) | sayfa ilk açılınca, kendi iş parçacığında | `<çalışma>\winget`, 24 saat |
| Kategoriler, paketler | `resources/catalog/programs.json` (yalnız kimlik + etiket; ad / sürüm dizinden) | gömülü kaynak | — |
| Ayrıntılar, simge | `versionData.mszyml` → birleşik manifest (SHA-256'lar dizinden) → `IconUrl` (`IconSha256`) | satır ekranda / seçilince, birer birer, en yenisi önce (en çok 64 bekleyen) | `<çalışma>\winget\…` |
| Seçimler | Kurulum Sonrası planı `PostSetupPlan::programs` (+ pencere metinleri) | kuyruk | preset taşır |

## 4. Aksiyonlar
| Aksiyon | Tetikleyici | Etki | Admin? | Geri alınır? |
|---|---|---|---|---|
| Program seç / çıkar | onay kutusu, çift tık, Enter, inceleme **Ekle/Çıkar** | `SetPostSetup` (tek işlem) | Uygula'da | Ctrl+Z |
| Paket seç / çıkar | kart | aynı işlem, tek düzenleme | Uygula'da | Ctrl+Z |
| Önizle | başlık eylemi | `%TEMP%\WinLove\programs-preview` + dryRun penceresi | hayır | — |
| Listeyi yenile | başlık eylemi | dizin yeniden indirilir | hayır | — |

## 5. Durumlar
- Boş (bağlı imaj yok): "Önce bir imaj bağla" + İmajlar'a git.
- Yükleniyor: dönen simge, "winget deposu indiriliyor".
- Hata: ileti + bağlam, **Yeniden dene**.
- Arama sonuçsuz: sağda "Sonuç yok".

## 6. Motor ve `wlcli`
- `core/programs/Winget` (`WingetIndex`, `refreshWingetIndex`, `fetchWingetDetails`, `fetchWingetIcon`), `Yaml` (alt küme).
- `core/postsetup`: `programs.ps1` (WPF penceresi), `programs.json`, "WinLove Programs" görevi.
- `wlcli programs-index [--cache] [--refresh]`, `programs-search <metin> [--limit] [--json]`, `programs-show <kimlik> [--locale]`.
- `tools/check_programs.py`: katalogdaki her kimlik ve etiket dizinde var mı.

## 7. Kabul kriterleri
- [x] Dizin imzası denetlenir; bozuk / imzasız dosya reddedilir (birim testi + gerçek indirme).
- [x] Arama, kategori, Bütün depo, Yalnızca seçili, paketler gerçek pencerede (gui.py) çalışır.
- [x] Seçim tek kuyruk işlemi; Kurulum Sonrası adımlarıyla birlikte yaşar (birim testi).
- [x] VM: 6 programın 5'i ağla ilk oturumda kuruldu; yöneticiyi reddeden Spotify artık yükseltilmeden kuruluyor (STATUS).
- [ ] VM: internet yokken pencere bekler, bağlantı gelince devam eder.
- [ ] Kullanıcı onayı.

## 8. Kullanıcı test senaryosu
1. Bir sürüm bağla → Programlar. Arama: "chrome", "vlc"; Kategori → Tarayıcılar → Depodaki diğerleri.
2. "Oyuncu" kartı → 9 program seçili; tekrar tık → çıkar. Ctrl+Z.
3. **Kurulum penceresini önizle** → pencere (hiçbir şey kurulmaz).
4. Uygula → ISO → VM / gerçek kurulum: ilk oturumda "Programların kuruluyor" penceresi.
