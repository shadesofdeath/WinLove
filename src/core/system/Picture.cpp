#include "core/system/Picture.h"

#include "core/system/Com.h"

#include <windows.h>

#include <objbase.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstring>
#include <format>

namespace wl::core {

namespace {

using Microsoft::WRL::ComPtr;

Error wicError(HRESULT hr, std::wstring_view what, const std::filesystem::path& file) {
    return Error{ErrorCode::IoError, std::wstring(what), file.wstring(), static_cast<std::int32_t>(hr)};
}

struct Decoded {
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICBitmapFrameDecode> frame;
    UINT width = 0;
    UINT height = 0;
};

Result<Decoded> decode(const std::filesystem::path& file) {
    Decoded d;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&d.factory));
    if (FAILED(hr)) {
        return std::unexpected(wicError(hr, L"WIC is not available", file));
    }
    ComPtr<IWICBitmapDecoder> decoder;
    hr = d.factory->CreateDecoderFromFilename(file.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand,
                                              &decoder);
    if (FAILED(hr)) {
        return std::unexpected(wicError(hr, L"not a picture Windows can read", file));
    }
    hr = decoder->GetFrame(0, &d.frame);
    if (SUCCEEDED(hr)) {
        hr = d.frame->GetSize(&d.width, &d.height);
    }
    if (FAILED(hr) || d.width == 0 || d.height == 0) {
        return std::unexpected(wicError(hr, L"the picture cannot be decoded", file));
    }
    return d;
}

} // namespace

Result<PictureSize> pictureSize(const std::filesystem::path& file) {
    ComScope com;
    auto d = decode(file);
    if (!d) {
        return std::unexpected(d.error());
    }
    return PictureSize{static_cast<int>(d->width), static_cast<int>(d->height)};
}

