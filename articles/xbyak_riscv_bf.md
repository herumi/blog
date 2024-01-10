---
title: "RISC-Vの呼び出し規約とXbyak_riscvによるBrainFuckのJITインタプリタ"
emoji: "📖"
type: "tech"
topics: ["RISCV", "JIT", "xbyak", "brainfuck"]
published: true
---
## 初めに
2022年末から[Xbyak](https://github.com/herumi/xbyak)のRISC-V版[Xbyak_riscv](https://github.com/herumi/xbyak_riscv)を作ってました。
現在RV32, RV64のI, fence, M, AとCの一部, Vector Extensionに対応してます。
しばらく放置してたので全部忘れてしまい、勉強し直すためにBrainFuckのJITインタプリタを作りました。

## RISC-Vのレジスタと呼び出し規約
整数レジスタはx0からx31までの32個あります。レジスタサイズはRV64なら64ビット, RV32なら32ビットです。
いくつかのレジスタは用途が決まっています。

レジスタ|ABI上の名前|用途|保存する場所
-|-|-|-
x0|zero|常にゼロ|-
x1|ra|リターンアドレス|呼び出し元(caller)
x2|sp|スタックポインタ|呼び出し先(callee)
x3|gp|グローバルポインタ|-
x4|tp|スレッドポインタ|-
x5, x6, x7|t0, t1, t2|一時的|caller
x8|s0/fp|保存されたレジスタ/フレームポインタ|callee
x9|s1|保存されたレジスタ|callee
x10, x11|a0, a1|関数の引数/戻り値|caller
x12, ..., x17|a2, ..., a7|関数の引数|caller
x18, ..., x27|s2, ..., s11|保存されたレジスタ|callee
x28, ..., x31|t3, ..., t6|一時的|caller

calleeとなっているのが関数の中で変更したら、関数を抜けるときに元に戻すべきレジスタです。
x10からx17にはa0, ..., a7という名前のaliasがついています。それぞれ関数の引数に割り当てられています。
だから
```cpp
int addC(int x, int y) { return x + y; }
```
は

```cpp
addC() {
  addw(a0, a0, a1); // x = x + y
  ret();
}
```

となります（addwは32ビットとしての加算）。

## callとret
x64に慣れている人がcallとretを使うときに一つ注意点があります。
x64の`call(reg)`に相当する命令は`jalr(reg)`です。jalrは`jalr(reg1, reg2, imm)`という形で使い、reg1をjalrの次のアドレスに設定して`reg2 + imm`の指すアドレスにジャンプします。
`jalr(reg)`と書いたときは`jalr(ra, reg, 0)`を意味します。つまりraに戻るべきアドレスを設定してregにジャンプするのですね。
そしてRISC-Vの`ret()`は`jalr(zero, ra, 0)`のaliasです。
zeroは何を書き込んでもゼロのままなので次のアドレスは設定せずにraにジャンプします。raには戻るべきアドレスが入っているので無事戻れます。

だから関数Xの中でraを壊してはいけません。関数Xの中で別の関数Yを呼び出すと、そのときraが変わってしまうので関数Xの中で事前にraをスタックに保存し、関数Yなどを呼び出した後、retする前にraを元に戻す必要があります。

```cpp
funcX() {
  // raをスタックに保存
  ...
  jalr(関数Y);
  ...
  // raをスタックから復元
  ret();
}
```
jalrとretだけを使っているとraは明示的には見えないので注意してください（はまった人）。

## Xbyak_riscvの文法
概ねasm構文をC++のメソッド呼び出しにする形で置き換えます。Xbyakと異なり、凝った演算子オーバーライドはしていません。

asm|Xbyak
-|-
`add x3, x4, x5`|`add(x3, x4, x5);`
`ld x3, 16(x4)`|`ld(x3, x4, 16);`
`sw t0,8(s1)`|`sw(t0, s1, 8);`
`amoswap.w a0,a1,(a2)`|`amoswap_w(a0, a1, a2);`

- `ld`や`sw`などのメモリ関係の命令はアドレス指定を意味する`()`は取り除いてください。
- `amoswap.w`などの途中にピリオド`.`がある命令はアンダースコア`_`に置き換えてください。

```cpp
Label F;
  beq(a0, x0, F);
```
分岐命令もXbyakと同じ使い方です。現状は飛び先が遠いと例外を投げます。
32ビットオフセット内ならOKにする拡張はそのうち対応します。

## Brainfuck
Brainfuckは小さな言語です。
一定量のゼロ初期化されたuint8_t型配列とその先頭ポインタ(stack)が渡されたときに次の命令を処理します。

- `>` // stack++;
- `<` // stack--;
- `+` // (*stack)++;
- `-` // (*stack)--;
- `.` // putchar(*stack);
- `,` // *stack = getchar();
- `[` // while (*stack) {
- `]` // }

## JITコード生成の呼び出し
コード生成された中でputcharやgetcharを直接呼ぶのはややこしいので関数ポインタとしてそれらのポインタとstackを渡すことにします。

```cpp
std::ifstream ifs(argv[1]);
Brainfuck bf(ifs);
bf.ready();
static uint8_t stack[30000];
auto f = bf.getCode<void (*)(const void *, const void *, uint8_t *)>();
f((const void *)putchar, (const void *)getchar, stack);
```

## JITインタプリタの実装

### 関数のプロローグとエピローグ
```cpp
Brainfuck(std::istream &is) : CodeGenerator(100000) {
    const auto &pPutchar = s2;
    const auto &pGetchar = s3;
    const auto &stack = s4;
    const int saveSize = 16*2;
    addi(sp, sp, -saveSize); // sp -= saveSize;
    sd(pPutchar, sp, 8); // sp[1] = pPutchar;
    sd(pGetchar, sp, 16); // sp[2] = pGetchar;
    sd(stack, sp, 24); // sp[3] = stack;
    sd(ra, sp, 32); // sp[4] = ra;

    mv(pPutchar, a0);
    mv(pGetchar, a1);
    mv(stack, a2);
```
関数の最初に引数で渡されたputchar, getchar, stackのポインタをs2, s3, s4に保存します。
これらのレジスタは保存すべきなのでスタックに退避します。前述のようにraも退避します。
spは16の倍数でなければなりません。

関数を抜けるときにこれらの値を元に戻します。
```cpp
    ld(ra, sp, 32);
    ld(stack, sp, 24);
    ld(pGetchar, sp, 16);
    ld(pPutchar, sp, 8);
    addi(sp, sp, saveSize);
    ret();
}
```

### `+`と`-`の処理
`getContinuousChar(is, c)`は`c`と同じ文字が続く限り連続して読み込み、読み込んだ個数を返します。簡単な最適化です。

```cpp
while (is >> c) {
    switch (c) {
    case '+':
    case '-': {
        int count = getContinuousChar(is, c);
        lb(a0, stack);
        if (c == '+') {
            addi(a0, a0, count);
        } else {
            addi(a0, a0, -count);
        }
        sb(a0, stack);
    } break;
```
lbでstackの値にある値を1バイトa0に読み込み、読み込んだ個数count分加減算してstackの場所に書き戻します。
`addi`は即値に符号つき12ビットしかとれないので、それ以上の値はおかしくなってしまうのですが、今は無視してます。また`stack`の領域チェックもしていません。

### `>`と`<`の処理

```cpp
    case '>':
    case '<': {
        int count = getContinuousChar(is, c);
        if (c == '>') {
            addi(stack, stack, count);
        } else {
            addi(stack, stack, -count);
        }
    } break;
```
こちらはstackの値そのものを加減算します。

### `.`と`,`の処理

```cpp
    case '.':
        lb(a0, stack);
        jalr(pPutchar);
        break;
    case ',':
        jalr(pGetchar);
        sb(a0, stack);
        break;
```
`.`は`stack`にある値を表示するので`lb`でa0に1バイト読み込み、`putchar`を呼び出します。
`,`は`getchar`を呼び出して、`sb`でその値を`stack`に書き込みます。

### `[`と`]`の処理
`*stack`の値が0でない限り、`[`と`]`で囲まれた領域をループします。
Cによる疑似コードは
```cpp
B:
  if (*stack == 0) goto F;
  // コードいろいろ
  goto B;
F:
```

ラベル`B`と`F`は任意個、ネストして現れるので`std::stack<Label>`で管理します。

```cpp
std::stack<Label> labelF, labelB;
```

```cpp
    case '[': {
        Label B = L();
        labelB.push(B);
        lb(a0, stack);
        Label F;
        beq(a0, x0, F);
        labelF.push(F);
    } break;
    case ']': {
        Label B = labelB.top();
        labelB.pop();
        j_(B);
        Label F = labelF.top();
        labelF.pop();
        L(F);
    } break;
```

`[`が登場したら現時点の場所でラベル`B`を作り、`labelB`にpushします。
そして、`stack`から1バイト読み込んで等しければラベル`F`にジャンプします。
この時点でラベル`F`の場所は未確定ですが、そのまま`labelF`にpushします。
beqで扱える分岐先のオフセットは13ビットなのでそれより大きいコードは動かなくなりますが、このコードでは無視しています。

`]`が登場したら、`labelB`からラベル`B`をpopして`j_(B)`でジャンプします。
そしてその次の場所が戻るべき`F`なので`labelF`から取り出して`L(F)`で場所を確定させます。

## 動作確認
全体のコードは[bf.cpp](https://github.com/herumi/xbyak_riscv/blob/main/sample/bf.cpp)にあります。

```bash
% sudo apt install g++-13-riscv64-linux-gnu
```
などでRISC-V用コンパイラをインストールして

```bash
% cd xbyak_riscv
% make -C sample EXE=bf.exe
```
でbuildできます。

```bash
% env QEMU_LD_PREFIX=/usr/riscv64-linux-gnu ./bf.exe hello.bf
```
とqemuを使ってbfコードを実行してみてください。

`-d`オプションを付けるとJIT生成されたコードが`bf.dump`に保存されます。

```bash
% env QEMU_LD_PREFIX=/usr/riscv64-linux-gnu ./bf.exe A.bf -d
```

`objdump -D -b binary -m riscv:rv64`を使ってヘッダの無いバイトコードを逆アセンブルできます。
```
% riscv64-linux-gn-objdump -D -b binary -m riscv:rv64 bf.dump
bf.dump:     file format binary

Disassembly of section .data:

0000000000000000 <.data>:
   0:   fe010113                add     sp,sp,-32
   4:   01213423                sd      s2,8(sp)
   8:   01313823                sd      s3,16(sp)
   c:   01413c23                sd      s4,24(sp)
  10:   02113023                sd      ra,32(sp)
  14:   00050913                mv      s2,a0
  18:   00058993                mv      s3,a1
  1c:   00060a13                mv      s4,a2
  20:   000a0503                lb      a0,0(s4)
  24:   04150513                add     a0,a0,65 # 'A'
  28:   00aa0023                sb      a0,0(s4)
  2c:   000a0503                lb      a0,0(s4)
  30:   000900e7                jalr    s2
  34:   02013083                ld      ra,32(sp)
  38:   01813a03                ld      s4,24(sp)
  3c:   01013983                ld      s3,16(sp)
  40:   00813903                ld      s2,8(sp)
  44:   02010113                add     sp,sp,32
  48:   00008067                ret
```
デバッグのお供にしてください。
