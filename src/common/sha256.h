// SHA-256 (FIPS 180-4), self-contained so it is safe to use from DllMain (no library loads).
#pragma once
#include <cstddef>

namespace h2e {

// Writes the digest of `data` as 64 lowercase hex characters plus NUL.
void Sha256Hex(const void* data, size_t n, char out[65]);

}  // namespace h2e
