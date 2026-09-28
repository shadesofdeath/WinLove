# Kod Kuralları

## Dil ve derleme
- C++23 (MSVC `/std:c++latest`; `std::expected` için), `/W4 /WX /permissive- /utf-8`, `UNICODE`/`_UNICODE`, `WIN32_LEAN_AND_MEAN`, `NOMINMAX`. Ayarlar tek yerde: `cmake/WinLoveTarget.cmake` → her hedefte `wl_configure_target()`.
- Include yolu `src/` köklü: `#include "core/image/ImageFormat.h"`. Üçüncü parti: `#include <json.hpp>`, `<doctest.h>`, `<pugixml.hpp>`.
- Windows metinleri `std::wstring`; UTF-8 yalnızca dosya/JSON sınırında (`wl::utf8::to/from`).
- COM: `wil`-benzeri kendi ince sarmalayıcımız yerine `Microsoft::WRL::ComPtr`. Ham `Release()` yok.

## Adlandırma
| Öğe | Stil | Örnek |
|---|---|---|
| Namespace | küçük | `wl` (base), `wl::core`, `wl::ui`, `wl::app` |
| Tip | PascalCase | `ChangeSet`, `TreeView` |
| Fonksiyon/değişken | camelCase | `listImages`, `mountDir` |
| Üye değişken | `m_` önek | `m_session` |
| Sabit/enum değeri | PascalCase | `Compression::Max` |
| Dosya | PascalCase.h/.cpp (tip başına) | `ChangeSet.h` |
| String anahtarı | nokta ayrımlı | `page.features.title` |
| Enum → metin | `<şey>Name()` | `codeName(ErrorCode)`, `formatName(ImageFormat)` |

> **`toString` adında serbest fonksiyon yazma.** ADL yüzünden doctest'in (ve başka kütüphanelerin) niteliksiz `toString` çağrılarını ele geçirir ve derleme hatası verir.

## Hata yönetimi
- Modül sınırında exception yok → `wl::Result<T>` (`base/Result.h`), hata üretmek için `wl::fail(code, message, context, hr)`. Hata döndüren fonksiyonlar `[[nodiscard]]`.
- Her `HRESULT` hatası bağlamla birlikte `Error`'a çevrilir ve loglanır.
- UI hata gösterimi: InfoBar (sayfa düzeyi), satır içi (öğe düzeyi), Toast (arka plan görevi).

## Thread kuralları
- UI nesnelerine yalnızca UI thread'i dokunur. Motor işleri yalnızca `TaskRunner` üzerinden.
- Paylaşılan durum yok; mesajla aktarım (değer kopyası / `shared_ptr<const T>`).

## UI kodu
- Renk/ölçü/font asla literal değil: `theme().color(Token::BgBase)`, `metrics().controlHeight`.
- Metin asla literal değil: `tr("page.features.title")`.
- Widget `paint()` içinde bellek ayırma ve layout hesaplama yok (önbellekten).
- Yeni widget = galeri sayfasına (`app/pages/GalleryPage.cpp`) eklenir. Galeri geliştirici aracıdır; satır etiketleri literal olabilir (tek istisna).
- Widget kuralları: renk geçişleri `Tween` + `animate()`; `tick()` dönüşü "hâlâ çalışıyor mu". Etkileşimli olmayan dekor çocuklar `setHitTestVisible(false)`. Liste/menü içi gezinme roving focus: öğeler `setTabStop(false)`, yalnızca aktif öğe Tab durağı.
- Durum doğrulaması: yeni etkileşim için `--render` + `--hover-at/--press-at/--tooltip-at/--tab` ile PNG al; mantığı `tests/ui/WidgetTests.cpp` benzeri testle sabitle.

## Yorumlar
Kodun *neden*'ini açıkla, *ne*'sini değil. Windows API tuhaflıkları için kısa yorum + `ENGINE.md` saha notu bağlantısı.

## Commit
İngilizce, emir kipi, kapsam önekli: `core(image): add WIM index listing`, `ui(tree): virtualize rows`, `docs: update STATUS`.
