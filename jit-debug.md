# JITのデバッグ方法
XbyakはJITコードなのでデバッグが面倒になりがちです。
いくつかデバッグのヒントを書きます。

## 逆アセンブル

コード生成したあと`.getCode()`から`getSize()`byteが生成されたコードなのでそれをファイルに保存します。

生成したファイルはヘッダがないので先頭から逆アセンブルするには

```
// x64
objdump -M x86-64 -D -bin binary -m i386 ファイル名

// aarch64
objdump -m aarch64 -D -b binary ファイル名
```

## ブレークポイント

JITコードにブレークポイントを置くのはやや面倒です。
対象コードを呼び出す直前にブレークポイントを置くことをよくしますが、JITコード内に生成する方法もあります。

x64では`int3();`を呼び出します。

JITコード
```
int3();
mov(eax, 5);
mov(eax, 3);
ret();
```

このようにしてgdbで実行すると`int3();`の直後でストップします。

```
(gdb) r
./a.out
Program received signal SIGTRAP, Trace/breakpoint trap.
0x00007ffff7ffb001 in ?? ()
(gdb) disas
Dump of assembler code from 0x7ffff7ffb001 to 0x7ffff7ffb081:
=> 0x00007ffff7ffb001:  mov    eax,0x5
   0x00007ffff7ffb006:  mov    eax,0x3
   0x00007ffff7ffb00b:  ret
```

ちなみに私が適当につけた`dis`は現在のインストラクションポインタから10個分の命令を逆アセンブルする命令aliasです。

```
// ~/.gdbinit
define dis
  x/11i $pc
end
```

`.gdbcom`に`display/i $pc`としておくと常に現在の命令が表示されるので便利です。

aarch64では`brk(0);`とします。

JITコード

```
brk(0);
stp(x29, x30, pre_ptr(sp, -32));
stp(x, func, ptr(sp, 16));

mov(x, x0);
```

gdbで実行

```
Program received signal SIGTRAP, Trace/breakpoint trap.
0x0000ffffbe7e0000 in ?? ()
1: x/i $pc
=> 0xffffbe7e0000:      brk     #0x0
(gdb) si
0x0000ffffbe7e0000 in ?? ()
1: x/i $pc
=> 0xffffbe7e0000:      brk     #0x0
```

x64の`int3`と異なり、ここでcontinueしても同じ`brk`で止まります。
そこで`set $pc+=4`としてプログラムカウンタを進めることで次の命令から再開できます。

gdbで実行

```
(gdb) set $pc+=4
(gdb) si
0x0000ffffbe7e0008 in ?? ()
1: x/i $pc
=> 0xffffbe7e0008:      stp     x19, x20, [sp, #16]
(gdb)
0x0000ffffbe7e000c in ?? ()
1: x/i $pc
=> 0xffffbe7e000c:      mov     x19, x0
(gdb)
0x0000ffffbe7e0010 in ?? ()
1: x/i $pc
=> 0xffffbe7e0010:      mov     x20, x1
```
