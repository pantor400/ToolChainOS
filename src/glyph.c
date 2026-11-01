#include "glyph.h"
#include "font.h"
struct accent {uint32_t cp;uint8_t base,mark;};
static const struct accent accents[]={
 {0xe1,'a',1},{0xe9,'e',1},{0xed,'i',1},{0xf3,'o',1},{0xfa,'u',1},
 {0xc1,'A',1},{0xc9,'E',1},{0xcd,'I',1},{0xd3,'O',1},{0xda,'U',1},
 {0xf1,'n',3},{0xd1,'N',3},{0xfc,'u',2},{0xdc,'U',2},
 {0xe4,'a',2},{0xeb,'e',2},{0xef,'i',2},{0xf6,'o',2},
 {0xc4,'A',2},{0xcb,'E',2},{0xcf,'I',2},{0xd6,'O',2},
 {0xe0,'a',4},{0xe8,'e',4},{0xec,'i',4},{0xf2,'o',4},{0xf9,'u',4},
 {0xc0,'A',4},{0xc8,'E',4},{0xcc,'I',4},{0xd2,'O',4},{0xd9,'U',4},
 {0xe2,'a',5},{0xea,'e',5},{0xee,'i',5},{0xf4,'o',5},{0xfb,'u',5},
 {0xc2,'A',5},{0xca,'E',5},{0xce,'I',5},{0xd4,'O',5},{0xdb,'U',5}
};
static int custom(uint32_t c){
 return c==0xbf||c==0xa1||c==0xb0||c==0xb4||c==0xa8||c==0x20ac||
        c==0xab||c==0xbb||c==0xac||c==0x2026;
}
static uint32_t approximate(uint32_t c){
 if(c==0xa0)return ' ';
 if(c==0x2018||c==0x2019)return 39;
 if(c==0x201c||c==0x201d)return 34;
 if(c==0x2013||c==0x2014)return '-';
 return c;
}
int glyph_supported(uint32_t c){
 if(approximate(c)<127&&approximate(c)>=32)return 1;
 if(custom(c))return 1;
 for(unsigned i=0;i<sizeof(accents)/sizeof(*accents);i++)if(accents[i].cp==c)return 1;
 return 0;
}
uint8_t glyph_row(uint32_t c,unsigned row){
 if(row>=9)return 0;
 c=approximate(c);
 for(unsigned i=0;i<sizeof(accents)/sizeof(*accents);i++)if(accents[i].cp==c){
  unsigned mark=accents[i].mark;
  unsigned offset=accents[i].base>='a'&&accents[i].base<='z'?2:0;
  if(row>=offset&&row<offset+2){
   static const uint8_t marks[6][2]={{0,0},{2,4},{10,0},{9,22},{8,4},{4,10}};
   return marks[mark][row-offset];
  }
  return row<2?0:font[accents[i].base][row-2];
 }
 if(c>=32&&c<127)return row<2?0:font[c][row-2];
 if(row<2)return 0;
 unsigned r=row-2;
 static const uint8_t inverted_q[7]={4,0,4,8,16,17,14};
 static const uint8_t inverted_e[7]={4,0,4,4,4,4,4};
 static const uint8_t degree[7]={6,9,9,6,0,0,0};
 static const uint8_t euro[7]={7,8,30,8,30,8,7};
 static const uint8_t left[7]={0,5,10,20,10,5,0},right[7]={0,20,10,5,10,20,0};
 static const uint8_t unknown[7]={31,17,21,17,21,17,31};
 if(c==0xbf)return inverted_q[r];
 if(c==0xa1)return inverted_e[r];
 if(c==0xb0)return degree[r];
 if(c==0x20ac)return euro[r];
 if(c==0xab)return left[r];
 if(c==0xbb)return right[r];
 if(c==0xb4)return r==0?2:r==1?4:0;
 if(c==0xa8)return r==0?10:0;
 if(c==0xac)return r==2?31:r==3||r==4?1:0;
 if(c==0x2026)return r==6?21:0;
 return unknown[r];
}
