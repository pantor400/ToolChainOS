#include <stddef.h>
/* Rutinas freestanding: GCC puede necesitarlas incluso sin libc. */
void *memcpy(void *restrict dst, const void *restrict src, size_t n) {
    unsigned char *d=dst; const unsigned char *s=src;
    for (size_t i=0;i<n;i++) d[i]=s[i];
    return dst;
}
void *memset(void *dst, int value, size_t n) {
    unsigned char *d=dst;
    for (size_t i=0;i<n;i++) d[i]=(unsigned char)value;
    return dst;
}
void *memmove(void *dst, const void *src, size_t n) {
    unsigned char *d=dst; const unsigned char *s=src;
    /* uintptr_t evita comparar punteros de objetos diferentes. */
    if ((size_t)d < (size_t)s) for(size_t i=0;i<n;i++) d[i]=s[i];
    else for(size_t i=n;i>0;i--) d[i-1]=s[i-1];
    return dst;
}
int memcmp(const void *a, const void *b, size_t n) {
    const unsigned char *x=a,*y=b;
    for(size_t i=0;i<n;i++) if(x[i]!=y[i]) return (int)x[i]-(int)y[i];
    return 0;
}
