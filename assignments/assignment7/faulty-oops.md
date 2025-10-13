# Faulty OOPS File

## Oops command line output
```text
Unable to handle kernel NULL pointer dereference at virtual address 0000000000000000
Mem abort info:
  ESR = 0x0000000096000045
  EC = 0x25: DABT (current EL), IL = 32 bits
  SET = 0, FnV = 0
  EA = 0, S1PTW = 0
  FSC = 0x05: level 1 translation fault
Data abort info:
  ISV = 0, ISS = 0x00000045
  CM = 0, WnR = 1
user pgtable: 4k pages, 39-bit VAs, pgdp=0000000041b65000
[0000000000000000] pgd=0000000000000000, p4d=0000000000000000, pud=0000000000000000
Internal error: Oops: 0000000096000045 [#1] SMP
Modules linked in: hello(O) faulty(O) scull(O)
CPU: 0 PID: 155 Comm: sh Tainted: G           O       6.1.44 #1
Hardware name: linux,dummy-virt (DT)
pstate: 80000005 (Nzcv daif -PAN -UAO -TCO -DIT -SSBS BTYPE=--)
pc : faulty_write+0x10/0x20 [faulty]
lr : vfs_write+0xc8/0x390
sp : ffffffc008debd20
x29: ffffffc008debd80 x28: ffffff8001b98d40 x27: 0000000000000000
x26: 0000000000000000 x25: 0000000000000000 x24: 0000000000000000
x23: 000000000000000c x22: 000000000000000c x21: ffffffc008debdc0
x20: 0000005580a4b9e0 x19: ffffff8001bf5100 x18: 0000000000000000
x17: 0000000000000000 x16: 0000000000000000 x15: 0000000000000000
x14: 0000000000000000 x13: 0000000000000000 x12: 0000000000000000
x11: 0000000000000000 x10: 0000000000000000 x9 : 0000000000000000
x8 : 0000000000000000 x7 : 0000000000000000 x6 : 0000000000000000
x5 : 0000000000000001 x4 : ffffffc000787000 x3 : ffffffc008debdc0
x2 : 000000000000000c x1 : 0000000000000000 x0 : 0000000000000000
Call trace:
 faulty_write+0x10/0x20 [faulty]
 ksys_write+0x74/0x110
 __arm64_sys_write+0x1c/0x30
 invoke_syscall+0x54/0x130
 el0_svc_common.constprop.0+0x44/0xf0
 do_el0_svc+0x2c/0xc0
 el0_svc+0x2c/0x90
 el0t_64_sync_handler+0xf4/0x120
 el0t_64_sync+0x18c/0x190
Code: d2800001 d2800000 d503233f d50323bf (b900003f) 
---[ end trace 0000000000000000 ]---
```

## Analysis
We registered faulty write function as function to be invoked whenever write is to be occured in /dev/faulty file, so when echo command tries to write, it calls faulty write function which deliberately as defined, do the NULL pointer dereference by trying to dereference at address 0 which is an invalid address.
As seen in call trace too, faulty write is the culprit function. Using PC (Progam counter) value, we can extract the exact instruction at offset 0x10 inside the function.

With Objdump command, ```./output/host/bin/aarch64-buildroot-linux-gnu-objdump -d ./output/target/lib/modul.44/extra/faulty.ko```
```text
./output/target/lib/modules/6.1.44/extra/faulty.ko:     file format elf64-littleaarch64


Disassembly of section .text:

0000000000000000 <faulty_write>:
   0:   d2800001        mov     x1, #0x0                        // #0
   4:   d2800000        mov     x0, #0x0                        // #0
   8:   d503233f        paciasp
   c:   d50323bf        autiasp
  10:   b900003f        str     wzr, [x1]                      //Crash line
  14:   d65f03c0        ret
  18:   d503201f        nop
  1c:   d503201f        nop

0000000000000020 <faulty_init>:
  20:   d503233f        paciasp
  24:   a9be7bfd        stp     x29, x30, [sp, #-32]!
  28:   90000004        adrp    x4, 0 <faulty_write>
  2c:   910003fd        mov     x29, sp
  30:   f9000bf3        str     x19, [sp, #16]
  34:   90000013        adrp    x19, 0 <faulty_write>
  38:   b9400260        ldr     w0, [x19]
```

We can see offset 0x10 is inside faulty write function and line is str and before return, and it tries to access zero address in x1, so we can likely reach line ```*(int *)0 = 0;```. It would be better if we build with debug symbols and use addr2line to point to exact line number.
