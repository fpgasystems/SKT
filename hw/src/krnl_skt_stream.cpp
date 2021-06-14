#include "skt_stream.hpp"

#include "ap_axi_sdata.h"
#include "hls_stream.h"

#include <algorithm>
#include <cmath>
#include <iostream>

#include "streaming.hpp"
#include "distr.hpp"
#include "bit_utils.hpp"

using namespace hsl;

static unsigned const  N0 = 16;

template<
	unsigned H,	// Total Number of Hash Bits
	unsigned P	// Precision: Bits used to differentiate Buckets
>
class HllEstimator {
	static unsigned	constexpr	M		= 1u<<P;	// Number of Buckets
	static float	constexpr	ALPHAM2	= M * (M * (0.7213/(1+(1.079/M)))); // for M >= 128

	/**
	 * Exact accumulation until float conversion for output for M=2^P buckets:
	 *
	 * | <- P -> . <- H-P+1 -> |
	 *
	 * Numeric Range:
	 *	0.0 .. (2^P)*1.0
	 *
	 * All values are represented exact except for the maximum final result
	 * in the case that *all* buckets report a rank of zero, i.e. empty. The
	 * accumulator will then be ignored in favor of linear counting.
	 */
	using accu_t = ap_ufixed<H+1, P>;
	ap_uint<P+1>	zeros	= 0;
	accu_t			accu	= 0;

public:
	template<typename T_RANK>
	void estimate(
		hls::stream<flit_v_t<T_RANK>> &src,
		hls::stream<float> &dst
	) {
#pragma HLS pipeline II=1
		if(!src.empty()) {
			auto const  x = src.read();
			T_RANK const  rank = x.val;
			if(rank == 0)  zeros++;
			accu_t  d = 0;		// d = 2^(-rank)
			d[H-P+1 - rank] = 1;
			accu += d;

			if(x.last) {
				// Raw Cardinality
				float card = ALPHAM2 / accu.to_float();

				// Estimate Refinement
				if(card <= 2.5*M) {
					// Linear Counting if there are empty Buckets
					if(zeros != 0)  card = M*logf((float)M / (float)zeros);
				}

				// State Reset
				zeros = 0;
				accu  = 0;

				dst.write(card);
			}
		}
	} // estimate()

}; // HllEstimator

//===========================================================================
// Dependent internal types
using T_HASH = decltype(Hash()(ap_uint<32>{}));
using hashed_t = flit_v_t<T_HASH>;

//- HLL ---------------------------------------------------------------------
static void hll_sketch(
	hls::stream<hashed_t>	(&hashed)[N],
	hls::stream<float>		&cardest
) {
#pragma HLS inline
#pragma HLS interface ap_ctrl_none port=return
#pragma HLS dataflow disable_start_propagation

	using T_KEY  = ap_uint<P_HLL>;
	using T_RANK = ap_uint<clog2<T_HASH::width-T_KEY::width+2>::value>;
	using ranked_t = flit_kv_t<T_KEY, T_RANK>;

	// Rank Inputs
	static hls::stream<ranked_t>  ranked[N];
#pragma HLS aggregate variable=ranked
	for(int i = 0; i < N; i++) {
#pragma HLS unroll
		map(hashed[i], ranked[i], [](hashed_t const& x) -> ranked_t {
			T_HASH const  hash = x.val;
			T_KEY  const  buck = hash(T_HASH::width-1, T_HASH::width-P_HLL);
			T_RANK const  rank = 1 + clz(ap_uint<T_HASH::width-P_HLL>(hash));
			return  ranked_t{x.last, buck, rank};
		});
	}

	static hls::stream<flit_v_t<T_RANK>>  sketch[N];
#pragma HLS aggregate variable=sketch
	static Collect<T_KEY, T_RANK>  collect[N];
#pragma HLS array_partition variable=collect dim=1
	for(int  i = 0; i < N; i++) {
#pragma HLS unroll
		collect[i].collect(ranked[i], sketch[i], [](T_RANK a, T_RANK b) -> T_RANK{ return  std::max(a, b); });
	}

	static hls::stream<flit_v_t<T_RANK>>  sketch_joined;
#pragma HLS aggregate variable=sketch_joined
	reduce(sketch, sketch_joined, [](flit_v_t<T_RANK> a, flit_v_t<T_RANK> b) {
		return  flit_v_t<T_RANK>{b.last, std::max(a.val, b.val)};
	});

	static HllEstimator<T_HASH::width, P_HLL>  estimator;
	estimator.estimate(sketch_joined, cardest);

} // hll_sketch()

