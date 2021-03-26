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
(gdb) dis
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

また`.gdbinit`に

```
define reg
  printf "rax:%016lx rbx:%016lx rcx:%016lx rdx:%016lx\n", $rax, $rbx, $rcx, $rdx
  printf "rdi:%016lx rsi:%016lx rbp:%016lx rsp:%016lx\n", $rdi, $rsi, $rbp, $rsp
  printf " r8:%016lx  r9:%016lx r10:%016lx r11:%016lx\n", $r8, $r9, $r10, $r11
  printf "r12:%016lx r13:%016lx r14:%016lx r15:%016lx\n", $r12, $r13, $r14, $r15
```
や
```
define reg
  printf "x0 :%016lx x1 :%016lx x2 :%016lx x3 :%016lx\n", $x0, $x1, $x2, $x3
  printf "x4 :%016lx x5 :%016lx x6 :%016lx x7 :%016lx\n", $x4, $x5, $x6, $x7
  printf "x8 :%016lx x9 :%016lx x10:%016lx x11:%016lx\n", $x8, $x9, $x10, $x11
  printf "x12:%016lx x13:%016lx x14:%016lx x15:%016lx\n", $x12, $x13, $x14, $x15
  ...
end
```
としておくと`reg`でまとめてレジスタを表示できるので、これも便利です。

aarch64では`int3();`ではなく`brk(0);`とします。

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

## Valgrindでデバッグ
先日valgrind上で実行すると落ちるバグを調べることになりました。
次のようにするとvalgrindとgdbを組み合わせられます。

まずvalgrindで該当プログラムを動かします。
```
valgrind --vgdb=yes --vgdb-error=0 対象プログラム
```

そして別のターミナルでgdbを起動してvgdbに接続します。

```
gdb 対象プログラム
target remote | vgdb
```

実行プログラムが落ちてたまにvalgrindが返って来なくなることがあります。
`ctrl-c`を押しても駄目なときはpsコマンドで該当プロセスを探してその実行プロセスだけkillします。

詳細は[Debugging your program using Valgrind gdbserver and GDB](https://www.valgrind.org/docs/manual/manual-core-adv.html)を参照してください。

## Intel SDEでデバッグ
SDEはIntelが提供するエミュレータです。
Haswell(-hsw)やSkylake(-skl)など専用命令を正しくハンドルできるか調べるときに便利です。
SDEもgdbと一緒にデバッグできます。

```
sde -debug -- 対象プログラム
```

で起動すると

```
Application stopped until continued from debugger.
Start GDB, then issue this command at the (gdb) prompt:
  target remote :50407
```

と表示されます(数値は毎回変わります)。
そこで別のターミナルでgdbを実行し、

```
gdb
target remote:50407
```
と上記番号を指定します。

詳細は[Debugging applications with Intel SDE](https://software.intel.com/content/www/us/en/develop/articles/debugging-applications-with-intel-sde.html)を参照してください。
