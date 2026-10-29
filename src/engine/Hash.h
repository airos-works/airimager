// Hash.h - the 64-bit xxHash (XXH64) of a buffer.
//
// airImager hashes every chunk it writes and every chunk it reads back, and
// compares the two lists: a fast check that catches any bit the drive did not
// store, with no second pass over the (possibly compressed) image.
#ifndef AIRIMAGER_HASH_H
#define AIRIMAGER_HASH_H

#include <cstddef>
#include <cstdint>

namespace imager {

uint64_t Xxh64(const void* data, size_t length, uint64_t seed = 0);

}  // namespace imager

#endif
