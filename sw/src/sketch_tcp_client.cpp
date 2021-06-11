#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include <chrono>
#include <thread>
#include <random>
#include <vector>
#include <algorithm>


#include <boost/program_options.hpp>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <iostream>

#include "barrier.hpp"

#define MAXSOCKETS 128


using Tuple = uint32_t;


static void set_cpu(int cpu) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu, &cpuset);
    pthread_t thread = pthread_self();

    if (pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset)) {
      fprintf(stderr, "Error setting cpu affinity\n");
    }
}

int fillRandomData(uint32_t* block, uint64_t sizeInTuples, std::string fileName) {
 
  uint32_t* uPtr = block;
  std::ifstream myFile;
  std::string line;
  uint32_t value;

  myFile.open("../../datafiles/"+fileName);

  uint64_t i =0;
  while( i < sizeInTuples) {  
    if(myFile.is_open()) {
      while(std::getline(myFile,line) && i < sizeInTuples) {
        value = std::stoul(line);
        *uPtr = (uint32_t) value;
        uPtr++;
        i++;
      }

      myFile.clear();
      myFile.seekg (0, std::ios::beg);     
    } else {
      std::cerr<< "Please insert a valid data file !" << std::endl;
      return -1;
    }
  }

   myFile.close();
   return 0;
}

void fillRandomDataLocal(uint32_t* block, uint64_t sizeInTuples,uint32_t repet) {

   for(unsigned i = 0; i < (((unsigned)sizeInTuples)/repet); i++) {
        for(unsigned j=0; j<repet; j++) 
         block[i*repet+j] = i;
    }

}

void call_from_thread(int threadNumber, uint32_t* pBuffer, uint32_t transferTuples, int openedSocket, int repetitions){
  //  set_cpu(threadNumber%NUM_CORES);
  char const *const  buf = (char const*)pBuffer;
  size_t      const  len = transferTuples * sizeof(uint32_t);

  for(int r=0; r<repetitions; r++){
    size_t  cnt = 0;
    while(cnt < len) {
      ssize_t const  n = write(openedSocket, buf+cnt, len-cnt);
      if(n < 0)  std::cerr << "Write error." << std::endl;
      cnt += n;
    }
  }
}


// call: ./sketch_tcp_client -t 250 --address 10.1.212.129 -f file_10k_2.txt

