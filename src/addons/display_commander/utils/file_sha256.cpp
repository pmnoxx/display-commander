#include "file_sha256.hpp"
#include <windows.h>
#include <bcrypt.h>
#include <fstream>
#include <vector>

#pragma comment(lib, "bcrypt.lib")

namespace display_commander::utils {

namespace {
constexpr size_t kSha256DigestBytes = 32;
constexpr size_t kSha256HexLen = 64;
}  // namespace

std::string ComputeFileSha256(const std::filesystem::path& file_path) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::string result;

    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) {
        return result;
    }

    ULONG hash_object_len = 0;
    ULONG copied = 0;
    if (BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&hash_object_len), sizeof(hash_object_len),
                          &copied, 0) != 0) {
        BCryptCloseAlgorithmProvider(alg, 0);
        return result;
    }

    std::vector<UCHAR> hash_object(static_cast<size_t>(hash_object_len));
    if (BCryptCreateHash(alg, &hash, hash_object.data(), hash_object_len, nullptr, 0, 0) != 0) {
        BCryptCloseAlgorithmProvider(alg, 0);
        return result;
    }

    std::ifstream f(file_path, std::ios::binary);
    if (!f) {
        BCryptDestroyHash(hash);
        BCryptCloseAlgorithmProvider(alg, 0);
        return result;
    }

    std::vector<char> buf(65536);
    while (f.read(buf.data(), static_cast<std::streamsize>(buf.size())) || f.gcount() > 0) {
        ULONG n = static_cast<ULONG>(f.gcount());
        if (n == 0) break;
        if (BCryptHashData(hash, reinterpret_cast<PUCHAR>(buf.data()), n, 0) != 0) {
            BCryptDestroyHash(hash);
            BCryptCloseAlgorithmProvider(alg, 0);
            return result;
        }
    }

    UCHAR digest[kSha256DigestBytes];
    if (BCryptFinishHash(hash, digest, sizeof(digest), 0) != 0) {
        BCryptDestroyHash(hash);
        BCryptCloseAlgorithmProvider(alg, 0);
        return result;
    }

    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(alg, 0);

    result.resize(kSha256HexLen);
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < kSha256DigestBytes; i++) {
        result[i * 2] = hex[(digest[i] >> 4) & 0x0F];
        result[(i * 2) + 1] = hex[digest[i] & 0x0F];
    }
    return result;
}

}  // namespace display_commander::utils
