/* Targeted PS/2 burst: long command, Enter, Backspace and Spanish UTF-8. */
#include "keyboard.h"
#include "utf8.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
void mouse_poll(void) {}
static void key(unsigned scan) {keyboard_scancode(scan);keyboard_scancode(scan|0x80);}
int main(void) {
    assert(keyboard_set_layout(KEYBOARD_ES));
    key(18);key(46);key(24);key(57); /* eco */
    for(int i=0;i<80;i++)key(30);
    key(14);key(14);
    key(26);key(18); /* é */
    key(39);        /* ñ */
    key(28);        /* Enter, all queued before any read */
    char line[128]={0};unsigned used=0;int byte,enter=0;
    while((byte=keyboard_next_byte())>=0) {
        if(byte=='\n') {enter=1;break;}
        if(byte=='\b') {used=(unsigned)utf8_previous(line,used);line[used]=0;}
        else {assert(used+1<sizeof(line));line[used++]=(char)byte;line[used]=0;}
    }
    assert(enter && keyboard_next_byte()==-1 && utf8_valid(line,used));
    char expected[128]="eco ";memset(expected+4,'a',78);
    strcpy(expected+82,"éñ");
    assert(strcmp(line,expected)==0);
    puts("PASSED: queued PS/2 long command (80 letters), Enter retained, Backspace x2, UTF8 é/ñ intact");
    return 0;
}
