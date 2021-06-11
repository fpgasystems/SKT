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
#ifndef SKT_HPP
#define SKT_HPP

#include <cstdint>
#include <memory>
#include <functional>

#include "skt.hpp"

#include "hash.hpp"

template<typename T> char const *name_of(T  val);
template<>           char const *name_of<hash_e>(hash_e  val);
template<typename T> T      value_of(char const *name);
template<>           hash_e value_of<hash_e>(char const *name);

// SKT Collector Backends
template<hash_e HASH>
void skt_collect_ptr(uint32_t const *data, size_t const num_items, unsigned *hll_buckets, signed *agms_buckets, unsigned *cm_buckets, unsigned const hp_val, unsigned const ar_val, unsigned const ap_val, unsigned const cr_val, unsigned const cp_val);

class SktCollector {

    struct dispatch_t {
        void (*f_ptr)(uint32_t const*, size_t, unsigned*, signed*, unsigned*, unsigned, unsigned, unsigned, unsigned, unsigned);
    };
    static std::array<dispatch_t, (unsigned)hash_e::end> const  DISPATCH;

    //hll
    unsigned const              m_p_hll;
    std::unique_ptr<unsigned[]> m_buckets_hll;
    //agms
    unsigned const              m_r_agms;
    unsigned const              m_p_agms;
    std::unique_ptr<signed[]>   m_table_agms;
    //cm
    unsigned const              m_r_cm;
    unsigned const              m_p_cm;
    std::unique_ptr<unsigned[]> m_table_cm;

    dispatch_t const *const     m_dispatch;

public:
    SktCollector(unsigned const hp_val, unsigned const ar_val, unsigned const ap_val, unsigned const cr_val, unsigned const cp_val, hash_e const hash)
     : m_p_hll(hp_val), m_buckets_hll(new unsigned[1<<hp_val]()), 
       m_r_agms(ar_val), m_p_agms(ap_val), m_table_agms(new signed[(1<<ap_val)*ar_val]()),
       m_r_cm(cr_val), m_p_cm(cp_val), m_table_cm(new unsigned[(1<<cp_val)*cr_val]()),
       m_dispatch(&DISPATCH.at((unsigned)hash)) {}
    
    SktCollector(SktCollector&& o)
     : m_p_hll(o.m_p_hll), m_buckets_hll(std::move(o.m_buckets_hll)), 
       m_r_agms(o.m_r_agms), m_p_agms(o.m_p_agms), m_table_agms(std::move(o.m_table_agms)),
       m_r_cm(o.m_r_cm), m_p_cm(o.m_p_cm), m_table_cm(std::move(o.m_table_cm)),
       m_dispatch(o.m_dispatch) {}

    ~SktCollector() {}

public:
    void collect(uint32_t const *data, size_t  n) { m_dispatch->f_ptr(data, n, &m_buckets_hll[0], &m_table_agms[0], &m_table_cm[0],
                                                    m_p_hll, m_r_agms, m_p_agms, m_r_cm, m_p_cm); }

private:
    void merge0(SktCollector const& other);
public:
    SktCollector& merge(SktCollector const& other) {
        merge0(other);
        return *this;
    }

public:
    double estimate_cardinality();

private:
    void merge0_columns(SktCollector const& other);
public:
    SktCollector& merge_columns(SktCollector const& other) {
        merge0_columns(other);
        return *this;
    }

public:
    double get_median();

public:
    void clean();
};
#endif
