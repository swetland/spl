
## sr32 emulator program format

The emulator reads programs that are line-based hex listings that consist of
an eight digit hexidecimal address, a colon, a space, and an eight digit
hexidecimal value. Lines look like this:

`aaaaaaaa: vvvvvvvv`

The line after the address and value is ignored, except if there is a colon,
in which case the characters after the last colon, until the end of line, are
treated as a label for the line, and saved to be used in debug output, etc.
Labels beginning with '$' are ignored.

Lines starting with `#` or `/` are treated as comments.

Lines starting with `#.` are treated as directives.  Currently the supported
directives are `#.abi0` and `#.abi1`, which cause the emulator to select the
calling convention for syscall argument passing. (ABI 0 passes arguments in
stack words sp[0..7]. ABI 1 passes arguments in registers a0..a7. Both use
a0 for return values.)

The sr32 assembler generates hex files that look like this:

```
00100054: fffc1068 // stw     ra, -4(sp)             :start
00100058: fff81228 // stw     fp, -8(sp)
0010005c: 00001200 // mv      fp, sp
00100060: 00281081 // subi    sp, sp, 40
```