//- AGMS --------------------------------------------------------------------
static void agms_sketch(
	hls::stream<hashed_t>		(&hashed)[N],
	hls::stream<ap_uint<64>>	(&sumqs)[R_AGMS]
) {
#pragma HLS inline
#pragma HLS interface ap_ctrl_none port=return
#pragma HLS dataflow disable_start_propagation

	static_assert(T_HASH::width >= R_AGMS*(1+P_AGMS));
	using agmsed_t = flit_kv_t<ap_uint<P_AGMS>, ap_uint<1>>;
	using counted_t = flit_v_t<ap_int<W_AGMS>>;

	// Select Counter Indeces
	static hls::stream<agmsed_t>  agmsed[N][R_AGMS];
#pragma HLS aggregate variable=agmsed
	for(int i = 0; i < N; i++) {
#pragma HLS unroll
		split(hashed[i], agmsed[i], [](hashed_t const &x, int  idx) -> agmsed_t{
			ap_uint<1+P_AGMS> const  hash_slice = x.val(idx*(1+P_AGMS)+P_AGMS, idx*(1+P_AGMS));
			return  agmsed_t{x.last, hash_slice(P_AGMS-1, 0), hash_slice[P_AGMS]};
		});
	}

	// Collect +/-1 Counts
	static hls::stream<counted_t>  counts0[R_AGMS][N];
#pragma HLS aggregate variable=counts0
	static Collect<ap_uint<P_AGMS>, ap_int<W_AGMS>>  collect[R_AGMS*N];
#pragma HLS array_partition variable=collect dim=1
	for(int  i = 0; i < N*R_AGMS; i++) {
#pragma HLS unroll
		collect[i].collect(
			agmsed[i/R_AGMS][i%R_AGMS], counts0[i%R_AGMS][i/R_AGMS],
			[](ap_int<W_AGMS> const& a, ap_uint<1> const& b) -> ap_int<W_AGMS> {
				return  a + (b? 1 : -1);
			}
		);
	}

	// Merge parallel Sketches
	static hls::stream<counted_t>  counts[R_AGMS];
#pragma HLS aggregate variable=counts
	for(int j = 0; j < R_AGMS; j++) {
#pragma HLS unroll
		reduce(counts0[j], counts[j], [](counted_t const& a, counted_t const& b) {
			return  counted_t{b.last, a.val+b.val};
		});
	}

	// Sum up each Row
	static Fold<ap_uint<64>>  folds[R_AGMS];
#pragma HLS array_partition variable=folds dim=1
	for(int  j = 0; j < R_AGMS; j++) {
#pragma HLS unroll
		folds[j].fold(counts[j], sumqs[j], [](ap_uint<64> const& a, ap_int<W_AGMS> const& b) {
			return  a + b*b;
		}, 0);
	}

} // agms_sketch()

//- CountMin ----------------------------------------------------------------
static void cm_sketch(
	hls::stream<hashed_t>					(&hashed)[N],
	hls::stream<flit_v_t<ap_uint<W_CM>>>	(&cm_counts)
) {
#pragma HLS inline
#pragma HLS interface ap_ctrl_none port=return
#pragma HLS dataflow disable_start_propagation

	static_assert(T_HASH::width >= R_CM*P_CM);
	using cmined_t = flit_kv_t<ap_uint<P_CM>, ap_uint<W_CM>>;
	using counted_t = flit_v_t<ap_uint<W_CM>>;

	// Select Counter Indices
	static hls::stream<cmined_t>  cmined[N][R_CM];
#pragma HLS aggregate variable=cmined
	for(int i = 0; i < N; i++) {
#pragma HLS unroll
		split(hashed[i], cmined[i], [](hashed_t const &x, int  idx) -> cmined_t{
			ap_uint<P_CM> const  hash_slice = x.val(P_CM*idx+P_CM-1, P_CM*idx);
			return  cmined_t{x.last, hash_slice, 1};
		});
	}

	// Collect Counts
	static hls::stream<counted_t>  counts0[R_CM][N];
#pragma HLS aggregate variable=counts0
	static /*U*/Collect<ap_uint<P_CM>, ap_uint<W_CM>>  collect[R_CM*N];
#pragma HLS array_partition variable=collect dim=1 complete
	for(int  i = 0; i < N*R_CM; i++) {
#pragma HLS unroll
		collect[i].collect(
			cmined[i/R_CM][i%R_CM]/*aggr_in[i]*/, counts0[i%R_CM][i/R_CM],
			[](ap_uint<W_CM> const& a, ap_uint<W_CM> const& b) -> ap_uint<W_CM> {
				return  a + b;
			}
		);
	}

	// Merge parallel Sketches
	static hls::stream<counted_t>  counts[R_CM];
#pragma HLS aggregate variable=counts
	for(int j = 0; j < R_CM; j++) {
#pragma HLS unroll
		reduce(counts0[j], counts[j], [](counted_t const& a, counted_t const& b) {
			return  counted_t{b.last, a.val+b.val};
		});
	}

	// Stream all rows sequentially
	static Concat<R_CM>  concat;
	concat.concat(counts, cm_counts);

} // cm_sketch()