Result<std::string> encodePicture(const std::filesystem::path& source, PictureFormat format, int width, int height) {
    ComScope com;
    auto d = decode(source);
    if (!d) {
        return std::unexpected(d.error());
    }
    IWICImagingFactory* factory = d->factory.Get();
    ComPtr<IWICBitmapSource> current = d->frame;
    HRESULT hr = S_OK;

    if (width > 0 && height > 0) {
        // Cover: scale so both sides reach the target, then crop the middle.
        const double scale = std::max(static_cast<double>(width) / d->width, static_cast<double>(height) / d->height);
        const UINT scaledW = std::max<UINT>(static_cast<UINT>(width), static_cast<UINT>(d->width * scale + 0.5));
        const UINT scaledH = std::max<UINT>(static_cast<UINT>(height), static_cast<UINT>(d->height * scale + 0.5));
        ComPtr<IWICBitmapScaler> scaler;
        hr = factory->CreateBitmapScaler(&scaler);
        if (SUCCEEDED(hr)) {
            hr = scaler->Initialize(current.Get(), scaledW, scaledH, WICBitmapInterpolationModeHighQualityCubic);
        }
        if (FAILED(hr)) {
            return std::unexpected(wicError(hr, L"the picture cannot be scaled", source));
        }
        ComPtr<IWICBitmapClipper> clipper;
        hr = factory->CreateBitmapClipper(&clipper);
        const WICRect crop{static_cast<INT>((scaledW - width) / 2), static_cast<INT>((scaledH - height) / 2), width, height};
        if (SUCCEEDED(hr)) {
            hr = clipper->Initialize(scaler.Get(), &crop);
        }
        if (FAILED(hr)) {
            return std::unexpected(wicError(hr, L"the picture cannot be cropped", source));
        }
        current = clipper;
    }

    const WICPixelFormatGUID pixel = format == PictureFormat::Png ? GUID_WICPixelFormat32bppBGRA : GUID_WICPixelFormat24bppBGR;
    ComPtr<IWICFormatConverter> converter;
    hr = factory->CreateFormatConverter(&converter);
    if (SUCCEEDED(hr)) {
        hr = converter->Initialize(current.Get(), pixel, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
    }
    if (FAILED(hr)) {
        return std::unexpected(wicError(hr, L"the picture cannot be converted", source));
    }

    ComPtr<IStream> stream;
    hr = CreateStreamOnHGlobal(nullptr, TRUE, &stream);
    const GUID container = format == PictureFormat::Jpeg  ? GUID_ContainerFormatJpeg
                           : format == PictureFormat::Png ? GUID_ContainerFormatPng
                                                          : GUID_ContainerFormatBmp;
    ComPtr<IWICBitmapEncoder> encoder;
    if (SUCCEEDED(hr)) {
        hr = factory->CreateEncoder(container, nullptr, &encoder);
    }
    if (SUCCEEDED(hr)) {
        hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
    }
    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> options;
    if (SUCCEEDED(hr)) {
        hr = encoder->CreateNewFrame(&frame, &options);
    }
    if (SUCCEEDED(hr) && format == PictureFormat::Jpeg && options) {
        PROPBAG2 option{};
        option.pstrName = const_cast<LPOLESTR>(L"ImageQuality");
        VARIANT value;
        VariantInit(&value);
        value.vt = VT_R4;
        value.fltVal = 0.95f;
        (void)options->Write(1, &option, &value);
    }
    if (SUCCEEDED(hr)) {
        hr = frame->Initialize(options.Get());
    }
    UINT outW = 0;
    UINT outH = 0;
    if (SUCCEEDED(hr)) {
        hr = converter->GetSize(&outW, &outH);
    }
    if (SUCCEEDED(hr)) {
        hr = frame->SetSize(outW, outH);
    }
    WICPixelFormatGUID want = pixel;
    if (SUCCEEDED(hr)) {
        hr = frame->SetPixelFormat(&want);
    }
    if (SUCCEEDED(hr)) {
        hr = frame->WriteSource(converter.Get(), nullptr);
    }
    if (SUCCEEDED(hr)) {
        hr = frame->Commit();
    }
    if (SUCCEEDED(hr)) {
        hr = encoder->Commit();
    }
    if (FAILED(hr)) {
        return std::unexpected(wicError(hr, L"the picture cannot be encoded", source));
    }

    STATSTG stat{};
    hr = stream->Stat(&stat, STATFLAG_NONAME);
    if (FAILED(hr)) {
        return std::unexpected(wicError(hr, L"the encoded picture cannot be read", source));
    }
    std::string bytes(static_cast<std::size_t>(stat.cbSize.QuadPart), '\0');
    const LARGE_INTEGER zero{};
    stream->Seek(zero, STREAM_SEEK_SET, nullptr);
    ULONG read = 0;
    hr = stream->Read(bytes.data(), static_cast<ULONG>(bytes.size()), &read);
    if (FAILED(hr) || read != bytes.size()) {
        return std::unexpected(wicError(hr, L"the encoded picture cannot be read", source));
    }
    return bytes;
}

Result<std::string> pictureBgraSquare(const std::filesystem::path& source, int size) {
    if (size <= 0 || size > 1024) {
        return fail(ErrorCode::InvalidArgument, L"bad icon size", source.wstring());
    }
    ComScope com;
    auto d = decode(source);
    if (!d) {
        return std::unexpected(d.error());
    }
    // Contain: the longer side becomes `size`, the other is centred on transparency.
    const double scale = std::min(static_cast<double>(size) / d->width, static_cast<double>(size) / d->height);
    const UINT w = std::max<UINT>(1, static_cast<UINT>(d->width * scale + 0.5));
    const UINT h = std::max<UINT>(1, static_cast<UINT>(d->height * scale + 0.5));
    ComPtr<IWICBitmapScaler> scaler;
    HRESULT hr = d->factory->CreateBitmapScaler(&scaler);
    if (SUCCEEDED(hr)) {
        hr = scaler->Initialize(d->frame.Get(), w, h, WICBitmapInterpolationModeHighQualityCubic);
    }
    ComPtr<IWICFormatConverter> converter;
    if (SUCCEEDED(hr)) {
        hr = d->factory->CreateFormatConverter(&converter);
    }
    if (SUCCEEDED(hr)) {
        hr = converter->Initialize(scaler.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0.0,
                                   WICBitmapPaletteTypeCustom);
    }
    std::string pixels(static_cast<std::size_t>(size) * size * 4, '\0');
    if (SUCCEEDED(hr)) {
        std::string scaled(static_cast<std::size_t>(w) * h * 4, '\0');
        hr = converter->CopyPixels(nullptr, w * 4, static_cast<UINT>(scaled.size()), reinterpret_cast<BYTE*>(scaled.data()));
        if (SUCCEEDED(hr)) {
            const UINT left = (static_cast<UINT>(size) - w) / 2;
            const UINT top = (static_cast<UINT>(size) - h) / 2;
            for (UINT y = 0; y < h; ++y) {
                std::memcpy(pixels.data() + ((top + y) * static_cast<std::size_t>(size) + left) * 4,
                            scaled.data() + static_cast<std::size_t>(y) * w * 4, static_cast<std::size_t>(w) * 4);
            }
        }
    }
    if (FAILED(hr)) {
        return std::unexpected(wicError(hr, L"the picture cannot be scaled to an icon", source));
    }
    return pixels;
}

Result<std::string> encodePngBgra(const std::string& pixels, int size) {
    if (size <= 0 || pixels.size() != static_cast<std::size_t>(size) * size * 4) {
        return fail(ErrorCode::InvalidArgument, L"bad pixel buffer", L"");
    }
    ComScope com;
    ComPtr<IWICImagingFactory> factory;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    ComPtr<IWICBitmap> bitmap;
    if (SUCCEEDED(hr)) {
        hr = factory->CreateBitmapFromMemory(static_cast<UINT>(size), static_cast<UINT>(size), GUID_WICPixelFormat32bppBGRA,
                                             static_cast<UINT>(size) * 4, static_cast<UINT>(pixels.size()),
                                             reinterpret_cast<BYTE*>(const_cast<char*>(pixels.data())), &bitmap);
    }
    ComPtr<IStream> stream;
    if (SUCCEEDED(hr)) {
        hr = CreateStreamOnHGlobal(nullptr, TRUE, &stream);
    }
    ComPtr<IWICBitmapEncoder> encoder;
    if (SUCCEEDED(hr)) {
        hr = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
    }
    if (SUCCEEDED(hr)) {
        hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
    }
    ComPtr<IWICBitmapFrameEncode> frame;
    if (SUCCEEDED(hr)) {
        hr = encoder->CreateNewFrame(&frame, nullptr);
    }
    if (SUCCEEDED(hr)) {
        hr = frame->Initialize(nullptr);
    }
    if (SUCCEEDED(hr)) {
        hr = frame->SetSize(static_cast<UINT>(size), static_cast<UINT>(size));
    }
    WICPixelFormatGUID want = GUID_WICPixelFormat32bppBGRA;
    if (SUCCEEDED(hr)) {
        hr = frame->SetPixelFormat(&want);
    }
    if (SUCCEEDED(hr)) {
        hr = frame->WriteSource(bitmap.Get(), nullptr);
    }
    if (SUCCEEDED(hr)) {
        hr = frame->Commit();
    }
    if (SUCCEEDED(hr)) {
        hr = encoder->Commit();
    }
    STATSTG stat{};
    if (SUCCEEDED(hr)) {
        hr = stream->Stat(&stat, STATFLAG_NONAME);
    }
    if (FAILED(hr)) {
        return std::unexpected(wicError(hr, L"the icon cannot be encoded as PNG", L""));
    }
    std::string bytes(static_cast<std::size_t>(stat.cbSize.QuadPart), '\0');
    const LARGE_INTEGER zero{};
    stream->Seek(zero, STREAM_SEEK_SET, nullptr);
    ULONG read = 0;
    hr = stream->Read(bytes.data(), static_cast<ULONG>(bytes.size()), &read);
    if (FAILED(hr) || read != bytes.size()) {
        return std::unexpected(wicError(hr, L"the icon cannot be encoded as PNG", L""));
    }
    return bytes;
}

} // namespace wl::core
