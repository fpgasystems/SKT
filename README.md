# SKT - A One-Pass Multi-Sketch

SKT is a C++-implemented HLS kernel for computing multiple sketches (HyperLogLog, Fast-AGMS and Count-Min)
over the data input in one pass. This complete set is accommodated on a single Alveo U250 accelerator
card operated under the Xilinx QDMA shell without backpressure on the PCIe data link. A detailed
anylysis of possible design trade-offs in terms of sketch dimensions and input bandwidth will be
shared shortly.

## Repo Structure
- [`sw`](sw/) - a SKT software reference implementation
- [`hw`](hw/) - the SKT hardware accelerator kernel + example host application
