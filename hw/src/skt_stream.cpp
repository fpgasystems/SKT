#include "skt_stream.hpp"

#define CL_HPP_CL_1_2_DEFAULT_BUILD
#define CL_HPP_TARGET_OPENCL_VERSION 120
#define CL_HPP_MINIMUM_OPENCL_VERSION 120
#define CL_HPP_ENABLE_PROGRAM_CONSTRUCTION_FROM_ARRAY_COMPATIBILITY 1
#include <CL/cl2.hpp>
#include <CL/cl_ext_xilinx.h>

#include <stdlib.h>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <vector>
#include <chrono>

struct output_t {
	uint64_t  agms[R_AGMS];
	uint32_t  cnt;
	uint32_t  sumq_hi;
	uint32_t  min;
	uint32_t  max;
	uint64_t  sum;
	uint64_t  sumq;
	float     cardest;
	uint32_t  cycs;
	uint32_t  cm[R_CM*(1<<P_CM)];
};

//Customized buffer allocation for 4K boundary alignment
template<typename T>
struct aligned_allocator {
	using value_type = T;
	T* allocate(std::size_t num) {
		void* ptr = nullptr;
		if(posix_memalign(&ptr, 4096, num*sizeof(T)))
			throw  std::bad_alloc();
		return  reinterpret_cast<T*>(ptr);
	}
	void deallocate(T* p, std::size_t num) {
		free(p);
	}
};

int main(int const  argc, char const *const  argv[]) {

	// TARGET_DEVICE macro needs to be passed from gcc command line
	if(argc != 3) {
		std::cout << "Usage: " << argv[0] <<" <xclbin> <items>" << std::endl;
		return EXIT_FAILURE;
	}
	char const*   const  xclbinFilename = argv[1];
	long unsigned const  n = strtoul(argv[2], NULL, 0);

	std::vector<cl::Device> devices; {
		std::vector<cl::Platform> platforms;
		cl::Platform::get(&platforms);
		for(cl::Platform &platform : platforms) {
			if(platform.getInfo<CL_PLATFORM_NAME>().compare("Xilinx") == 0) {
				platform.getDevices(CL_DEVICE_TYPE_ACCELERATOR, &devices);
				if(!devices.empty())  goto found;
			}
		}
		std::cerr << "Error: Unable to find Target Device." << std::endl;
		return EXIT_FAILURE;
	}
  found:
	  devices.resize(1);

	// Creating Program from Binary File
	cl::Program::Binaries bins; {
		std::cout << "Loading: '" << xclbinFilename << "'\n";
		std::ifstream bin_file(xclbinFilename, std::ifstream::binary);
		bin_file.seekg(0, bin_file.end);
		unsigned const  nb = bin_file.tellg();
		char *const  buf = new char[nb];
		bin_file.seekg(0, bin_file.beg);
		bin_file.read(buf, nb);
		bins.push_back({buf,nb});
	}

	// Creating Context and Command Queue for selected device
	cl::Context	  context(devices[0]);
	cl::CommandQueue q(context, devices[0], CL_QUEUE_PROFILING_ENABLE);
	cl::Program	  program(context, devices, bins);
	cl::Kernel	   krnl(program, "krnl_skt_stream");

	// Device connection specification of the stream through extension pointer
	cl_mem_ext_ptr_t  ext;  // Extension pointer
	ext.param = krnl.get(); // The .param should be set to kernel
	ext.obj   = nullptr;

	// The .flag should be used to denote the kernel argument
	int  ret;
	ext.flags = 0;
	cl_stream  h2k_stream = clCreateStream(devices[0].get(), CL_STREAM_WRITE_ONLY, CL_STREAM, &ext, &ret);
	ext.flags = 1;
	cl_stream  k2h_stream = clCreateStream(devices[0].get(), CL_STREAM_READ_ONLY, CL_STREAM, &ext, &ret);

	std::vector<uint32_t, aligned_allocator<uint32_t>>  x(n);
	std::vector<output_t, aligned_allocator<output_t>>  y(1);
	for(unsigned i = 0 ; i < n; i++)  x[i] = i;

	auto const t1 = std::chrono::system_clock::now();

	// Initiating the WRITE transfer
	cl_stream_xfer_req  wr_req{0};
	wr_req.flags = CL_STREAM_EOT | CL_STREAM_NONBLOCKING;
	wr_req.priv_data = (void*)"write";
	clWriteStream(h2k_stream, x.data(), n*sizeof(uint32_t), &wr_req , &ret);

	// Initiate the READ transfer
	cl_stream_xfer_req  rd_req{0};
	rd_req.flags = CL_STREAM_EOT | CL_STREAM_NONBLOCKING;
	rd_req.priv_data = (void*)"read";
	clReadStream(k2h_stream, y.data(), sizeof(output_t), &rd_req, &ret);

	std::cout << "Polling results ..." << std::endl;
	cl_streams_poll_req_completions poll_req[2] {};
	int num = 2;
	clPollStreams(devices[0].get(), poll_req, 2, 2, &num, 5000, &ret);

	auto const t2 = std::chrono::system_clock::now();

	for(unsigned  i = 0; i < 2; i++) {
		std::cout << i << " [" << (char*)poll_req[i].priv_data << "]: " << poll_req[i].nbytes << std::endl;
	}

	output_t const& o = *y.data();
	std::cout
		<< "Cycles:\t" << o.cycs << std::endl
		<< "Count:\t"  << o.cnt << std::endl
		<< "Min:\t"    << o.min << std::endl
		<< "Max:\t"    << o.max << std::endl
		<< "Sum:\t"    << o.sum << std::endl
		<< "SumQ:\t"   << o.sumq << std::endl
		<< "AGMS:\t";
	for(uint64_t const  a : o.agms) std::cout << ' ' << a;
	std::cout << std::endl
		<< "Card:\t" << o.cardest << '\n' << std::endl;

	std::cout
		<< "Items\tBytes\tCard\tCycles\tHost_us\n"
		<< n << '\t'
		<< n * sizeof(uint32_t) << '\t'
		<< std::fixed << std::setprecision(1)
		<< o.cardest << '\t'
		<< o.cycs << '\t'
		<< std::fixed << std::setprecision(2)
		<< std::chrono::duration_cast<std::chrono::nanoseconds>(t2 - t1).count() / 1000.f
		<< std::endl;

	std::ofstream  ofs("a.sketch", std::ofstream::binary|std::ofstream::trunc);
	ofs.write(reinterpret_cast<char const*>(y.data()), sizeof(output_t));
	ofs.close();

	return  EXIT_SUCCESS;
}