struct basic_summary_t {
	ap_uint<32>  min;
	ap_uint<32>  max;
	ap_uint<32+clog2<N>::value>  sum;
	ap_uint<64+clog2<N>::value>  sumq;

public:
	basic_summary_t(
		ap_uint<32>  _min = -1,
		ap_uint<32>  _max =  0,
		ap_uint<32+clog2<N>::value>  _sum  = 0,
		ap_uint<64+clog2<N>::value>  _sumq = 0
	) : min(_min), max(_max), sum(_sum), sumq(_sumq) {}
};

static void basic_sketch(
	hls::stream<flit_v_t<basic_summary_t>> &src_basic,
	hls::stream<flit_v_t<ap_uint<64>>>     &dst_basic
) {
#pragma HLS pipeline II=1
	static ap_uint<32>  cnt  =  0;
	static ap_uint<32>  min  = -1;
	static ap_uint<32>  max  =  0;
	static ap_uint<64>  sum  =  0;
	static ap_uint<96>  sumq =  0;
	static bool        dump = 0;
	static ap_uint<2>  didx = 0;

	if(!dump) {
		if(!src_basic.empty()) {
			auto const  x = src_basic.read();

			cnt += N0;
			min   = std::min(min, x.val.min);
			max   = std::max(max, x.val.max);
			sum  += x.val.sum;
			sumq += x.val.sumq;

			dump = x.last;
		}
	}
	else {
		bool const  olast = (didx == 3);

		ap_uint<64>  odat;
		switch(didx) {
		case 0:
			odat = (sumq(95, 64), cnt);
			break;
		case 1:
			odat = (max, min);
			break;
		case 2:
			odat = sum;
			break;
		case 3:
			odat = sumq(63, 0);
			break;
		}
		dst_basic.write(flit_v_t<ap_uint<64>>{ olast, odat });
		didx++;

		if(olast) {
			cnt  =  0;
			min  = -1;
			max  =  0;
			sum  =  0;
			sumq =  0;

			dump = false;
		}
	}
} // basic_sketch

static unsigned const  H = T_HASH::width;
template<int M>
void distribute(
	hls::stream<flit_v_t<ap_uint<32>>> (&src_flat)[N],
	hls::stream<hashed_t>              (&hashed)[M][N]
){
#pragma HLS pipeline II=1
	for(int i = 0; i < N; i++) {
#pragma HLS unroll
		flit_v_t<ap_uint<32>>  x;
		if(src_flat[i].read_nb(x)) {
			bool const  last = x.last;
			ap_uint<H> const  hash = Hash()(x.val);
			for(int j = 0; j < M; j++) {
#pragma HLS unroll
				hashed[j][i].write(hashed_t{last, hash});
			}
		}
	}
} // distribute()

static void compose(
	hls::stream<bool>					&tick_src,
	hls::stream<flit_v_t<ap_uint<64>>>	&dst_basic,
	hls::stream<float>					&cardest,
	hls::stream<ap_uint<64>>			(&agms_sums)[R_AGMS],
	hls::stream<flit_v_t<ap_uint<W_CM>>>	&cm_counts,
	hls::stream<flit_v_t<ap_uint<64>>>	&dst
) {
#pragma HLS pipeline II=1
	static ap_uint<32>  cc = 0;
	if(!tick_src.empty()) {
		tick_src.read();
		cc = 0;
	}
	cc++;

	static ap_uint<clog2<R_AGMS+3>::value>  idx = 0;

	bool  ostb = false;
	bool  olst;
	ap_uint<64>  odat;
	if(idx < R_AGMS) {
		if(agms_sums[idx].read_nb(odat)) {
			ostb = true;
			olst = false;
			idx++;
		}
	}
	else if(idx == R_AGMS) {
		if(!dst_basic.empty()) {
			auto const  x = dst_basic.read();

			ostb = true;
			olst = false;
			odat = x.val;

			if(x.last)  idx++;
		}
	}
	else if(idx == R_AGMS+1) {
		if(!cardest.empty()) {
			float const  x = cardest.read();
			union { float f; uint32_t i; } const  conv = { .f = x };

			ostb = true;
			olst = false;
			odat(31,  0) = conv.i;
			odat(63, 32) = cc;

			idx++;
		}
	}
	else {
		if(!cm_counts.empty()) {
			static struct { bool vld; ap_uint<32> val; } lo = { false, };
			auto const  x = cm_counts.read();
			if(!lo.vld)  lo = { .vld = true, .val = x.val };
			else {
				ostb = true;
				olst = x.last;
				odat = (x.val, lo.val);
				lo.vld = false;

				if(x.last)  idx = 0;
			}
		}
	}
	if(ostb)  dst.write(flit_v_t<ap_uint<64>>{olst, odat});

} // compose()


