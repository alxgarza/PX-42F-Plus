#ifndef APPDEC_H
#define APPDEC_H

#include "px_app.h"

#ifdef __cplusplus
extern "C" {
#endif

/* px_app.h's decimal numbers, on the Intel decimal library Plus42 already
 * links (src/core/bid_functions.h): the table's dec* entries (app.c). */
int appdec_op(int op, px_dec_t *r, const px_dec_t *a, const px_dec_t *b);
void appdec_from_int(px_dec_t *r, int32_t v);
int appdec_to_int(int32_t *r, const px_dec_t *a);
int appdec_from_string(px_dec_t *r, const char *s);
void appdec_to_string(char buf[PX_DEC_STRING_MAX], const px_dec_t *a);
int appdec_cmp(const px_dec_t *a, const px_dec_t *b);
int appdec_class(const px_dec_t *a);

#ifdef __cplusplus
}
#endif

#endif
