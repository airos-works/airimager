// Hash.cpp - XXH64, written from the xxHash specification
// (https://github.com/Cyan4973/xxHash/blob/dev/doc/xxhash_spec.md).
#include "engine/Hash.h"

#include <cstring>

namespace imager {

namespace {

const uint64_t kPrime1 = 0x9E3779B185EBCA87ULL;
const uint64_t kPrime2 = 0xC2B2AE3D27D4EB4FULL;
const uint64_t kPrime3 = 0x165667B19E3779F9ULL;
const uint64_t kPrime4 = 0x85EBCA77C2B2AE63ULL;
const uint64_t kPrime5 = 0x27D4EB2F165667C5ULL;


inline uint64_t
RotateLeft(uint64_t value, int bits)
{
	return (value << bits) | (value >> (64 - bits));
}


// Little-endian loads; memcpy keeps them legal on unaligned addresses.
inline uint64_t
Read64(const uint8_t* p)
{
	uint64_t value;
	memcpy(&value, p, sizeof(value));
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
	value = __builtin_bswap64(value);
#endif
	return value;
}


inline uint32_t
Read32(const uint8_t* p)
{
	uint32_t value;
	memcpy(&value, p, sizeof(value));
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
	value = __builtin_bswap32(value);
#endif
	return value;
}


inline uint64_t
Round(uint64_t accumulator, uint64_t lane)
{
	accumulator += lane * kPrime2;
	accumulator = RotateLeft(accumulator, 31);
	return accumulator * kPrime1;
}


inline uint64_t
MergeRound(uint64_t accumulator, uint64_t lane)
{
	accumulator ^= Round(0, lane);
	return accumulator * kPrime1 + kPrime4;
}

}  // namespace


uint64_t
Xxh64(const void* data, size_t length, uint64_t seed)
{
	const uint8_t* p = static_cast<const uint8_t*>(data);
	const uint8_t* end = p + length;
	uint64_t hash;

	if (length >= 32) {
		uint64_t v1 = seed + kPrime1 + kPrime2;
		uint64_t v2 = seed + kPrime2;
		uint64_t v3 = seed;
		uint64_t v4 = seed - kPrime1;
		const uint8_t* limit = end - 32;
		do {
			v1 = Round(v1, Read64(p));
			v2 = Round(v2, Read64(p + 8));
			v3 = Round(v3, Read64(p + 16));
			v4 = Round(v4, Read64(p + 24));
			p += 32;
		} while (p <= limit);
		hash = RotateLeft(v1, 1) + RotateLeft(v2, 7) + RotateLeft(v3, 12)
			+ RotateLeft(v4, 18);
		hash = MergeRound(hash, v1);
		hash = MergeRound(hash, v2);
		hash = MergeRound(hash, v3);
		hash = MergeRound(hash, v4);
	} else
		hash = seed + kPrime5;

	hash += length;

	while (end - p >= 8) {
		hash ^= Round(0, Read64(p));
		hash = RotateLeft(hash, 27) * kPrime1 + kPrime4;
		p += 8;
	}
	if (end - p >= 4) {
		hash ^= uint64_t(Read32(p)) * kPrime1;
		hash = RotateLeft(hash, 23) * kPrime2 + kPrime3;
		p += 4;
	}
	while (p < end) {
		hash ^= uint64_t(*p) * kPrime5;
		hash = RotateLeft(hash, 11) * kPrime1;
		p++;
	}

	hash ^= hash >> 33;
	hash *= kPrime2;
	hash ^= hash >> 29;
	hash *= kPrime3;
	hash ^= hash >> 32;
	return hash;
}

}  // namespace imager
