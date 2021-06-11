cmake_minimum_required(VERSION 3.2)
project(Sketches)

set(CMAKE_CXX_STANDARD 14)
set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread -mavx -march=native -O3 ")
set (CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -pthread -mavx -march=native -O3")
set (CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -pthread -mavx -march=native -O3")

find_package(Threads REQUIRED)
find_package(Boost COMPONENTS program_options REQUIRED)

add_executable(sketch_tcp_client 
    sketch_tcp_client.cpp 
)
target_link_libraries(sketch_tcp_client
        ${Boost_LIBRARIES}
)
add_executable(sketch_tcp_fileclient
    sketch_tcp_fileclient.cpp
)
add_executable(txt2bin
    txt2bin.cpp
)
add_executable(sketch_fileclient
    sketch_fileclient.cpp skt.cpp skt_base.cpp
)
target_link_libraries(sketch_fileclient
        ${Boost_LIBRARIES} -lboost_iostreams
)

add_executable(sketch_tcp_server 
    skt.cpp
    skt_base.cpp
    sketch_tcp_server.cpp
    )

