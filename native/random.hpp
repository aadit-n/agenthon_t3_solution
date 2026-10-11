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
    int position=624;
    bool cached=false;
    double spare=0;
public:
    explicit Random(uint32_t seed=0) {
        state[0]=seed;
        for (uint32_t i=1;i<624;++i)
            state[i]=1812433253U*(state[i-1]^(state[i-1]>>30))+i;
    }
    uint32_t word() {
        if (position==624) {
            for (int i=0;i<227;++i) {
                uint32_t y=(state[i]&0x80000000U)|(state[i+1]&0x7fffffffU);
                state[i]=state[i+397]^(y>>1)^((y&1)?0x9908b0dfU:0);
            }
            for (int i=227;i<623;++i) {
                uint32_t y=(state[i]&0x80000000U)|(state[i+1]&0x7fffffffU);
                state[i]=state[i-227]^(y>>1)^((y&1)?0x9908b0dfU:0);
            }
            uint32_t y=(state[623]&0x80000000U)|(state[0]&0x7fffffffU);
            state[623]=state[396]^(y>>1)^((y&1)?0x9908b0dfU:0);
            position=0;
        }
        uint32_t y=state[position++];
        y^=y>>11; y^=(y<<7)&0x9d2c5680U; y^=(y<<15)&0xefc60000U;
        return y^(y>>18);
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
