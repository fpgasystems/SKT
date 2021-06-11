#ifndef HSL_DISTR_HPP
#define HSL_DISTR_HPP

#include "streaming.hpp"

template<int N, int K, typename std::enable_if<(K<N), bool>::type = true>
void distr_funnel(
	hls::stream<hsl::flit_v_t<ap_uint<N*32>>> &src,
	hls::stream<hsl::flit_v_t<ap_uint<32>>>  (&dst)[K]
) {
#pragma HLS pipeline II=1
	static_assert(K < N, "No datapath narrowing.");

	static hsl::flit_v_t<ap_uint<32>>       Buf[N+K-1];
#pragma HLS array_partition variable=Buf complete dim=1
	static ap_uint<hsl::clog2<N+K>::value>  Cnt = 0;

	bool const  fullout = Cnt >= K;
	if(fullout || Buf[0].last) {
		for(int  i = 0; i < K; i++) {
#pragma HLS unroll
			if(i < Cnt)  dst[i].write(Buf[i]);
		}
		for(int  i = 0; i < N-1; i++) {
#pragma HLS unroll
			Buf[i] = Buf[i+K];
		}
		if(fullout)  Cnt -= K; else Cnt = 0;
	}

	if((Cnt < K) && !src.empty()) {
		hsl::flit_v_t<ap_uint<N*32>> const  x = src.read();

		// Split input into indexable lanes and mark last K with last
		hsl::flit_v_t<ap_uint<32>>  x_split[N];
#pragma HLS array_partition variable=x_split complete dim=1
		for(int  i = 0; i < N; i++) {
#pragma HLS unroll
			x_split[i] = hsl::flit_v_t<ap_uint<32>>{
				x.last && (i >= N-K),
				x.val(32*i+31, 32*i)
			};
		}

		hsl::flit_v_t<ap_uint<32>>  x_aligned[N];
#pragma HLS array_partition variable=x_aligned complete dim=1
		for(int  i = 0; i < N; i++) {
#pragma HLS unroll
			x_aligned[i] = x_split[(N+i-Cnt)%N];
		}

		for(int  i = 0; i < N+K-1; i++) {
#pragma HLS unroll
			if(i >= Cnt)  Buf[i] = x_aligned[i%N];
		}
		Cnt += N;
	}

} // distr_funnel

template<int N, int K, typename std::enable_if<(K==N), bool>::type = true>
void distr_funnel(
	hls::stream<hsl::flit_v_t<ap_uint<N*32>>> &src,
	hls::stream<hsl::flit_v_t<ap_uint<32>>>  (&dst)[K]
) {
#pragma HLS inline
	hsl::split(src, dst, [](hsl::flit_v_t<ap_uint<N*32>> const &x, int  idx) -> hsl::flit_v_t<ap_uint<32>>{
		return  hsl::flit_v_t<ap_uint<32>>{ x.last, x.val(32*idx+31, 32*idx) };
	});
} // distr_funnel

#endif