int main(int argc, char *argv[]) {

   //command line arguments

  boost::program_options::options_description programDescription("Allowed options");
  programDescription.add_options()("tuples,t", boost::program_options::value<uint64_t>(), "Size in tuples")
                                  ("repetitions,r", boost::program_options::value<uint32_t>(), "Number of repetitions")
                                  ("address", boost::program_options::value<std::string>(), "Master ip address")
                                  ("threads", boost::program_options::value<int>(), "Threads")
                                  ("datafile,f", boost::program_options::value<std::string>(), "Data file");

  boost::program_options::variables_map commandLineArgs;
  boost::program_options::store(boost::program_options::parse_command_line(argc, argv, programDescription), commandLineArgs);
  boost::program_options::notify(commandLineArgs);
   
  uint64_t sizeInTuples;
  uint64_t sizePerConn  = 0;
  uint32_t numberRepetitions = 1;
  std::string  masterAddr;
  std::string  file;

  int numThreads = MAXSOCKETS;
   
  if (commandLineArgs.count("tuples") > 0) {
     sizeInTuples = commandLineArgs["tuples"].as<uint64_t>();
  } else {
     std::cerr << "Argument missing.\n";
     return -1;
  }

  if (commandLineArgs.count("repetitions") > 0) {
     numberRepetitions = commandLineArgs["repetitions"].as<uint32_t>();
  }

  if (commandLineArgs.count("threads") > 0) {
     numThreads = commandLineArgs["threads"].as<int>();
     if(numThreads<0 || numThreads > MAXSOCKETS)
       numThreads = MAXSOCKETS;
  }

  if (commandLineArgs.count("datafile") > 0) {
     file = commandLineArgs["datafile"].as<std::string>();
  } else {
     std::cerr <<"Data set file missing.\n";
     return -1;
  }
   
  sizePerConn = sizeInTuples/numThreads; 
  uint64_t inputSize = sizePerConn * sizeof(Tuple);
   
  std::cout << "Number of Threads : " << numThreads << std::endl;  
  std::cout << "Number of tuples  : " << sizeInTuples*numberRepetitions << std::endl;
  std::cout << "Tuples per Connect: " << sizePerConn << std::endl;
  std::cout << "TranferSize       : " << ((double)inputSize*numThreads*numberRepetitions)/1000.0/1000.0/1000.0 << "[GB]" << std::endl;
  
  //std::cout << "Cardinality     : " << cardinality << std::endl;
  std::cout << "File name         : " << file << std::endl;

  if (commandLineArgs.count("address") > 0) {
    masterAddr = commandLineArgs["address"].as<std::string>();
    std::cout << "Master address  : " << masterAddr << std::endl;
  }

  uint32_t* dataBuffer =  (uint32_t*) malloc(inputSize);

  //sender
  double durationUs = 0.0;
  std::vector<double> durations;
  std::thread t[numThreads];

  std::cout<<"Start filling in the memory!"<<std::endl;  
  //if( fillRandomData(dataBuffer, sizePerConn, file) !=0 )
  //  return -1;
  fillRandomDataLocal(dataBuffer,sizePerConn,1);
  std::cout<<"End filling in the memory!"<<std::endl;

  //  barrier = new barrier_t[numberRepetitions+1];

  // for(int i=0; i<(numberRepetitions+1); i++){
  //    barrier_init(&barrier[i], numThreads+1);
  // }

  //Open Connection
  struct sockaddr_in server_addr;
  int sockfd[numThreads];

    // create sockets
  for(int i=0; i<numThreads; i++){  
    sockfd[i] = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP); //0
    
    if (sockfd[i] == -1) {
      std::cerr << "Error opening socket\n";
      return -1;
    }
    
    bzero(&server_addr, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(masterAddr.c_str());
    server_addr.sin_port = htons(5017);

    //Connect to server (CPU or FPGA)
    if (connect(sockfd[i], (struct sockaddr*)&server_addr, sizeof(server_addr)) != 0) {
      std::cerr << "Connection to Server failed...\n";
      return -1;
    }else{
      int yes = 1;
      if( setsockopt(sockfd[i], IPPROTO_TCP, TCP_NODELAY, (void *) &yes, sizeof(int)) < 0) 
        return -1;
    }
  }
    
  double totalDurationUs = 0.0;
  auto start = std::chrono::high_resolution_clock::now(); 
    
  for(int i=0; i<numThreads; i++){
    //Launch threads
    //call_from_thread(uint32_t* pBuffer, uint32_t transferTuples, int socket)
    t[i] = std::thread(call_from_thread, i, dataBuffer, sizePerConn, sockfd[i], numberRepetitions);  
  }
  // auto start = std::chrono::high_resolution_clock::now();
    
  // for(int i=0; i<(numberRepetitions+1); i++)
  //  barrier_cross(&barrier[i]);
    
  //auto end = std::chrono::high_resolution_clock::now();
  for(int i = 0; i<numThreads; i++){
    t[i].join();
  }

  auto end   = std::chrono::high_resolution_clock::now();   
  durationUs = std::chrono::duration_cast<std::chrono::microseconds>(end-start).count();    
  
  std::cout<<"Duration[s]:"<<((double)durationUs)/1000/1000<<std::endl;
  
  for(int i = 0; i<numThreads; i++){
    close(sockfd[i]);
  }
 
  std::cout << std::fixed << "Throughput[GB/s] : " << ((double) inputSize*numThreads*numberRepetitions)/durationUs/1000.0 << std::endl;

  std::cout<<"End sending data on the network!"<<std::endl;
  free (dataBuffer);
  
  return 0;

}

