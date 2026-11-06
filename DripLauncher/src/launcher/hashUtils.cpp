#include "hashUtils.h"

#include <windows.h>
#include <bcrypt.h>

#include <fstream>
#include <iterator>
#include <vector>

#pragma comment(lib, "bcrypt.lib")

namespace dk {

std::string sha256HexOfBytes(const void* data, size_t size)
{
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD cb = 0, objLen = 0, hashLen = 0;
    std::vector<unsigned char> obj, digest;
    std::string outHex;

    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0)
        return {};

    if (BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objLen),
                          sizeof(objLen), &cb, 0) == 0 &&
        BCryptGetProperty(alg, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&hashLen),
                          sizeof(hashLen), &cb, 0) == 0) {
        obj.resize(objLen);
        digest.resize(hashLen);
        if (BCryptCreateHash(alg, &hash, obj.data(), objLen, nullptr, 0, 0) == 0 &&
            BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<void*>(data)),
                           static_cast<ULONG>(size), 0) == 0 &&
            BCryptFinishHash(hash, digest.data(), hashLen, 0) == 0) {
            static const char kHex[] = "0123456789abcdef";
            outHex.reserve(hashLen * 2);
            for (unsigned char b : digest) {
                outHex.push_back(kHex[b >> 4]);
                outHex.push_back(kHex[b & 0x0F]);
            }
        }
    }

    if (hash)
        BCryptDestroyHash(hash);
    if (alg)
        BCryptCloseAlgorithmProvider(alg, 0);
    return outHex;
}

std::string sha256HexOfFile(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return {};
    std::vector<char> blob((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (blob.empty())
        return {};
    return sha256HexOfBytes(blob.data(), blob.size());
}

} // namespace dk
