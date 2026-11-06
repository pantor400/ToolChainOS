#ifndef WEB_H
#define WEB_H
#include <stdint.h>
#define WEB_URL_MAX 1023
#define WEB_DOC_MAX 24576
/* 10s host budget + up to 16s delivery through a polled 16-byte UART FIFO. */
#define WEB_RESPONSE_TICKS 3000u
/* Asynchronous, one request. poll: 0 pending, >0 document bytes,
 * -1 no bridge, -2 timeout, -3 malformed/oversize transport. */
int web_start(const char *url);
int web_poll(void *out, uint32_t capacity);
void web_cancel(void);
#endif
