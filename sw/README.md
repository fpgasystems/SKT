# SW-SKT Reference Implementations
## Build Step
```
cd src
mkdir build
cd build 
make
```
## Running SW-SKT
### Local Sketch Computation over in-memory values
```
./sketch_bench MURMUR3_64 10000000 16 6 13 6 13 4 1
```

### Local Sketch Computation over a File
```
sketch_fileclient
```

### Remote Sketching
1. Sketching Server:
```
sketch_tcp_server
```

2. Data Feed Client Options

`sketch_tcp_client`     - feed generated data  
`sketch_tcp_fileclient` - feed a data file
