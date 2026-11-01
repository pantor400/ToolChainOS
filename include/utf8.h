#ifndef UTF8_H
#define UTF8_H
#include <stddef.h>
#include <stdint.h>
typedef struct {uint8_t bytes[4];unsigned count;} utf8_state_t;
/* 1 complete, 0 incomplete, -1 invalid. No overlongs/surrogates/out-of-range. */
int utf8_decode(const uint8_t *s,size_t n,uint32_t *cp,size_t *consumed);
int utf8_feed(utf8_state_t *state,uint8_t byte,uint32_t *cp);
unsigned utf8_encode(uint32_t cp,char out[4]);
int utf8_valid(const char *s,size_t n);
size_t utf8_previous(const char *s,size_t n);
unsigned utf8_columns(uint32_t cp);
#endif
