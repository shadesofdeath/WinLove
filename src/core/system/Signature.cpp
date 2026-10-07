#include "core/system/Signature.h"

#include "base/Log.h"
#include "core/system/Handle.h"

#include <windows.h>

#include <bcrypt.h>
#include <wincrypt.h>
#include <softpub.h>
#include <wintrust.h>
#include <mscat.h>

#include <format>
#include <vector>

namespace wl::core {

namespace {

// WinVerifyTrust on prepared data; the leaf signer's display name when it verifies.
std::wstring verify(WINTRUST_DATA& data) {
    GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    data.cbStruct = sizeof(data);
    data.dwUIChoice = WTD_UI_NONE;
    data.fdwRevocationChecks = WTD_REVOKE_NONE;
    data.dwStateAction = WTD_STATEACTION_VERIFY;
    const LONG status = WinVerifyTrust(static_cast<HWND>(INVALID_HANDLE_VALUE), &action, &data);
    std::wstring signer;
    if (status == ERROR_SUCCESS) {
        if (CRYPT_PROVIDER_DATA* provider = WTHelperProvDataFromStateData(data.hWVTStateData)) {
            if (CRYPT_PROVIDER_SGNR* sgnr = WTHelperGetProvSignerFromChain(provider, 0, FALSE, 0)) {
                if (CRYPT_PROVIDER_CERT* cert = WTHelperGetProvCertFromChain(sgnr, 0); cert && cert->pCert) {
                    wchar_t name[256] = {};
                    CertGetNameStringW(cert->pCert, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, nullptr, name, 256);
                    signer = name;
                }
            }
        }
    }
    data.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust(static_cast<HWND>(INVALID_HANDLE_VALUE), &action, &data);
    return signer;
}

} // namespace

std::wstring embeddedSigner(const std::filesystem::path& file) {
    WINTRUST_FILE_INFO info{};
    info.cbStruct = sizeof(info);
    info.pcwszFilePath = file.c_str();
    WINTRUST_DATA data{};
    data.dwUnionChoice = WTD_CHOICE_FILE;
    data.pFile = &info;
    return verify(data);
}

std::wstring catalogSigner(const std::filesystem::path& file) {
    UniqueHandle handle{CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr)};
    if (!handle) {
        return {};
    }
    HCATADMIN admin = nullptr;
    CERT_STRONG_SIGN_PARA policy{};
    policy.cbSize = sizeof(policy);
    policy.dwInfoChoice = CERT_STRONG_SIGN_OID_INFO_CHOICE;
    policy.pszOID = const_cast<char*>(szOID_CERT_STRONG_SIGN_OS_CURRENT);
    if (!CryptCATAdminAcquireContext2(&admin, nullptr, BCRYPT_SHA256_ALGORITHM, &policy, 0)) {
        return {};
    }
    std::wstring signer;
    DWORD size = 0;
    CryptCATAdminCalcHashFromFileHandle2(admin, handle.get(), &size, nullptr, 0);
    std::vector<BYTE> hash(size);
    if (size > 0 && CryptCATAdminCalcHashFromFileHandle2(admin, handle.get(), &size, hash.data(), 0)) {
        // The member tag of a catalog entry is the hash in upper-case hex.
        std::wstring tag;
        for (const BYTE b : hash) {
            tag += std::format(L"{:02X}", b);
        }
        HCATINFO catalog = CryptCATAdminEnumCatalogFromHash(admin, hash.data(), size, 0, nullptr);
        while (catalog && signer.empty()) {
            CATALOG_INFO info{};
            info.cbStruct = sizeof(info);
            if (CryptCATCatalogInfoFromContext(catalog, &info, 0)) {
                WINTRUST_CATALOG_INFO member{};
                member.cbStruct = sizeof(member);
                member.pcwszCatalogFilePath = info.wszCatalogFile;
                member.pcwszMemberFilePath = file.c_str();
                member.pcwszMemberTag = tag.c_str();
                member.pbCalculatedFileHash = hash.data();
                member.cbCalculatedFileHash = size;
                member.hMemberFile = handle.get();
                member.hCatAdmin = admin;
                WINTRUST_DATA data{};
                data.dwUnionChoice = WTD_CHOICE_CATALOG;
                data.pCatalog = &member;
                signer = verify(data);
            }
            catalog = CryptCATAdminEnumCatalogFromHash(admin, hash.data(), size, 0, &catalog);
        }
        if (catalog) {
            CryptCATAdminReleaseCatalogContext(admin, catalog, 0);
        }
    }
    CryptCATAdminReleaseContext(admin, 0);
    return signer;
}

std::wstring trustedSigner(const std::filesystem::path& file) {
    if (auto signer = embeddedSigner(file); !signer.empty()) {
        return signer;
    }
    return catalogSigner(file);
}

bool signedByMicrosoft(const std::filesystem::path& file) {
    const std::wstring signer = trustedSigner(file);
    const bool microsoft = signer == L"Microsoft Windows" || signer == L"Microsoft Corporation" ||
                           signer.starts_with(L"Microsoft Windows ");
    if (!microsoft) {
        log::warn("trust", std::format(L"not signed by Microsoft ({}): {}", signer.empty() ? L"no valid signature" : signer,
                                       file.wstring()));
    }
    return microsoft;
}

} // namespace wl::core
