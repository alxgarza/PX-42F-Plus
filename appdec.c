#include "appdec.h"

#include <string.h>

#include "bid_conf.h"
#include "bid_functions.h"

/* The library's own type, by reference, as Plus42 calls it (CMakeLists:
 * DECIMAL_CALL_BY_REFERENCE, global rounding and flags -- Plus42 never
 * reads the flags nor changes the rounding, so an app using them leaves
 * the calculator's arithmetic as it was). */
_Static_assert(sizeof(px_dec_t) == sizeof(BID_UINT128), "px_dec_t is a BID_UINT128");
#define BID(p) ((BID_UINT128 *) (p))

int appdec_op(int op, px_dec_t *r, const px_dec_t *a, const px_dec_t *b) {
    BID_UINT128 x = *BID(a), y = b != NULL ? *BID(b) : x, z;
    switch (op) {
        case PX_DEC_ADD: bid128_add(&z, &x, &y); break;
        case PX_DEC_SUB: bid128_sub(&z, &x, &y); break;
        case PX_DEC_MUL: bid128_mul(&z, &x, &y); break;
        case PX_DEC_DIV: bid128_div(&z, &x, &y); break;
        case PX_DEC_POW: bid128_pow(&z, &x, &y); break;
        case PX_DEC_REM: bid128_rem(&z, &x, &y); break;
        case PX_DEC_ATAN2: bid128_atan2(&z, &x, &y); break;
        case PX_DEC_HYPOT: bid128_hypot(&z, &x, &y); break;
        case PX_DEC_NEG: bid128_negate(&z, &x); break;
        case PX_DEC_ABS: bid128_abs(&z, &x); break;
        case PX_DEC_SQRT: bid128_sqrt(&z, &x); break;
        case PX_DEC_EXP: bid128_exp(&z, &x); break;
        case PX_DEC_LN: bid128_log(&z, &x); break;
        case PX_DEC_LOG10: bid128_log10(&z, &x); break;
        case PX_DEC_EXPM1: bid128_expm1(&z, &x); break;
        case PX_DEC_LN1P: bid128_log1p(&z, &x); break;
        case PX_DEC_SIN: bid128_sin(&z, &x); break;
        case PX_DEC_COS: bid128_cos(&z, &x); break;
        case PX_DEC_TAN: bid128_tan(&z, &x); break;
        case PX_DEC_ASIN: bid128_asin(&z, &x); break;
        case PX_DEC_ACOS: bid128_acos(&z, &x); break;
        case PX_DEC_ATAN: bid128_atan(&z, &x); break;
        case PX_DEC_SINH: bid128_sinh(&z, &x); break;
        case PX_DEC_COSH: bid128_cosh(&z, &x); break;
        case PX_DEC_TANH: bid128_tanh(&z, &x); break;
        case PX_DEC_ASINH: bid128_asinh(&z, &x); break;
        case PX_DEC_ACOSH: bid128_acosh(&z, &x); break;
        case PX_DEC_ATANH: bid128_atanh(&z, &x); break;
        case PX_DEC_GAMMA: bid128_tgamma(&z, &x); break;
        case PX_DEC_FLOOR: bid128_round_integral_negative(&z, &x); break;
        case PX_DEC_CEIL: bid128_round_integral_positive(&z, &x); break;
        case PX_DEC_TRUNC: bid128_round_integral_zero(&z, &x); break;
        default: return -1;
    }
    *BID(r) = z;
    return 0;
}

void appdec_from_int(px_dec_t *r, int32_t v) {
    int i = (int) v;
    bid128_from_int32(BID(r), &i);
}

int appdec_class(const px_dec_t *a) {
    BID_UINT128 x = *BID(a);
    int nan, inf;
    bid128_isNaN(&nan, &x);
    bid128_isInf(&inf, &x);
    return nan ? PX_DEC_NAN : inf ? PX_DEC_INF : PX_DEC_FINITE;
}

int appdec_to_int(int32_t *r, const px_dec_t *a) {
    if (appdec_class(a) != PX_DEC_FINITE)
        return -1;
    BID_UINT128 x = *BID(a), lo, hi;
    int below, above, i, min = INT32_MIN, max = INT32_MAX;
    bid128_from_int32(&lo, &min);
    bid128_from_int32(&hi, &max);
    BID_UINT128 t;
    bid128_round_integral_zero(&t, &x);
    bid128_quiet_less(&below, &t, &lo);
    bid128_quiet_greater(&above, &t, &hi);
    if (below || above)
        return -1;
    bid128_to_int32_xint(&i, &t);
    *r = (int32_t) i;
    return 0;
}

int appdec_from_string(px_dec_t *r, const char *s) {
    char buf[PX_DEC_STRING_MAX + 16];
    size_t n = strnlen(s, sizeof(buf));
    if (n == 0 || n == sizeof(buf)) {
        bid128_from_string(BID(r), (char *) "NaN");
        return -1;
    }
    memcpy(buf, s, n + 1);
    bid128_from_string(BID(r), buf);
    return appdec_class(r) == PX_DEC_NAN ? -1 : 0;
}

void appdec_to_string(char buf[PX_DEC_STRING_MAX], const px_dec_t *a) {
    BID_UINT128 x = *BID(a);
    bid128_to_string(buf, &x);
}

int appdec_cmp(const px_dec_t *a, const px_dec_t *b) {
    BID_UINT128 x = *BID(a), y = *BID(b);
    int less, equal;
    if (appdec_class(a) == PX_DEC_NAN || appdec_class(b) == PX_DEC_NAN)
        return 2;
    bid128_quiet_less(&less, &x, &y);
    bid128_quiet_equal(&equal, &x, &y);
    return less ? -1 : equal ? 0 : 1;
}
