/**
 * Copyright (c) 2020, Systems Group, ETH Zurich
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its contributors
 * may be used to endorse or promote products derived from this software
 * without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include "skt.hpp"

#include <map>
#include <vector>
#include <algorithm>
#include <limits>
#include <cstring>
#include <cmath>

//---------------------------------------------------------------------------
// Utilities for hash_e enum
template<>
char const *name_of<hash_e>(hash_e  val) {
    static char const *LOOKUP[(unsigned)hash_e::end] = {
        "IDENT",
        "SIP",
        "MURMUR3_32",
        "MURMUR3_64",
        "MURMUR3_128",
#ifdef INCLUDE_AVX_HASHES
        "MURMUR3_32AVX",
        "MURMUR3_64AVX",
#endif
    };
    return  val < hash_e::end? LOOKUP[(unsigned)val] : "<undef>";
}

template<>
hash_e value_of<hash_e>(char const *name) {
    static std::map<char const*, hash_e, std::function<bool(char const*, char const*)>> const  LOOKUP {
        {
            { "IDENT",              hash_e::IDENT },
            { "SIP",                hash_e::SIP },
            { "MURMUR3_32",         hash_e::MURMUR3_32 },
            { "MURMUR3_64",         hash_e::MURMUR3_64 },
            { "MURMUR3_128",        hash_e::MURMUR3_128 },
#ifdef INCLUDE_AVX_HASHES
            { "MURMUR3_32AVX",      hash_e::MURMUR3_32AVX },
            { "MURMUR3_64AVX",      hash_e::MURMUR3_64AVX },
#endif
        },
        [](char const *a, char const *b) { return  strcmp(a, b) < 0; }
    };
    auto const  res = LOOKUP.find(name);
    return  res != LOOKUP.end()? res->second : hash_e::end;
}

//---------------------------------------------------------------------------
// Hash-based Dispatch Table
std::array<SktCollector::dispatch_t, (unsigned)hash_e::end> const  SktCollector::DISPATCH {
    SktCollector::dispatch_t { skt_collect_ptr<hash_e::IDENT>         },
    SktCollector::dispatch_t { skt_collect_ptr<hash_e::SIP>           },
    SktCollector::dispatch_t { skt_collect_ptr<hash_e::MURMUR3_32>    },
    SktCollector::dispatch_t { skt_collect_ptr<hash_e::MURMUR3_64>    },
    SktCollector::dispatch_t { skt_collect_ptr<hash_e::MURMUR3_128>    },
#ifdef INCLUDE_AVX_HASHES
    SktCollector::dispatch_t { skt_collect_ptr<hash_e::MURMUR3_32AVX> },
    SktCollector::dispatch_t { skt_collect_ptr<hash_e::MURMUR3_64AVX> },
#endif
};

#include <iostream>

void SktCollector::merge0(SktCollector const& other) {
    
    if(this->m_p_hll != other.m_p_hll)  
        throw std::invalid_argument("HLL incompatible bucket sets.");

    if(this->m_p_agms != other.m_p_agms || this->m_r_agms != other.m_r_agms)  
        throw std::invalid_argument("AGMS incompatible table.");

    if(this->m_p_cm != other.m_p_cm || this->m_r_cm != other.m_r_cm)  
        throw std::invalid_argument("CM incompatible table.");

    size_t const  M_hll  = 1<<other.m_p_hll;
    size_t const  M_agms = (1<<other.m_p_agms) * other.m_r_agms;
    size_t const  M_cm   = (1<<other.m_p_cm) * other.m_r_cm;
 
    
    for(size_t  i = 0; i < M_hll; i++) {
        unsigned *const  ref  = &this->m_buckets_hll[i];
        unsigned  const  cand = other.m_buckets_hll[i];
        if(*ref < cand) *ref = cand;
    }

    for(size_t  i = 0; i < M_agms; i++) {
        signed *const  ref  = &this->m_table_agms[i];
        signed  const  cand = other.m_table_agms[i];
        *ref += cand;
    }

    for(size_t  i = 0; i < M_cm; i++) {
        unsigned *const  ref  = &this->m_table_cm[i];
        unsigned  const  cand = other.m_table_cm[i];
        *ref += cand;
    }
}

void SktCollector::merge0_columns(SktCollector const& other) {
    
    size_t const  N = (1<<other.m_p_agms);

    for(size_t  i = 0; i < other.m_r_agms; i++) {
        signed *const  ref  = &this->m_table_agms[i];

        for (size_t j=0; j<N; j++){
            signed  const  cand = other.m_table_agms[i*N+j];
            *ref += cand*cand;
        }
        //std::cout<< this->m_table[i]<<std::endl;
    }
}

double SktCollector::get_median() {
    
    signed *const ref = &this->m_table_agms[0];
    unsigned val = this->m_r_agms;
    
    std::sort(ref, ref+val);

    double median = double(m_table_agms[(val-1)/2] + m_table_agms[val/2])/2;
    
    return  median;
}

double SktCollector::estimate_cardinality() {
    size_t const  M = 1<<m_p_hll;
    double const  ALPHA = (0.7213*M)/(M+1.079);

    // Raw Estimate and Zero Count
    size_t zeros    = 0;
    double rawest   = 0.0;
    for(unsigned  i = 0; i < M; i++) {
        unsigned const  rank = m_buckets_hll[i];
        if(rank == 0)   zeros++;
        if(std::numeric_limits<double>::is_iec559) {
            union { uint64_t  i; double  f; } d = { .i = (UINT64_C(1023)-rank)<<52 };
            rawest += d.f;
        }
        else rawest += 1.0/pow(2.0, rank);
    }
    rawest = (ALPHA * M * M) / rawest;

    // Refine Output
    if(rawest <= 2.5*M) {                           // Small Range Correction
        // Linear counting
        if(zeros)   return  M * log((double)M / (double)zeros);
    }
// DROP Large Range Correction - It makes things worse instead of better.
//  else if(rawest > CONST_TWO_POWER_32/30.0) {     // Large Range Correction
//      return (-CONST_TWO_POWER_32) * log(1.0 - (rawest / CONST_TWO_POWER_32));
//  }
    return  rawest;
}

void SktCollector::clean(){
    size_t const M_hll  = 1<<this->m_p_hll;
    size_t const M_agms = (1<<this->m_p_agms) * this->m_r_agms;
    size_t const M_cm   = (1<<this->m_p_cm) * this->m_r_cm;

    for(unsigned i=0; i<M_hll; i++)
        this->m_buckets_hll[i] = 0;

    for(unsigned i=0; i<M_agms; i++)
        this->m_table_agms[i] = 0;

    for(unsigned i=0; i<M_cm; i++)
        this->m_table_cm[i] = 0;
}