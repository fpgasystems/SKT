# Expected Environment (and example values)
#	XILINX_VITIS=/opt/Xilinx/Vitis/2020.1
#	XILINX_VIVADO=/opt/Xilinx/Vivado/2020.1
#	XILINX_XRT=/opt/xilinx/xrt
ifndef XILINX_XRT
  $(error Please, ensure that XILINX_XRT is defined in environment.)
endif
ifndef XILINX_VITIS
  $(error Please, ensure that XILINX_VITIS is defined in environment.)
endif
ifndef XILINX_VIVADO
  $(error Please, ensure that XILINX_VIVADO is defined in environment.)
endif

VITIS_PLATFORM := xilinx_u250_qdma_201920_1
CXXFLAGS := -std=c++0x -O2 -Wall -g \
	-DVITIS_PLATFORM=$(VITIS_PLATFORM) -D__USE_XOPEN2K8 \
	-I$(XILINX_XRT)/include/ -I$(XILINX_VIVADO)/include

.PHONY: all host hw clean
all: host hw
host: skt_stream
hw:   skt_stream.xclbin

#----------------------------------------------------------------------------
# Build Host Application
skt_stream: skt_stream.o
	$(CXX) -o$@ $^ -Wl,-rpath-link,$(XILINX_XRT)/lib -L$(XILINX_XRT)/lib \
	-lxilinxopencl -lrt

skt_stream.o: src/skt_stream.cpp src/skt_stream.hpp
	$(CXX) $(CXXFLAGS) -c -o$@ $<

#----------------------------------------------------------------------------
# Build Kernel
skt_stream.xclbin: krnl_skt_stream.xo
	v++ --target hw --link --config common-config.cfg --config krnl_skt_stream-link.cfg -o$@ $<

krnl_skt_stream.xo: src/krnl_skt_stream.cpp src/skt_stream.hpp src/bit_utils.hpp src/streaming.hpp src/distr.hpp
	v++ --target hw --config common-config.cfg --config krnl_skt_stream-compile.cfg \
		-I/usr/include/x86_64-linux-gnu -o$@ $<

#----------------------------------------------------------------------------
# Clean-up
clean:
	rm -rf skt_stream skt_stream.o krnl_skt_stream.xo*
