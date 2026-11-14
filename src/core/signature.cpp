#include "signature.h"

#include "text.h"

#include <windows.h>
#include <wincrypt.h>
#include <bcrypt.h>
#include <softpub.h>
#include <wintrust.h>
#include <mscat.h>

#include <iterator>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace jl {
namespace {

bool signer_is_microsoft(HANDLE state) {
    if (state == nullptr) {
        return false;
    }
    CRYPT_PROVIDER_DATA* data = WTHelperProvDataFromStateData(state);
    if (data == nullptr) {
        return false;
    }
    CRYPT_PROVIDER_SGNR* signer = WTHelperGetProvSignerFromChain(data, 0, FALSE, 0);
    if (signer == nullptr) {
        return false;
    }
    CRYPT_PROVIDER_CERT* provider = WTHelperGetProvCertFromChain(signer, 0);
    if (provider == nullptr || provider->pCert == nullptr) {
        return false;
    }
    wchar_t name[512];
    name[0] = 0;
    if (CertGetNameStringW(provider->pCert, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, nullptr, name,
                           static_cast<DWORD>(std::size(name))) <= 1) {
        return false;
    }
    return contains_ci(name, L"microsoft windows");
}

bool verify_file(const std::wstring& path) {
    WINTRUST_FILE_INFO file = {};
    file.cbStruct = sizeof(file);
    file.pcwszFilePath = path.c_str();

    WINTRUST_DATA data = {};
    data.cbStruct = sizeof(data);
    data.dwUIChoice = WTD_UI_NONE;
    data.fdwRevocationChecks = WTD_REVOKE_NONE;
    data.dwUnionChoice = WTD_CHOICE_FILE;
    data.pFile = &file;
    data.dwStateAction = WTD_STATEACTION_VERIFY;
    data.dwProvFlags = WTD_CACHE_ONLY_URL_RETRIEVAL;

    GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    LONG status = WinVerifyTrust(nullptr, &action, &data);
    bool result = status == ERROR_SUCCESS && signer_is_microsoft(data.hWVTStateData);
    data.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust(nullptr, &action, &data);
    return result;
}

std::wstring hex_upper(const std::vector<BYTE>& bytes) {
    wchar_t pair[3];
    std::wstring out;
    out.reserve(bytes.size() * 2);
    for (BYTE value : bytes) {
        swprintf_s(pair, L"%02X", value);
        out += pair;
    }
    return out;
}

bool verify_catalog(const std::wstring& path) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }

    std::vector<BYTE> sha1;
    DWORD sha1Size = 0;
    if (CryptCATAdminCalcHashFromFileHandle(file, &sha1Size, nullptr, 0) && sha1Size > 0) {
        sha1.assign(sha1Size, 0);
        if (!CryptCATAdminCalcHashFromFileHandle(file, &sha1Size, sha1.data(), 0) || sha1.empty()) {
            sha1.clear();
        }
    }

    bool result = false;

    HCATADMIN admin = nullptr;
    bool usingSha1 = false;
    if (!CryptCATAdminAcquireContext2(&admin, nullptr, BCRYPT_SHA256_ALGORITHM, nullptr, 0)) {
        if (!CryptCATAdminAcquireContext(&admin, nullptr, 0)) {
            CloseHandle(file);
            return false;
        }
        usingSha1 = true;
    }

    std::vector<BYTE> hash;
    DWORD hashSize = 0;
    if (usingSha1) {
        hash = sha1;
        hashSize = static_cast<DWORD>(sha1.size());
    } else {
        DWORD size = 0;
        if (CryptCATAdminCalcHashFromFileHandle2(admin, file, &size, nullptr, 0) && size > 0) {
            hash.assign(size, 0);
            if (CryptCATAdminCalcHashFromFileHandle2(admin, file, &size, hash.data(), 0)) {
                hashSize = static_cast<DWORD>(hash.size());
            }
        }
    }

    if (!hash.empty() && sha1.size() > 0) {
        HCATINFO info = CryptCATAdminEnumCatalogFromHash(admin, hash.data(), hashSize, 0, nullptr);
        if (info != nullptr) {
            CATALOG_INFO catalog = {};
            catalog.cbStruct = sizeof(catalog);
            if (CryptCATCatalogInfoFromContext(info, &catalog, 0)) {
                std::wstring tag = hex_upper(sha1);

                WINTRUST_CATALOG_INFO member = {};
                member.cbStruct = sizeof(member);
                member.pcwszCatalogFilePath = catalog.wszCatalogFile;
                member.pcwszMemberFilePath = path.c_str();
                member.pcwszMemberTag = tag.c_str();
                member.hMemberFile = file;
                member.pbCalculatedFileHash = sha1.data();
                member.cbCalculatedFileHash = static_cast<DWORD>(sha1.size());

                WINTRUST_DATA data = {};
                data.cbStruct = sizeof(data);
                data.dwUIChoice = WTD_UI_NONE;
                data.fdwRevocationChecks = WTD_REVOKE_NONE;
                data.dwUnionChoice = WTD_CHOICE_CATALOG;
                data.pCatalog = &member;
                data.dwStateAction = WTD_STATEACTION_VERIFY;
                data.dwProvFlags = WTD_CACHE_ONLY_URL_RETRIEVAL;

                GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
                LONG status = WinVerifyTrust(nullptr, &action, &data);
                result = status == ERROR_SUCCESS && signer_is_microsoft(data.hWVTStateData);
                data.dwStateAction = WTD_STATEACTION_CLOSE;
                WinVerifyTrust(nullptr, &action, &data);
            }
            CryptCATAdminReleaseCatalogContext(admin, info, 0);
        }
    }
    CloseHandle(file);
    CryptCATAdminReleaseContext(admin, 0);
    return result;
}

}

bool microsoft_signed(const std::wstring& path) {
    if (path.empty()) {
        return false;
    }
    static std::mutex lock;
    static std::unordered_map<std::wstring, bool> cache;
    std::wstring key = lower(path);
    {
        std::lock_guard<std::mutex> guard(lock);
        auto it = cache.find(key);
        if (it != cache.end()) {
            return it->second;
        }
    }
    DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        return false;
    }
    bool result = verify_file(path) || verify_catalog(path);
    {
        std::lock_guard<std::mutex> guard(lock);
        cache[key] = result;
    }
    return result;
}

}