// Streaming Interface Payload
using T_AXIN0 = qdma_axis<N0*32, 0, 0, 0>;
using T_AXI64 = qdma_axis<64, 0, 0, 0>;

// Kernel Implementation
extern "C"
void krnl_skt_stream(
	hls::stream<T_AXIN0> &src,
	hls::stream<T_AXI64> &dst
) {
#pragma HLS interface axis port=src
#pragma HLS interface axis port=dst
#pragma HLS interface ap_ctrl_none port=return

#pragma HLS dataflow disable_start_propagation
 /**
  * This infinite wrapping loop:
  *	- is needed by SW Emulation, which will halt pre-maturely otherwise.
  *	- is redundant for HW design and, in fact, degrades HLS synthesis quality.
  */
#ifndef __SYNTHESIS__
	while(true) {
#endif

	// Source Buffer
	static hls::stream<flit_v_t<ap_uint<N0*32>>>  src_buf;
#pragma HLS aggregate variable=src_buf
#pragma HLS stream variable=src_buf depth=32768
#pragma HLS bind_storage variable=src_buf type=fifo impl=uram
	map(src, src_buf, [](T_AXIN0 const& x) -> flit_v_t<ap_uint<N0*32>> {
		return  flit_v_t<ap_uint<N0*32>>{ x.get_last(), x.get_data() };
	});

	// Unpack and Feed Tick Count
	static hls::stream<bool>  tick_src;
	static hls::stream<flit_v_t<ap_uint<N0*32>>>  src_flat[2];
#pragma HLS aggregate variable=src_flat
	static bool  src_last = true;
	split(src_buf, src_flat, [](flit_v_t<ap_uint<N0*32>> const& x, unsigned idx) -> flit_v_t<ap_uint<N0*32>> {
		bool const  last = x.last;
		if(idx == 0) {
			if(src_last)  tick_src.write(true);
			src_last = last;
		}
		return  flit_v_t<ap_uint<N0*32>>{last, x.val};
	});

	// Basic Sketch
	static hls::stream<flit_v_t<basic_summary_t>>  src_basic;
#pragma HLS aggregate variable=src_basic
	map(src_flat[0], src_basic, [](flit_v_t<ap_uint<N0*32>> const& x) {
		basic_summary_t  y;
		for(int i = 0; i < N0; i++) {
#pragma HLS unroll
			ap_uint<32>  xx;
			xx = x.val(i*32+31, i*32);
			y.min   = std::min(y.min, xx);
			y.max   = std::max(y.max, xx);
			y.sum  += xx;
			y.sumq += xx*xx;
		}
		return  flit_v_t<basic_summary_t>{ x.last, y };
	});

	static hls::stream<flit_v_t<ap_uint<64>>>  dst_basic;
#pragma HLS aggregate variable=dst_basic
	basic_sketch(src_basic, dst_basic);

	// Pipeline Funnel
	hls::stream<flit_v_t<ap_uint<32>>>  src_funnelled[N];
#pragma HLS aggregate variable=src_funnelled
	distr_funnel<N0, N>(src_flat[1], src_funnelled);

	// Hash Inputs
	static hls::stream<hashed_t>  hashed[3][N];
#pragma HLS aggregate variable=hashed
#pragma HLS array_partition variable=hashed dim=0 complete
	distribute(src_funnelled, hashed);

	// Compute HLL Sketch
	static hls::stream<float>  cardest;
#pragma HLS aggregate variable=cardest
	hll_sketch(hashed[0], cardest);

	// Compute AGMS Sketch
	static hls::stream<ap_uint<64>>	agms_sums[R_AGMS];
	agms_sketch(hashed[1], agms_sums);

	// Compute CountMin Sketch
	static hls::stream<flit_v_t<ap_uint<W_CM>>>  cm_counts;
	cm_sketch(hashed[2], cm_counts);

	// Compose Output
	static hls::stream<flit_v_t<ap_uint<64>>>  dst0;
#pragma HLS aggregate variable=dst0
	compose(tick_src, dst_basic, cardest, agms_sums, cm_counts, dst0);

	map(dst0, dst, [](flit_v_t<ap_uint<64>> const& x) -> T_AXI64 {
		T_AXI64  y;
		y.set_data(x.val);
		y.set_keep(-1);
		y.set_last(x.last);
		return  y;
	});

#ifndef __SYNTHESIS__
	}
#endif
} // krnl_skt_stream
