# Yol Haritası

Durum simgeleri: ⬜ başlamadı · 🟨 sürüyor · ✅ bitti (kullanıcı onaylı)
**Aynı anda yalnızca bir 🟨 olabilir.**

## Faz 0 — Temel (kod yazmadan önceki zemin)
| # | İş | Durum |
|---|---|---|
| 0.1 | git init, `.gitignore`, klasör iskeleti, `third_party` (doctest 2.4.12, json 3.12.0, pugixml 1.15) | ✅ |
| 0.2 | CMake + `CMakePresets.json` (x64-debug/release, Ninja, MSVC), `build.ps1` (VS Dev ortamı, -Test, -Gen) | ✅ |
| 0.3 | `tools/gen_tokens.py` → `Tokens.g.h`, `gen_icons.py` → `Icons.g.h` (115 ikon × 3 varyant), `gen_strings.py` → `StringKeys.g.h` (412 anahtar, TR/EN denetimi), `check_layers.py` | ✅ |
| 0.4 | Fontlar (IBM Plex Sans 400/500/600, JetBrains Mono 400, OFL) → `resources/fonts`; logo yazısı outline'a çevrildi, `WinLove.ico` (10 boyut) → `resources/brand` | ✅ |
| 0.5 | `wl_base`, `wl_core`, `wl_ui`, `wl_app`, `WinLove.exe` (ikon+manifest), `wlcli.exe`, `wl_tests` (15 test) Debug+Release yeşil | ✅ |

