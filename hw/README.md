# SKT Kernel (QDMA)

Synthesize SKT as a hardware-accelerated streaming QDMA kernel for the
parallel computation of the HLL, CountMin and FastAGMS sketches.

## Build Steps
1. Setup the environment importing XRT setup and Vitis settings:
```bash
. /opt/xilinx/xrt/setup.sh
. /opt/Xilinx/Vitis/2020.1/settings64.sh
```

2. Build the kernel archive `bin/skt_stream.xclbin` (expect 1-2 hours):
```bash
make hw
```

3. Build the example host application `bin/skt_stream`:
```bash
make host
```

## Running the Example Application on an Alveo U250 Card
Run the sketch over a generated input of sequential 32-bit integers: `0, 1, 2, 3, ...`
```bash
bin/skt_stream bin/skt_stream.xclbin 10000000
```
A sketch summary is printed to the console.
The complete result record is left in the `a.sketch` output file for later offline querying.

Run the sketch over a (prefix of a) user-supplied file of 32-bit binary data:
```bash
bin/skt_stream bin/skt_stream.xclbin 10000000 file.dat
```

## Kernel Source Index
- [Main Kernel Entry](src/krnl_skt_stream.cpp#L417)
- Core Sketch Functions:
	- [HLL Sketch](src/krnl_skt_stream.cpp#L84), [HLL Estimator](src/krnl_skt_stream.cpp#L18)
	- [Fast AGMS Sketch](src/krnl_skt_stream.cpp#L130)
	- [Count-Min Sketch](src/krnl_skt_stream.cpp#L191)
- Data Handling
	- [Basic Metrics](src/krnl_skt_stream.cpp#L260)
	- [Hash & Distirbute](src/krnl_skt_stream.cpp#L319)
	- [Result Aggregation](src/krnl_skt_stream.cpp#L340)
