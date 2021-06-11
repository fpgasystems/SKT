#include <iostream>
#include <fstream>
#include <iomanip>

#include <vector>
#include <thread>

#include <strings.h>
#include <unistd.h>

#include <sys/socket.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>


int main(int  argc, char const *const  argv[]) {
	if(argc != 4) {
		std::cerr << argv[0] << " <file> <server_address> <threads>" << std::endl;
		return -1;
	}
	std::vector<uint32_t>  ibuf; {
		std::ifstream  ifs(argv[1]);
		while(true) {
			uint32_t  v;
			ifs >> v;
			if(!ifs)  break;
			ibuf.emplace_back(v);
		}
	}
	auto const  isize = ibuf.size();

	struct sockaddr_in  server_addr;
	bzero(&server_addr, sizeof(server_addr));
	server_addr.sin_family      = AF_INET;
	server_addr.sin_addr.s_addr = inet_addr(argv[2]);
	server_addr.sin_port        = htons(5017);

	unsigned const  nthreads = atoi(argv[3]);

	std::cout << "Sending " << isize << " items to " << argv[2] << ":5017 over " << nthreads << " connections ..." << std::endl;

	std::thread  workers[nthreads];
	for(unsigned  i = 0; i < nthreads; i++) {
		workers[i] = std::thread([&server_addr, obuf = (char const*)&ibuf[i*isize/nthreads], olen = ((i+1)*isize/nthreads - i*isize/nthreads)*sizeof(uint32_t)]() {
			auto const  sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
			if(connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) != 0) {
				std::cerr << "Connection to Server failed ..." << std::endl;
				return;
			}

			int yes = 1;
			if(setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (void*)&yes, sizeof(int)) < 0) {
				std::cerr << "setsockopt failed ..." << std::endl;
				return;
			}

			size_t  ocnt = 0;
			while(ocnt < olen) {
				ssize_t const  n = write(sock, obuf+ocnt, olen-ocnt);
				if(n < 0) {
					std::cerr << "Write error." << std::endl;
					return;
				}
				ocnt += n;
			}
		});
	}

	for(std::thread &t : workers)  t.join();
	return 0;
}

