/* For tests/test_catalog.cc, which builds the core without a decimal library
 * (FP_BINARY): the conversions only N->DS and the like use, not the Catalog. */
#ifndef INTEL_CONV_H
#define INTEL_CONV_H
#include <stdint.h>
typedef uint32_t BID_UINT32;
typedef uint64_t BID_UINT64;
typedef struct { uint64_t w[2]; } BID_UINT128;
#define STUB(name) static inline void name(void *, const void *) {}
STUB(binary64_to_bid32)
STUB(binary64_to_bid64)
STUB(binary64_to_bid128)
STUB(bid32_to_binary64)
STUB(bid64_to_binary64)
STUB(bid128_to_binary64)
STUB(bid_to_dpd32)
STUB(bid_to_dpd64)
STUB(bid_to_dpd128)
STUB(bid_dpd_to_bid32)
STUB(bid_dpd_to_bid64)
STUB(bid_dpd_to_bid128)
#undef STUB
#endif