## Faz 1 — UI çatısı (tek sayfa yok, sadece iskelet)
| # | İş | Durum |
|---|---|---|
| 1.1 | Window: custom titlebar, caption butonları, Snap Layouts, DPI v2, min boyut | ✅ (Snap Layouts hover'ı kullanıcı elle doğrulamalı) |
| 1.2 | Render: D3D11/DXGI/D2D (flip swapchain), WARP yedeği, device lost, boşta %0 CPU (ölçüldü). DComp ve dirty-rect → 1.4 (D-011) | ✅ |
| 1.3 | Canvas (piksel hizalı 1px), Palette/kontrast, Text (gömülü fontlar, tipografik aile), Icons (SVG path → D2D, 116 ikon) | ✅ |
| 1.4 | `ui/widget`: Widget ağacı, Host (hover/press/capture/tıklama, Tab + roving focus, klavye odak halkası, tooltip), `Stack` yerleşimi, `Tween` animasyon (motion token'ları, hareketi azalt). DComp/dirty-rect hâlâ D-011 | ✅ |
| 1.5 | `--render=x.png` offscreen render, `tools/compare_design.py`, `tools/capture_window.py` (gerçek pencere). Sayfa seçimi (`--page=`) 1.7 ile gelecek | ✅ |
| 1.6 | `ui/widgets`: Button (4 tür, ikonlu, yalnız ikon), Label, Kbd, Splitter, EmptyState; tooltip Host'ta. **ScrollBar/ScrollView ilk kaydırma gereken sayfaya (P01/P02) ertelendi** | ✅ |
| 1.7 | `app/shell`: TitleBar (widget'lar + breadcrumb), NavRail (6 grup, rozet, daralma animasyonu, <1200'de otomatik daralma, roving focus, tooltip), StatusBar, PageView + sayfa kaydı (`pages/PageInfo`), yer tutucu sayfalar. Kısayollar: Ctrl+B, Ctrl+1…9, Ctrl+Shift+T/G, Ctrl+,. **Inspector paneli ilk kullanan sayfaya (P02), dil değiştirme P16'ya** | ✅ |
| 1.8 | Galeri sayfası (`--page=gallery`, Ctrl+Shift+G): tüm butonlar × rest/hover/pressed/disabled, Kbd, tipografi, Splitter, EmptyState | ✅ |

## Faz 2 — Motor temeli
| # | İş | Durum |
|---|---|---|
| 2.1 | `Result/Error`, `Log` (dosya + halka tampon), `TaskRunner`, `CancelToken`, `Progress`, UI dispatcher | ⬜ |
| 2.2 | Yetki: admin tespiti, "yönetici olarak yeniden başlat", SeBackup/SeRestore | ⬜ |
| 2.3 | `WimgApiBackend`: listImages; `IsoIO` okuma; `wlcli info/extract` | ⬜ |
| 2.4 | `DismApiBackend`: init, mount/unmount, mounts/cleanup, `mounts.json` kurtarma; `wlcli` komutları | ⬜ |
| 2.5 | `ops`: Operation, ChangeSet (undo/redo, JSON), Planner iskeleti, Applier iskeleti | ⬜ |
| 2.6 | Integration test altyapısı: `C:\WinLoveLab` hazırlama betiği, test ISO'sundan install.wim çıkarımı | ⬜ |

## Faz 3 — Sayfalar (sırayla, her biri tam döngü: `WORKFLOW.md` §2)
Sıra gerekçesi: önce imajı açmak, sonra en basit değiştirici sayfa ile **uçtan uca** (seç → değiştir → uygula → ISO → VM'de kur) hattı kurmak; zor sayfalar (Bileşenler) sağlam hattın üstüne gelir.

| # | Sayfa | Tasarım | Spec | Durum | Notlar |
|---|---|---|---|---|---|
| P01 | Kaynak (karşılama, sürükle-bırak, son kullanılanlar, canlı sistem) | 01, s1, s4 | `pages/01-source.md` | ⬜ | |
| P02 | İmajlar (index listesi, mount/unmount, export, sil, ESD→WIM) + mount ilerlemesi | 02, 03, s2, s3 | `pages/02-images.md` | ⬜ | |
| P03 | Loglar | 18 | `pages/03-logs.md` | ⬜ | Sonraki sayfaların hata ayıklamasını kolaylaştırır |
| P04 | Özellikler | 05 | `pages/04-features.md` | ⬜ | İlk ChangeSet kullanan sayfa |
| P05 | Uygula (özet, onay, çalışıyor, bitti) | 13, 13b, 14, 15 | `pages/05-apply.md` | ⬜ | Planner/Applier burada tamamlanır |
| P06 | ISO Oluştur / USB | 16 | `pages/06-iso.md` | ⬜ | Bittiğinde: uçtan uca VM kurulum testi |
| P07 | Bileşenler (paket + AppX + capability, katalog, bağımlılık, arama) | 04, 04b | `pages/07-components.md` | ⬜ | En büyük sayfa; alt adımlara bölünecek |
| P08 | Güncellemeler | 06 | `pages/08-updates.md` | ⬜ | |
| P09 | Sürücüler | 07 | `pages/09-drivers.md` | ⬜ | |
| P10 | Servisler | 09 | `pages/10-services.md` | ⬜ | OfflineRegistry burada gelir |
| P11 | Kayıt Defteri | 08 | `pages/11-registry.md` | ⬜ | |
| P12 | Ayarlar / Tweaks | 10 | `pages/12-tweaks.md` | ⬜ | |
| P13 | Katılımsız Kurulum | 11 | `pages/13-unattended.md` | ⬜ | |
| P14 | Kurulum Sonrası | 12 | `pages/14-post-setup.md` | ⬜ | |
| P15 | Presetler (kaydet/yükle/karşılaştır) | 17 | `pages/15-presets.md` | ⬜ | |
| P16 | Uygulama Ayarları | 19 | `pages/16-app-settings.md` | ⬜ | |
| P17 | Hakkında | 20 | `pages/17-about.md` | ⬜ | |
| P18 | Komut Paleti (Ctrl+K) | 21 | `pages/18-command-palette.md` | ⬜ | Tüm sayfaların `commands()`'ını toplar |

## Faz 4 — Sağlamlaştırma ve yayın
Canlı sistem (online) düzenleme, performans profili, erişilebilirlik (Narrator) turu, installer/portable paket, imzalama, sürüm notları.
