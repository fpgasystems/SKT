#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <linux/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>

#include <cstdlib>
#include <cstdint>
#include <cmath>

#include <memory>
#include <atomic>
#include <chrono>

#include <iostream>
#include <vector>
#include <deque>
#include <thread>
#include <mutex>
#include <condition_variable>

#include "skt.hpp"

unsigned constexpr  JOB_SIZE = 1u<<16;

struct Job {
    size_t    cnt;
    uint32_t  buf[JOB_SIZE];
};

class JobQueue {
    std::unique_ptr<Job[]>  pool;
    std::mutex               mtx;
    std::condition_variable  cv;
    std::deque<Job*>  queue;

public:
    JobQueue(unsigned const  n = 0) {
        if(n) {
            pool = std::make_unique<Job[]>(n);
            for(unsigned  i = 0; i < n; i++) {
                queue.push_back(&pool[i]);
            }
        }
    }

public:
    Job *pop() {
        std::unique_lock<std::mutex>  lock(mtx);
        cv.wait(lock, [&queue = this->queue](){ return !queue.empty(); });
        Job *const  res = queue.front();
        queue.pop_front();
        return  res;
    }
    void push(Job *job) {
        {
            std::unique_lock<std::mutex>  lock(mtx);
            queue.push_back(job);
        }
        cv.notify_one();
    }
}; // class JobQueue



int main(int argc, char *argv[]) {

    //- Parse Parameters ----------------------------------------------------
    if(argc != 3) {
        std::cerr << "Usage: " << argv[0] << " <hash:MURMUR3_128/64> <threads>[x<collectors>]" << std::endl;
        return  EXIT_FAILURE;
    }

    hash_e   const  hash    = value_of<hash_e>(argv[1]);
    char *endp;
    unsigned const  threads = strtoul(argv[2], &endp, 10);
    if((threads <= 0) || (128 < threads)) {
        std::cerr << "Threads out of bounds. Exp: 1..128" << std::endl;
        return  EXIT_FAILURE;
    }
    unsigned const  mul_collectors = (*endp == 'x')? strtoul(endp+1, NULL, 10) : 4;
    if((mul_collectors <= 0) || (64 < mul_collectors)) {
        std::cerr << "Collectors multiple out of bounds. Exp: 1..64" << std::endl;
        return  EXIT_FAILURE;
    }

    std::cout << "Threads: " << threads << 'x' << mul_collectors << std::endl;

    std::vector<SktCollector> collectors;
    for(unsigned i = 0; i < threads*mul_collectors; i++) {
        collectors.emplace_back(13, 5, 13, 5, 13, hash);
    }


    //- Open Server Socket --------------------------------------------------
    int const  serverSocket = socket(AF_INET, SOCK_STREAM, 0); {
        int const  one = 1;
        setsockopt(serverSocket, SOL_SOCKET, SO_REUSEPORT, &one, sizeof(int));

        // Bind the address struct to the socket 
        struct sockaddr_in  serverAddr;
        memset(&serverAddr, 0, sizeof(serverAddr));
        serverAddr.sin_family      = AF_INET;
        serverAddr.sin_port        = htons(5017);
        serverAddr.sin_addr.s_addr = INADDR_ANY;
        if(bind(serverSocket, (struct sockaddr *)&serverAddr, sizeof(serverAddr))<0) {
            perror("bind");
            return  EXIT_FAILURE;
        }
        printf("Socket bind done.\n");

        // Listen on the socket, with  max connections requests queued    
        if(listen(serverSocket, threads) != 0) {
            perror("listen");
            return  1;
        }
        printf("Listening...\n");
    }

    std::atomic<unsigned>  connectCount(0);
    std::atomic<size_t>    itemCount(0);
    decltype(std::chrono::system_clock::now())  t0;

    std::thread  tid[threads];
    for(unsigned i = 0; i < threads; i++) {
        tid[i] = std::thread([serverSocket, &connectCount, &t0, &itemCount, collects = &collectors[i*mul_collectors], mul_collectors](){
            JobQueue  jobsFree(mul_collectors+1);
            JobQueue  jobsFull;

            std::thread  slaves[mul_collectors];
            for(unsigned  i = 0; i < mul_collectors; i++) {
                slaves[i] = std::thread([&jobsFull, &jobsFree, &collect = collects[i], &itemCount](){
                    while(true) {
                        Job *const  job = jobsFull.pop();
                        if(!job)  break;
                        collect.collect(job->buf, job->cnt);
                        itemCount += job->cnt;
                        jobsFree.push(job);
                    }
                });
            }

            int const  sock = accept(serverSocket, NULL, NULL);
            if(connectCount++ == 0)  t0 = std::chrono::system_clock::now();

            size_t    cnt = 0;
            uint32_t  cy;
            while(true) {
                Job *const  job = jobsFree.pop();

                char *ptr = (char*)job->buf;
                *(uint32_t*)ptr = cy;
                ssize_t const  n = recv(sock, ptr+cnt, sizeof(job->buf)-cnt, MSG_WAITALL);
                if(!n)  break;
                cnt += n;

                size_t const  items = cnt/sizeof(uint32_t);
                cnt %= sizeof(uint32_t);
                if(cnt)  cy = job->buf[items];
                job->cnt = items;
                jobsFull.push(job);
            }

            for(unsigned  i = 0; i < mul_collectors; i++)  jobsFull.push(nullptr);
            if(cnt)  std::cerr << "Remaining bytes: " << cnt << std::endl;
            close(sock);
            for(std::thread &t : slaves)  t.join();
        });
    }

    for(std::thread &t : tid)  t.join();
    auto const  t1 = std::chrono::system_clock::now();
    close(serverSocket);

    // Compact into first Table 
    for(unsigned  i = 1; i < threads*mul_collectors; i++) {
        collectors[0].merge(collectors[i]);
    }
    double const  cardest = collectors[0].estimate_cardinality();

    SktCollector collector_cols(0, 6, 1, 0, 0, hash); 
    collector_cols.merge_columns(collectors[0]);
    double const  median = collector_cols.get_median();

    auto const t2 = std::chrono::system_clock::now();

    float const d0 = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
    float const d1 = std::chrono::duration_cast<std::chrono::nanoseconds>(t2 - t0).count();

    std::cout
        << "Item Count: " << itemCount.load() << '\n'
        << "Collect Throughput [GB/s]: " << sizeof(uint32_t) * itemCount.load() / d0 << '\n'
        << "Total Throughput   [GB/s]: " << sizeof(uint32_t) * itemCount.load() / d1 << '\n'
        << "Cardinality: " << cardest << std::endl;

    return 0;
}
