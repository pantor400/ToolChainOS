#include "utf8.h"
#include "keyboard.h"
#include "glyph.h"
#include "fs.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
void console_write(const char *s){(void)s;}
void console_putc(char c){(void)c;}
void console_dec(uint64_t n){(void)n;}
static void expect(const char *want){
 char result[40];size_t n=0;int c;
 while((c=keyboard_next_byte())>=0){assert(n+1<sizeof(result));result[n++]=(char)c;}
 result[n]=0;assert(strcmp(result,want)==0);
}
static void key(unsigned scan){keyboard_scancode(scan);keyboard_scancode(scan|0x80);}
int main(void){
 keyboard_set_layout(KEYBOARD_US);
 for(int i=0;i<100;i++)key(30);
 for(int i=0;i<60;i++)assert(keyboard_next_byte()=='a');
 for(int i=0;i<80;i++)key(48);
 for(int i=0;i<40;i++)assert(keyboard_next_byte()=='a');
 for(int i=0;i<80;i++)assert(keyboard_next_byte()=='b');
 assert(keyboard_next_byte()==-1);
 keyboard_set_layout(KEYBOARD_ES);
 for(int i=0;i<127;i++)key(30);
 key(39); /* Two-byte ñ cannot fit: drop the whole scalar. */
 for(int i=0;i<127;i++)assert(keyboard_next_byte()=='a');
 assert(keyboard_next_byte()==-1);
 key(39);expect("ñ");

 unsigned scalars=0;
 for(uint32_t cp=0;cp<=0x10ffff;cp++){
  if(cp>=0xd800&&cp<=0xdfff)continue;
  char bytes[4];unsigned n=utf8_encode(cp,bytes);uint32_t decoded;size_t used;
  assert(utf8_decode((const uint8_t*)bytes,n,&decoded,&used)==1);
  assert(cp==decoded&&used==n);scalars++;
  utf8_state_t state={0};
  for(unsigned i=0;i<n;i++)assert(utf8_feed(&state,(uint8_t)bytes[i],&decoded)==(i+1==n?1:0));
 }
 const char *bad[]={"\xc0\x80","\xc1\xbf","\xed\xa0\x80","\xf4\x90\x80\x80","\xf5\x80\x80\x80","\x80","\xe2\x28\xa1"};
 for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++)assert(!utf8_valid(bad[i],strlen(bad[i])));
 assert(!utf8_valid("\xc3",1));assert(!utf8_valid("\xf0\x9f",2));
 assert(utf8_previous("año",4)==3);assert(utf8_previous("ñ",2)==0);
 assert(utf8_previous("á🙂",6)==2);
 keyboard_set_layout(KEYBOARD_ES);key(39);expect("ñ");
 keyboard_scancode(42);key(39);keyboard_scancode(170);expect("Ñ");
 unsigned vowels[]={30,18,23,24,22};
 const char *lower[]={"á","é","í","ó","ú"},*upper[]={"Á","É","Í","Ó","Ú"};
 for(unsigned i=0;i<5;i++){
  key(26);key(vowels[i]);expect(lower[i]);
  key(26);keyboard_scancode(42);key(vowels[i]);keyboard_scancode(170);expect(upper[i]);
 }
 keyboard_scancode(42);key(26);keyboard_scancode(170);key(22);expect("ü");
 keyboard_scancode(42);key(26);key(22);keyboard_scancode(170);expect("Ü");
 key(13);expect("¿");keyboard_scancode(42);key(13);keyboard_scancode(170);expect("¡");
 keyboard_scancode(42);key(12);key(2);keyboard_scancode(170);expect("?!");
 keyboard_scancode(0xe0);keyboard_scancode(56);key(16);keyboard_scancode(0xe0);keyboard_scancode(184);expect("@");
 key(26);key(57);expect("´");
 key(26);key(48);expect("´b");
 key(26);key(14);expect("");
 keyboard_scancode(58);keyboard_scancode(58);keyboard_scancode(186);key(30);expect("A");
 keyboard_scancode(42);key(30);keyboard_scancode(170);expect("a");
 key(39);expect("Ñ");
 keyboard_set_layout(KEYBOARD_US);key(39);expect(";");
 keyboard_scancode(42);key(39);key(3);keyboard_scancode(170);expect(":@");
 key(26);expect("[");assert(!keyboard_set_layout(3));
 uint32_t spanish[]={0xe1,0xe9,0xed,0xf3,0xfa,0xc1,0xc9,0xcd,0xd3,0xda,0xf1,0xd1,0xfc,0xdc,0xbf,0xa1};
 for(unsigned i=0;i<sizeof(spanish)/sizeof(*spanish);i++){
  assert(glyph_supported(spanish[i]));unsigned dots=0;
  for(unsigned row=0;row<9;row++)dots|=glyph_row(spanish[i],row);
  assert(dots);
 }
 assert(!glyph_supported(0x1f642));
 fs_init();char err[96];assert(fs_write_path("año","Perú y programación: ¿qué? ¡Sí! Ñ ü",err,sizeof(err)));
 assert(strcmp(fs_read_file("año",NULL),"Perú y programación: ¿qué? ¡Sí! Ñ ü")==0);
 printf("PASSED: %u escalares UTF-8 (roundtrip/fragmentacion), invalidos, ES/US/deadkeys/modificadores, glifos y FS UTF-8\n",scalars);
 return 0;
}
