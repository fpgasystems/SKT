#ifndef SKT_STREAM_HPP
#define SKT_STREAM_HPP

#include "hashes.hpp"

static unsigned const  N = 16;

static unsigned constexpr  P_HLL  = 16;

static unsigned constexpr  R_AGMS =  6;
static unsigned constexpr  P_AGMS = 13;
static unsigned constexpr  W_AGMS = 32;
static unsigned constexpr  AGMS_BITS = R_AGMS*(1+P_AGMS);

static unsigned constexpr  R_CM =  6;
static unsigned constexpr  P_CM = 13;
static unsigned constexpr  W_CM = 32;
static unsigned constexpr  CM_BITS = R_CM * P_CM;

static unsigned constexpr  HASH_BITS = AGMS_BITS > CM_BITS? AGMS_BITS : CM_BITS;

//using Hash = ConcatHash<Murmur3_128<0xDEADF00D, (HASH_BITS+1)/2>, Murmur3_128<0xF00DBAAD, HASH_BITS/2>>;
using Hash = hsl::Murmur3_128<0xDEADF00D, HASH_BITS>;

#endif
