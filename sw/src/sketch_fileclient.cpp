#include <iostream>
#include <iomanip>
#include <vector>
#include <thread>

#include <boost/iostreams/device/mapped_file.hpp>

#include "skt.hpp"


int main(int  argc, char const *const  argv[]) {

	// Evaluate Parameters
	if(argc != 3) {
		std::cerr << argv[0] << " <file> <threads>" << std::endl;
		return -1;
	}

	boost::iostreams::mapped_file  ibuf(argv[1], std::ios_base::in);
	uint32_t const *const  idata    = (uint32_t const*)ibuf.const_data();
	size_t const           isize    = ibuf.size()/sizeof(uint32_t);
	unsigned const         nthreads = atoi(argv[2]);

	std::cout << "Processing " << isize << " items by " << nthreads << " threads." << std::endl;
	if(nthreads < 1)  return  0;

	std::vector<SktCollector> collectors;
	collectors.reserve(nthreads);
	hash_e const  hash = value_of<hash_e>("MURMUR3_128");

	// Distribute data to partial sketches
	std::thread  workers[nthreads];
	for(unsigned  i = 0; i < nthreads; i++) {
		collectors.emplace_back(13, 5, 13, 5, 13, hash);
		SktCollector &collector = collectors.back();
		workers[i] = std::thread([&collector, data = &idata[i*isize/nthreads], size = (i+1)*isize/nthreads - i*isize/nthreads](){
			collector.collect(data, size);
		});
	}

	// Compact into first Table
	workers[0].join();
	SktCollector &master_collector = collectors.front();
	for(unsigned  i = 1; i < nthreads; i++) {
		workers[i].join();
		master_collector.merge(collectors[i]);
	}

	double const  cardest = collectors[0].estimate_cardinality();
	std::cout  << "Estimated cardinality: " << cardest << std::endl;

	return 0;
}

