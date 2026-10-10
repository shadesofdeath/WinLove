<p align="center">
  <img src="resources/brand/app-icon-256.png" width="96" alt="WinLove">
</p>

<h1 align="center">WinLove</h1>

<p align="center">
  Windows imajlarını istediğin gibi hazırla: bileşen kaldır, ayarla, temizle, ISO'ya dök.<br>
  <b>Tamamen ücretsiz · açık kaynak · Türkiye'de sevgiyle yapıldı 🇹🇷</b>
</p>

<p align="center">
  <a href="https://github.com/shadesofdeath/WinLove/releases"><img alt="Sürüm" src="https://img.shields.io/github/v/release/shadesofdeath/WinLove?include_prereleases&label=s%C3%BCr%C3%BCm"></a>
  <a href="LICENSE"><img alt="Lisans" src="https://img.shields.io/badge/lisans-GPL--3.0-blue"></a>
  <img alt="Windows 10 / 11 x64" src="https://img.shields.io/badge/Windows-10%20%2F%2011%20x64-0078d4">
</p>

---

> **1.2.1 Beta.** Değişiklikleri yine önce bir sanal makinede dene; asıl imajının yedeğini tut. Neler değişti:
> [CHANGELOG](CHANGELOG.md).
> WinLove yalnızca **çevrimdışı imajlarla** çalışır (ISO / WIM / ESD); çalışan sistemine dokunmaz.

![WinLove](docs/screenshots/components.png)

## Neler yapar

| | |
|---|---|
| **Windows indir** | İstediğin Windows 10 / 11 derlemesini Microsoft'un güncelleme sunucularından indirir (SHA-256 doğrulamalı), Mağaza uygulamalarını seçerek ISO'ya dönüştürür. |
| **Kaynak ve imajlar** | ISO, WIM, ESD açar; sürümleri listeler, bağlar, dışa aktarır, siler; ESD / "lite" solid WIM → WIM; imaj sağlığını denetler ve onarır. |
| **Bileşenler ve uygulamalar** | Hazır gelen uygulamaları ve sistem bileşenlerini kaldırır (Edge, OneDrive, WinRE, eski sürücüler…); bağımlılıkları ve riski önceden söyler, uyumluluk korumalarıyla gerekenleri tutar; WinSxS'i en aza indirir. |
| **Windows'un kendiliğinden kurdukları** | Yeni Outlook, Teams, Dev Home, Telefon Bağlantısı, Microsoft 365 Copilot, Copilot ve OneDrive'ın kurulumdan sonra kendiliğinden gelmesini kökünden durdurur (Windows Update zamanlayıcısı, yedek paketler, "istenmiyor" işaretleri — sanal makinede ölçüldü). |
| **Başlat menüsü ve görev çubuğu** | Windows 11 Başlat'ını reklamsız ve boş başlatır ya da kendi uygulama listeni sabitler; görev çubuğu sabitlemelerini de — Home dahil her sürümde. |
| **Programlar** | winget deposunun tamamından (~15.400 paket) ya da hazır paketlerden (Temel, Oyuncu, Geliştirici…) seç; ilk oturumda kendi penceresinde kurulur — istersen kurulumlar imaja gömülür ve internetsiz kurulur; bu bilgisayarın programlarını da alır. |
| **Ayarlar / Tweaks** | 280+ ayar on sekmede: gizlilik, yapay zekâ, uygulamalar, performans, görünüm, Gezgin, Başlat ve görev çubuğu, güncellemeler, güvenlik, sistem — ya da hepsi bu bilgisayardaki gibi. |
| **Özellikler, güncellemeler, sürücüler, diller** | İsteğe bağlı özellikler; Microsoft Update Kataloğu'ndan toplu güncelleme indirme ve ekleme; sürücü ekleme / kaldırma; dil paketleri. |
| **Kayıt defteri, servisler, görevler, hosts** | Kendi kayıt değerlerin ve .reg dosyaların; servis başlangıç türleri; zamanlanmış görevleri kapatır ya da kendi yinelenen görevini ekler; hosts listeleri. |
| **Simgeler ve kişiselleştirme** | Windows'un kendi simge dosyalarındaki simgeleri yedekleyerek değiştirir, 7TSP simge paketlerini (.7z / .zip) tek seferde uygular; duvar kâğıdı, OEM bilgileri, yazı tipleri. |
| **Katılımsız kurulum ve sonrası** | `autounattend.xml` (yerel hesap, TPM / Secure Boot atlama, OOBE), kurulumdan sonra çalışacak uygulamalar, Wi-Fi, güç planı, betikler. |
| **Kendi kurulum ekranı** | Windows'un OOBE sayfaları yerine WinLove'un Windows 11 görünümlü sihirbazı: ağ, hesap, bilgisayar adı, tema ve vurgu, duvar kâğıdı, uygulama paketleri, tercihler, gizlilik — 15 dilde, kurulan Windows'un dilinde; sonra doğrudan masaüstü. |
| **ISO ve USB** | Önyüklenebilir ISO (UEFI / BIOS) ya da USB bellek; kurulum ortamını (WinRE, boot.wim, kurulum dosyaları) da günceller; Secure Boot 2023 medyası; desteklenmeyen bilgisayarda yerinde yükseltme; Win10 + Win11 tek ISO; presetlerle aynı ayarları tekrar uygula. |

<p>
  <img src="docs/screenshots/startmenu.png" width="49%" alt="Başlat menüsü">
  <img src="docs/screenshots/tweaks.png" width="49%" alt="Ayarlar / Tweaks">
</p>
<p>
  <img src="docs/screenshots/icons.png" width="49%" alt="Simgeler">
  <img src="docs/screenshots/about.png" width="49%" alt="Hakkında">
</p>

## Kurulum

1. [Sürümler](https://github.com/shadesofdeath/WinLove/releases) sayfasından `WinLove.exe`'yi indir.
2. Çalıştır — kurulum yok, tek dosya. İmaj bağlamak için yönetici izni ister.

**Gereken:** Windows 10 veya 11, x64. İşlenen imajlar x64 ya da ARM64 olabilir.

## Söz

WinLove tamamen ücretsizdir ve her zaman ücretsiz kalacak: reklam yok, hesap yok, ücretli sürüm yok.
Hiçbir kullanım verisi toplamaz, kimseye bir şey göndermez. Windows'unu kendi istediği gibi kurmak isteyen
herkes için, sevgiyle yapıldı.

## Derleme

Visual Studio 2026 (MSVC, C++23), Windows SDK 10.0.26100 ve Python 3 gerekir.

```powershell
./build.ps1              # Debug
./build.ps1 -Test        # birim testleri
./build.ps1 -Dist        # Release + testler + dist\WinLove.exe
```

Arayüz Direct2D / DirectWrite ile tamamen kendi çizimimiz; motor DISM API, wimgapi ve çevrimdışı kayıt defteri
üzerinde kendi katmanımız. Ayrıntılar: [`docs/`](docs).

## Katkı ve geri bildirim

Hata ya da öneri için bir [issue](https://github.com/shadesofdeath/WinLove/issues) aç.

## Lisans

[GNU GPL v3.0](LICENSE). Pakete giren üçüncü taraf bileşenler ve lisansları: Inter, Geist ve JetBrains Mono (OFL 1.1), Lucide simgeleri (ISC)
(SIL OFL 1.1), nlohmann/json ve pugixml (MIT), Win11Debloat boş Başlat şablonu (MIT) — bkz. `third_party/`.

---

<p align="center">Geliştirici: <a href="https://github.com/shadesofdeath">@shadesofdeath</a></p>
