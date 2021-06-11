#include <iostream>
#include <fstream>

int main(int const  argc, char const *const  argv[]) {
	if(argc != 3) {
		std::cerr << argv[0] << " <file.txt> <file.bin>" << std::endl;
		return -1;
	}

	std::ifstream  ifs(argv[1]);
	std::ofstream  ofs(argv[2], std::ios::binary);
	while(true) {
		uint32_t  v;
		ifs >> v;
		if(!ifs)  break;
		ofs.write((char const*)&v, sizeof(v));
	}

}