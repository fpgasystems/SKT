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
#include <memory>
#include <iostream>
#include <iomanip>
#include <chrono>

#include <cstdlib>
#include <cmath>
#include <algorithm>

#include <vector>
#include <thread>

#include <fstream>

#include "skt.hpp"

int main(int argc, char* argv[]) {

    // Validate and Capture Arguments
    if (argc != 10) {
        std::cout << "Usage: " << argv[0] << " '<hash>' <num_items> <bucket_bits> <num_rows> <bucket_bits> <num_rows> <bucket_bits> <num_threads> <repetitions>\n\n  Hashes:\n";
        for(unsigned i = 0; i < (unsigned)hash_e::end; i++) std::cout << '\t' << name_of((hash_e)i) << '\n';
        std::cout << std::endl;
        return  1;
    }
    hash_e const  hash = value_of<hash_e>(argv[1]);
    if(hash == hash_e::end) {
        std::cerr << "Unknown hash '" << argv[1] << '\'' << std::endl;
        return  1;
    }
    unsigned const per_block = 256/32;

    size_t const num_items = strtoul(argv[2], nullptr, 0);
    if(num_items%per_block != 0) {
        std::cerr << "num_items must be a multiple of " << per_block << std::endl;
        return  1;
    }
    size_t const num_blocks = num_items/per_block;
    unsigned const num_threads = (unsigned)std::min(num_blocks, (size_t)strtoul(argv[8], nullptr, 0));
    
    unsigned const hp_val = strtoul(argv[3], nullptr, 0);
    if(hp_val > 16) {
        std::cerr << "HLL precision bits has to be lower than 16." << std::endl;
        return  1;
    }

    unsigned const ar_val = strtoul(argv[4], nullptr, 0);
    if((ar_val < 1) || (ar_val > 8)) {
        std::cerr << "AGMS num_rows out of valid range [1:8]." << std::endl;
        return  1;
    }

    unsigned const ap_val = strtoul(argv[5], nullptr, 0);
    if((ap_val < 4) || (ap_val > 16)) {
        std::cerr << "AGMS bucket_bits out of valid range [4:16]." << std::endl;
        return  1;
    }

    unsigned const cr_val = strtoul(argv[6], nullptr, 0);
    if((cr_val < 1) || (cr_val > 8)) {
        std::cerr << "CM num_rows out of valid range [1:8]." << std::endl;
        return  1;
    }

    unsigned const cp_val = strtoul(argv[7], nullptr, 0);
    if((cp_val < 4) || (cp_val > 16)) {
        std::cerr << "AGMSbucket_bits out of valid range [4:16]." << std::endl;
        return  1;
    }
    
    unsigned const num_cores = std::thread::hardware_concurrency();
     
    unsigned const repetitions = strtoul(argv[9], nullptr, 0);

    std::cout
        << " H=" << name_of(hash)
        << " Bhll=" << hp_val
        << " Ragms=" << ar_val
        << " Bagms=" << ap_val
        << " Rcm=" << cr_val
        << " Bcm=" << cp_val
        << " T=" << num_threads << " (mod " << num_cores << " cores)" 
        << " Repetitions=" << repetitions << std::endl;

    // Frequency square through AGMS
    std::vector<SktCollector> collectors;
    for(unsigned i = 0; i < num_threads; i++)  collectors.emplace_back(hp_val, ar_val, ap_val, cr_val, cp_val, hash);
    
    SktCollector collector_cols(0, ar_val, 1, 0, 0, hash);  

    // Allocate & Populate Input Memory
    std::unique_ptr<uint32_t[]> input{new uint32_t[num_items]};
    for(unsigned i = 0; i < num_items; i++) input[i] = i;
    
    std::vector<float> durations_collect;
    std::vector<float> durations_total;
 
    double cardest = 0.0;
    double median  = 0.0;

    for(unsigned r=0; r<repetitions; r++) {
        // Threaded Skt Table Collection
        //  - split as evenly as possible at 32-byte boundaries
        auto const t0 = std::chrono::system_clock::now();
        {
            std::vector<std::thread> threads;

            size_t ofs = 0;
            for(unsigned i = 0; i < num_threads; i++) {
                size_t const  nofs  = (((i+1)*num_blocks)/num_threads) * per_block;

                size_t const  cnt = nofs-ofs;
                SktCollector& clct(collectors[i]);
                threads.emplace_back([&clct, data=&input[ofs], cnt](){ clct.collect(data, cnt); });
                cpu_set_t  cpus;
                CPU_ZERO(&cpus);
                CPU_SET((2*i)%num_cores, &cpus);
                pthread_setaffinity_np(threads.back().native_handle(), sizeof(cpus), &cpus);
                ofs = nofs;
            }   
            // Wait for all to finish
            for(std::thread& t : threads) t.join();
        }

        // Compact into first Table 
        auto const t1 = std::chrono::system_clock::now();
        for(unsigned  i = 1; i < num_threads; i++) {
            collectors[0].merge(collectors[i]);
        }

        cardest = collectors[0].estimate_cardinality();

        collector_cols.merge_columns(collectors[0]);
        median = collector_cols.get_median();

        auto const t2 = std::chrono::system_clock::now();
    
        float const d0 = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
        float const d1 = std::chrono::duration_cast<std::chrono::nanoseconds>(t2 - t0).count();
        
        durations_collect.push_back(d0); 
        durations_total.push_back(d1);

        for(unsigned  i = 0; i < num_threads; i++) collectors[i].clean();

        collector_cols.clean();
    }     

    // Report Measurements
//  double const std_error = (cardest/(double)num_items - 1.0) * 100.0;
//  double const ref_error = 100.0*(1.04/sqrt(1<<hp_val));
//  std::cout << std::fixed << std::setprecision(4)
//      << "  Estimated Cardinality: " << cardest << "\t[exp: " << num_items << ']' << std::endl
//      << "  Standard Error: " << std_error << "%\t[limit: " << ref_error << "%]";
//  if(fabs(std_error) > ref_error) std::cout << "\t! OUT OF RANGE !";
//    
//  std::cout<<std::endl;
//
//  std::cout<< std::fixed << std::setprecision(4)
//          << "  Median: " << median <<  std::endl;
//
//  float const d0 = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count()/1000.f;
//  float const d1 = std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t0).count()/1000.f;
//  std::cout
//      << std::endl << std::setprecision(3)
//      << "  Collection Time: " << d0 << " s" << "\t[" << (4*num_items)/d0/1000000.f << " MByte/s]" << std::endl
//      << "  Total Time: " << d1 << " s" << std::endl;

    //Get stats
    //collect-throughtput
    std::vector<float> th_collect;
    for(unsigned i=0; i<2; i++) {
        for(unsigned r=0; r<repetitions; r++) {
            if(i==0)
                th_collect.push_back((4*num_items)/durations_collect[r]);
            else
                th_collect.push_back((4*num_items)/durations_total[r]);
        }

        std::sort(th_collect.begin(),th_collect.end());
    
        float min = th_collect[0];
        float max = th_collect[repetitions-1];

        float p25 = 0.0;
        float p50 = 0.0;
        float p75 = 0.0;

        if(repetitions>=4){
            p25 = th_collect[(repetitions/4)-1];
            p50 = th_collect[(repetitions/2)-1];
            p75 = th_collect[(repetitions*3)/4-1];
        }
    
        float p1  = 0.0;
        float p5  = 0.0;
        float p95 = 0.0;
        float p99 = 0.0;
        float iqr = p75 - p25;
    
        float lower_iqr = p25 - (1.5 * iqr);
        float upper_iqr = p75 + (1.5 * iqr);
        if (repetitions >= 100) {
            p1  = th_collect[((repetitions)/100)-1];
            p5  = th_collect[((repetitions*5)/100)-1];
            p95 = th_collect[((repetitions*95)/100)-1];
            p99 = th_collect[((repetitions*99)/100)-1];
        }
        
        th_collect.clear();

        std::fstream file;
        std::string hash_name = name_of(hash);

        if(i==0){
            std::string name = "skt_results_collect_"+hash_name+".dat";
            file.open (name, std::ios::app);         
        }
        else{
            std::string name = "skt_results_total_"+hash_name+".dat";
            file.open (name, std::ios::app);
        }

        file.seekg (0, std::ios::end);
        int length = file.tellg();

        if(length == 0)
            file<<"items threads hpval arval apval crval cpval repet min max p1 p5 p25 p50 p75 p95 p99 iqr liqr uiqr hll agms"<<std::endl; 
    
        file<< num_items <<" "<< num_threads <<" "
            << hp_val <<" "<< ar_val <<" "<< ap_val <<" "<< cr_val <<" "<< cp_val <<" "
            << repetitions <<" "<< std::setprecision(5) 
            << min <<" "<< max <<" "
            << p1 <<" "<< p5 <<" "<< p25 <<" "
            << p50 <<" "<< p75 <<" "<< p95 <<" "<<p99<<" "
            << iqr <<" "<< lower_iqr <<" "
            << upper_iqr<<" "<<cardest<<" "<<median<<std::endl;
    
        file.close();
    }

    return  0;
}
