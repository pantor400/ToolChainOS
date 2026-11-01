#ifndef UTIL_H
#define UTIL_H
#include <stddef.h>
#include <stdint.h>
static inline size_t slen(const char *s){size_t n=0;while(s&&s[n])n++;return n;}
static inline void copystr(char *d,const char *s,size_t cap){size_t i=0;if(!cap)return;while(s&&s[i]&&i+1<cap){d[i]=s[i];i++;}d[i]=0;}
static inline int same(const char *a,const char *b){while(*a&&*a==*b){a++;b++;}return *a==*b;}
static inline int ci_same(const char *a,const char *b){for(;;a++,b++){char x=*a,y=*b;if(x>='a'&&x<='z')x-=32;if(y>='a'&&y<='z')y-=32;if(x!=y)return 0;if(!x)return 1;}}
static inline void zero(void *p,size_t n){unsigned char *d=p;while(n--)*d++=0;}
static inline int udecimal(const char *s,uint64_t *out){uint64_t n=0;if(!*s)return 0;while(*s){if(*s<'0'||*s>'9'||n>(UINT64_MAX-(*s-'0'))/10)return 0;n=n*10+*s++-'0';}*out=n;return 1;}
#endif
