#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <emmintrin.h>

namespace t3 {
inline int64_t rounded_integer(double value) {
#ifdef T3_SSE_ROUND
    // SSE2 is part of the amd64 baseline. CVTSD2SI follows the same floating
    // rounding mode as nearbyint and avoids a separate libm call/conversion.
    return _mm_cvtsd_si64(_mm_set_sd(value));
#else
    return int64_t(std::nearbyint(value));
#endif
}
// RandomState's scalar-seeded MT19937, 53-bit doubles, cached polar normal,
// masked bounded integers. Distribution order is part of the market contract.
class Random {
    std::array<uint32_t,624> state;
#ifdef T3_BUFFERED_MT
    std::array<uint32_t,624> tempered{};
#endif
    int position=624;
    bool cached=false;
    double spare=0;
#ifdef T3_VECTOR_MT
    void twist(int begin,int end,int offset) {
        const __m128i high=_mm_set1_epi32(int32_t(0x80000000U)),low=_mm_set1_epi32(0x7fffffff);
        const __m128i one=_mm_set1_epi32(1),magic=_mm_set1_epi32(int32_t(0x9908b0dfU));
        int i=begin;
        for(;i+4<=end;i+=4) {
            __m128i current=_mm_loadu_si128(reinterpret_cast<const __m128i*>(state.data()+i));
            __m128i next=_mm_loadu_si128(reinterpret_cast<const __m128i*>(state.data()+i+1));
            __m128i y=_mm_or_si128(_mm_and_si128(current,high),_mm_and_si128(next,low));
            __m128i adjustment=_mm_and_si128(_mm_sub_epi32(_mm_setzero_si128(),_mm_and_si128(y,one)),magic);
            __m128i result=_mm_xor_si128(_mm_loadu_si128(reinterpret_cast<const __m128i*>(state.data()+i+offset)),
                                       _mm_xor_si128(_mm_srli_epi32(y,1),adjustment));
            _mm_storeu_si128(reinterpret_cast<__m128i*>(state.data()+i),result);
        }
        for(;i<end;++i) {
            uint32_t y=(state[i]&0x80000000U)|(state[i+1]&0x7fffffffU);
            state[i]=state[i+offset]^(y>>1)^((y&1)?0x9908b0dfU:0);
        }
    }
#endif
public:
    explicit Random(uint32_t seed=0) {
        state[0]=seed;
        for (uint32_t i=1;i<624;++i)
            state[i]=1812433253U*(state[i-1]^(state[i-1]>>30))+i;
    }
    uint32_t word() {
        if (position==624) {
#ifdef T3_VECTOR_MT
            twist(0,227,397);twist(227,623,-227);
#else
            for (int i=0;i<227;++i) {
                uint32_t y=(state[i]&0x80000000U)|(state[i+1]&0x7fffffffU);
                state[i]=state[i+397]^(y>>1)^((y&1)?0x9908b0dfU:0);
            }
            for (int i=227;i<623;++i) {
                uint32_t y=(state[i]&0x80000000U)|(state[i+1]&0x7fffffffU);
                state[i]=state[i-227]^(y>>1)^((y&1)?0x9908b0dfU:0);
            }
#endif
            uint32_t y=(state[623]&0x80000000U)|(state[0]&0x7fffffffU);
            state[623]=state[396]^(y>>1)^((y&1)?0x9908b0dfU:0);
#ifdef T3_BUFFERED_MT
            // Temper a complete refill with baseline SSE2 integer operations.
            // The untempered MT state is retained for the next recurrence.
            const __m128i mask7=_mm_set1_epi32(int32_t(0x9d2c5680U)),mask15=_mm_set1_epi32(int32_t(0xefc60000U));
            for(int i=0;i<624;i+=4) {
                __m128i words=_mm_loadu_si128(reinterpret_cast<const __m128i*>(state.data()+i));
                words=_mm_xor_si128(words,_mm_srli_epi32(words,11));
                words=_mm_xor_si128(words,_mm_and_si128(_mm_slli_epi32(words,7),mask7));
                words=_mm_xor_si128(words,_mm_and_si128(_mm_slli_epi32(words,15),mask15));
                words=_mm_xor_si128(words,_mm_srli_epi32(words,18));
                _mm_storeu_si128(reinterpret_cast<__m128i*>(tempered.data()+i),words);
            }
#endif
            position=0;
        }
#ifdef T3_BUFFERED_MT
        return tempered[position++];
#else
        uint32_t y=state[position++];
        y^=y>>11; y^=(y<<7)&0x9d2c5680U; y^=(y<<15)&0xefc60000U;
        return y^(y>>18);
#endif
    }
    double uniform01() {
        uint32_t a=word()>>5,b=word()>>6;
        return (a*67108864.0+b)*(1.0/9007199254740992.0);
    }
    static uint64_t mask_for(uint64_t maximum) {
        uint64_t mask=maximum;
        mask|=mask>>1;mask|=mask>>2;mask|=mask>>4;mask|=mask>>8;mask|=mask>>16;mask|=mask>>32;
        return mask;
    }
    uint64_t bounded_prepared(uint64_t maximum,uint64_t mask) {
        if (!maximum) return 0;
        uint64_t value;
        do {
            if (maximum<=UINT32_MAX) value=word();
            else { value=uint64_t(word())<<32; value|=word(); }
            value&=mask;
        } while (value>maximum);
        return value;
    }
    uint64_t bounded(uint64_t upper) {
        if (!upper) throw std::invalid_argument("zero random bound");
        return bounded_prepared(upper-1,mask_for(upper-1));
    }
    double normal(double mean,double scale) {
        double gaussian;
        if (cached) { gaussian=spare; cached=false; spare=0; }
        else {
            double u,v,r;
            do { u=2*uniform01()-1; v=2*uniform01()-1; r=u*u+v*v; } while (r==0 || r>=1);
            double f=std::sqrt((-2*std::log(r))/r);
            spare=u*f; cached=true; gaussian=v*f;
        }
        return mean+scale*gaussian;
    }
    double exponential(double scale=1) { return scale*(-std::log(1-uniform01())); }
    double uniform(double low,double high) { return low+(high-low)*uniform01(); }
    double lognormal(double mean,double scale) { return std::exp(normal(mean,scale)); }
    double pareto(double alpha) { return std::exp(exponential()/alpha)-1; }
};
}
