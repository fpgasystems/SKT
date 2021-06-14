#ifndef HSL_HASHES_HPP
#define HSL_HASHES_HPP

#include <ap_int.h>
#include <cstdint>
#include <utility>

namespace hsl {

//---------------------------------------------------------------------------
// ConcatHash
//	Allows to assemble a wider hash function by concatenating sub hashes.
template<typename... Hashes>
class ConcatHash;

template<typename Hash>
class ConcatHash<Hash> : public Hash {
	Hash  hash;
public:
	ConcatHash() {}
	~ConcatHash() {}
public:
	auto operator()(ap_uint<32> const& val) const
	 -> ap_uint<decltype(hash(val))::width> {
		return  hash(val);
	}
};

template<typename Hash, typename... Tail>
class ConcatHash<Hash, Tail...> {
	Hash                head;
	ConcatHash<Tail...> tail;
public:
	ConcatHash() {}
	~ConcatHash() {}
public:
	auto operator()(ap_uint<32> const& val) const
	 -> ap_uint<decltype(head(val))::width + decltype(tail(val))::width> {
		return (head(val), tail(val));
	}
};

//---------------------------------------------------------------------------
// Murmur3_128
template<
	uint64_t SEED,
	unsigned W = 128
>
class Murmur3_128 {
	static uint64_t const  c1 = 0x87c37b91114253d5;
	static uint64_t const  c2 = 0x4cf5ad432745937f;
	static uint64_t const  c3 = 0xff51afd7ed558ccd;
	static uint64_t const  c4 = 0xc4ceb9fe1a85ec53;

public:
	Murmur3_128() {}
	~Murmur3_128() {}

public:
	ap_uint<W> operator()(ap_uint<32> const &data) const {
		static_assert(W <= 128, "Result width of more than 128 bits not supported.");
		ap_uint<64> const  len = 4;
		ap_uint<64> k1 = data;

		ap_uint<64> h1 = SEED;
		ap_uint<64> h2 = SEED;

		k1 *= c1;
		k1 = (k1 << 31) | (k1 >> (64 - 31));
		k1 *= c2;
		h1 ^= k1;

		h1 ^= len;
		h2 ^= len;

		h1 += h2;
		h2 += h1;

		h1 ^= h1 >> 33;
		h1 *= c3;
		h1 ^= h1 >> 33;
		h1 *= c4;
		h1 ^= h1 >> 33;

		h2 ^= h2 >> 33;
		h2 *= c3;
		h2 ^= h2 >> 33;
		h2 *= c4;
		h2 ^= h2 >> 33;

		h1 += h2;
		h2 += h1;

		return (h2, h1);
	}

}; // Murmur3_128

template<uint64_t SEED>
using Murmur3_64 = Murmur3_128<SEED, 64>;

//---------------------------------------------------------------------------
// Murmur3_32
template<uint32_t SEED, unsigned W = 32>
class Murmur3_32 {
	static uint32_t const  c1 = 0xcc9e2d51;
	static uint32_t const  c2 = 0x1b873593;

public:
	Murmur3_32() {}
	~Murmur3_32() {}

public:
	ap_uint<W> operator()(ap_uint<32> const &data) const {
		static_assert(W <= 32, "Result width of more than 32 bits not supported.");
		unsigned const len = 4;
		ap_uint<32>	h1 = SEED;
		ap_uint<32>	k1 = data;
		k1 *= c1;
		k1  = (k1 << 15) | (k1 >> (32 - 15));
		k1 *= c2;

		h1 ^= k1;
		h1  = (h1 << 13) | (h1 >> (32 - 13));
		h1  = h1*5 + 0xe6546b64;

		h1 ^= len;
		h1 ^= h1 >> 16;
		h1 *= 0x85ebca6b;
		h1 ^= h1 >> 13;
		h1 *= 0xc2b2ae35;
		h1 ^= h1 >> 16;

		return  h1;
	}

}; // class Murmur3_32

//---------------------------------------------------------------------------
// SipHash
template<
	unsigned W = 64,
	unsigned cROUNDS = 2,
	unsigned dROUNDS = 4
>
class SipHash {
public:
	SipHash() {}
	~SipHash() {}

public:
	ap_uint<W> operator()(ap_uint<32> const &data) const {
		static_assert(W <= 64, "Result width of more than 64 bits not supported.");
		uint64_t  v0 = UINT64_C(0x736f6d6570736575);
		uint64_t  v1 = UINT64_C(0x646f72616e646f6d);
		uint64_t  v2 = UINT64_C(0x6c7967656e657261);
		uint64_t  v3 = UINT64_C(0x7465646279746573);
		uint64_t  b = (UINT64_C(4)<<56) | data;
		v3 ^= b;
		for(unsigned i = 0; i < cROUNDS; i++)  sip_round(v0, v1, v2, v3);

		v0 ^= b;
		v2 ^= 0xFF;
		for(unsigned i = 0; i < dROUNDS; i++)  sip_round(v0, v1, v2, v3);

		return	v0^v1^v1^v2;
	}

private:
	static inline uint64_t rotl64( uint64_t x, int8_t r) {
		return (x << r) | (x >> (64 - r));
	}
	static inline void sip_round(uint64_t &v0, uint64_t &v1, uint64_t &v2, uint64_t &v3) {
		v0 += v1;
		v1 = rotl64(v1, 13);
		v1 ^= v0;
		v0 = rotl64(v0, 32);
		v2 += v3;
		v3 = rotl64(v3, 16);
		v3 ^= v2;
		v0 += v3;
		v3 = rotl64(v3, 21);
		v3 ^= v0;
		v2 += v1;
		v1 = rotl64(v1, 17);
		v1 ^= v2;
		v2 = rotl64(v2, 32);
	}

}; // class SipHash

} // namespace hsl
#endif
