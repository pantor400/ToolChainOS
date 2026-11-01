set pagination off
set architecture i386:x86-64
file build/kernel.elf
target remote 127.0.0.1:1234
break kmain
continue
# Al detenerse: next / step / info registers / x/10i $pc / continue
