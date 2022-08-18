---
title: "多倍長整数の実装8（LLVMを用いたasmコード生成）"
emoji: "🧮"
type: "tech"
topics: ["llvm", "clang", "x64", "aarch64"]
published: true
---
## 初めに

前回まではx64用asmコードを用いた実装を紹介しました。高速な実装のためには[CPUの特性と命令を駆使しなければなりません](https://zenn.dev/herumi/articles/bitint-07-gen-asm#mulunitadd%E3%81%AE%E5%A0%B4%E5%90%88)。今回はLLVMを用いてより汎用的で（そこそこ）高速なコードを効率よく生成することを目指します。
記事全体の一覧は[多倍長整数の実装1（C/C++）](https://zenn.dev/herumi/articles/bitint-01-cpp)参照。

## 方針

LLVMは仮想機械用の中間コード（LLVM IR）をもち、そのアセンブリ言語（以下ll）の文法は[LLVM Language Reference Manual](https://llvm.org/docs/LangRef.html)に記されています。
llファイルを作ってclangに渡すとターゲットCPUに応じた最適化されたコードが生成されます。従って、llファイルを一つ作るとLLVMが対応する様々なアーキテクチャに対応できます（実際にはそううまくはいかないことが多いですが）。そこで、今回は今まで作ってきたadd, mulUnitなどの低レベル関数をllで記述してみましょう。

## llの概略

詳細は本家マニュアルを参照していただくことにして、ここでは今回のターゲットに必要最小限の話を紹介します。
LLMV IRは任意個の整数レジスタを扱えます。レジスタのビットサイズも固定ですが、任意サイズです。ただしキャリーフラグは存在しません。

まず32bit整数同士の加算結果を返す関数

```cpp
extern "C" int add(int x, int y)
{
    return x + y;
}
```
をどう表現するかを見てみましょう。

clangで`-S -O2 -emit-llvm`オプションでコンパイルするとllファイルが生成されます。
生成されたファイルにはいろいろな付加情報がついていますが、最小限を抜き出すと次のようになります。

```llvm
define i32 @add(i32 %0, i32 %1) { ; 32bitレジスタ%0と%1を引数にもちi32を返す関数
  %3 = add i32 %1, %0             ; 32bit加算をして%3にセット
  ret i32 %3                      ; %3をreturnする
}
```

%で始まる変数がレジスタです。ここでは数字しか出ていませんが`%abc`のようなものでもOKです。
このファイル(add32.ll)と次の(main1.cpp)を一緒にコンパイルするとちゃんと動きます。clangはllを普通にコンパイルできるのが便利です。gccも対応してくれるとありがたいのですが。

```cpp
#include <stdio.h>
#include <stdint.h>

extern "C" int add(int x, int y);

int main()
{
  for (int i = 0; i < 10; i++) {
    printf("%d + %d = %d\n", i, i + 3, add(i, i + 3));
  }
}
```

```shell-session
$ clang++-12 -O2 main1.cpp add32.ll
$ ./a.out
0 + 3 = 3
1 + 4 = 5
2 + 5 = 7
3 + 6 = 9
4 + 7 = 11
5 + 8 = 13
6 + 9 = 15
7 + 10 = 17
8 + 11 = 19
9 + 12 = 21
```

`add`関数に対応するコードは`-S -o - -O2`で確認できます（余計な部分を除去しています）。

```shell-session
$ clang++-12 -S -O2 -o - add32.ll
        .text
        .globl  add
add:
        leal    (%rdi,%rsi), %eax
        retq
```

`--target`オプションでLLVMがサポートする他のアーキテクチャのasmコードも生成できます。
たとえばM1などのAArch64なら

```console
$ clang++-12 -O2 -S -o - --target=aarch64 add32.ll
        .text
        .globl  add
add:
        add     w0, w1, w0
        ret
```

clangが対応しているアーキテクチャは`-print-target`で確認できます。

```sehll-sessoin
$ clang++-12 -print-targets
  Registered Targets:
    aarch64    - AArch64 (little endian)
    aarch64_32 - AArch64 (little endian ILP32)
    aarch64_be - AArch64 (big endian)
    amdgcn     - AMD GCN GPUs
    arm        - ARM
    arm64      - ARM64 (little endian)
    arm64_32   - ARM64 (little endian ILP32)
    armeb      - ARM (big endian)
    avr        - Atmel AVR Microcontroller
    ...
```
RISC-Vにも対応してます。便利ですね。

```shell-sessoin
$ clang++-12 -S -o - -O2 --target=riscv64 a.ll
        .text
        .globl  add
add:
        addw    a0, a0, a1
        ret
```

メモリの読み書きはloadとstoreを使います。レジスタのサイズ変更はtrunc（小さくする方）やzext（0拡張）などがあります。こちらも詳細はマニュアルを参照ください。

## clangの`_ExtInt`の紹介（余談）

clangには整数型の拡張として[`_ExtInt`](https://blog.llvm.org/2020/04/the-new-clang-extint-feature-provides.html)というのがあります。
たとえば256bit整数と64bit整数を掛けて320bit整数を得る関数はC++で

```cpp
#include <stdint.h>
typedef unsigned _ExtInt(256) uint256_t; // 256bit符号無し整数
typedef unsigned _ExtInt(256+64) uint320_t; // 320bit符号無し整数

uint64_t mulUnit256(uint256_t *pz, const uint256_t *px, uint64_t y)
{
  uint256_t x = *px;
  uint320_t z = uint320_t(x) * uint320_t(y);
  *pz = uint256_t(z);
  return uint64_t(z >> 256);
}
```
と記述できます（clang-12で確認）。
`typedef unsigned _ExtInt(n)`でnビットの整数を定義します。演算は同じサイズの型同士でしかできないのでxとyの両方をuint320_tにキャストして掛けます。下位256bitをpzに保存し、上位の64bitをreturnします。

このコードのllファイルは

```llvm
define i64 @mulUnit256(i256* %0, i256* readonly %1, i64 %2) {
  %4 = load i256, i256* %1    ; %1(=px)から256bit読み込んで%4(=x)に代入
  %5 = zext i256 %4 to i320   ; %4を320bitにゼロ拡張
  %6 = zext i64 %2 to i320    ; %2(=y)を320bitにゼロ拡張
  %7 = mul i320 %5, %6        ; 掛けて320bitの値を得る
  %8 = trunc i320 %7 to i256  ; それを256bitに切り捨てて
  store i256 %8, i256* %0     ; %0(=pz)に書き込み
  %9 = lshr i320 %7, 256      ; 残りを256bitシフトして
  %10 = trunc i320 %9 to i64  ; 64bitに切り捨てて
  ret i64 %10                 ; returnする
}
```

となりました。コメントをつけたので概ね内容は理解できるでしょう。コードを見れば分かるようにllは静的単一代入SSA（Static Single Assignment form）で記述します。
このllファイルを`-S -O2 -mbmi2`をつけてコンパイルすると

```nasm
>clang++-12 -O2 -mbmi2 -S -o - a.cpp -masm=intel
        .text
        .globl  mulUnit256
mulUnit256:
        mulx    r9, r8, qword ptr [rsi + 16]
        mulx    rax, rcx, qword ptr [rsi + 24]
        add     rcx, r9
        adc     rax, 0
        mulx    r9, r10, qword ptr [rsi]
        mulx    rsi, rdx, qword ptr [rsi + 8]
        add     rdx, r9
        adc     rsi, r8
        adc     rcx, 0
        adc     rax, 0
        mov     qword ptr [rdi], r10
        mov     qword ptr [rdi + 8], rdx
        mov     qword ptr [rdi + 16], rsi
        mov     qword ptr [rdi + 24], rcx
        ret
```

となりました。元のllファイルでは320bit同士の乗算であっても元がそれぞれ256bit, 64bitをゼロ拡張したものであると判断して必要最小限の乗算しかしていないことに注意してください。256bitの切り捨てやシフトもレジスタ上では不要なのでそのようなコードもありません。
[adcとmulxのintrinsicによる実装](https://zenn.dev/herumi/articles/bitint-05-mulx#adc%E3%81%A8mulx%E3%81%AEintrinsic%E3%81%AB%E3%82%88%E3%82%8B%E5%AE%9F%E8%A3%85)で紹介した生成コードにかなり近いです。addが一つ余計ですが、たいしたものです。

ここまで出来るなら、全部`_ExtInt`で書けばええやん、と思ってしまいます。しかしここで残念なお知らせです。片方が64bitなら乗算はできるのですがそれより大きいビットサイズを指定するとランタイムエラーになります。自分でmulUnitを組み合わせて教科書ライクな乗算コードを書かねばなりません。
また、`_ExtInt`はCの新しい規格として実験的に実装されていたのですが、`_BitInt`という名前に変わってしまいました（cf. [Introduce _BitInt, deprecate _ExtInt](https://reviews.llvm.org/D108643)）。更にclang-13より新しいバージョンの[clangでは何故か128ビットまでしか指定できません](https://wandbox.org/permlink/6gUZkGBgx9UDx9V8)。

clang++-16が出すエラー。

```bash
add-extint.cpp(2,18): warning: '_ExtInt' is deprecated; use '_BitInt' instead [-Wdeprecated-type]
typedef unsigned _ExtInt(256) uint256_t;
                 ^~~~~~~
                 _BitInt
add-extint.cpp(2,1): error: unsigned _BitInt of bit sizes greater than 128 not supported
```

- `_ExtInt`は非推奨になったので代わりに`_BitInt`を使いなさい。
- `_BitInt`は128より大きい値は対応していない。

なんじゃそりゃ。おそらく、そのうち解決されるのでしょうが今のところ使えません。
というわけで、現状ではclangに頼らずにllファイルを直接書くのが望ましいです。

## llファイル生成補助ツール

LLVM IRの命令は全ての演算にレジスタのビット情報を書かなければなりません。最初にi256やi32と宣言してるのだからその情報を使ってくれればよいのにやってくれないのです。またSSAなので一行ごとに新しい変数名を定義する必要があり、手書きの場合はとても面倒です。

そこで、llファイルを作るための簡単なツール[mcl/src/llvm_gen.hpp](https://github.com/herumi/mcl/blob/master/src/llvm_gen.hpp)を開発しました。前回のXbyakライクなPython DSL(`gen_x64_asm.py`)と似たものですが、開発時期は`llvm_gen.hpp`のほうがずっと前です。将来JIT生成したいなと思ってC++で作ったのですが未だに実装していません（必要性が低かった）。素直にPythonで書けばよかったですね。
それはともかく、`llvm_gen.hpp`を使ったコードが[mcl/src/gen_bint.cpp](https://github.com/herumi/mcl/blob/master/src/gen_bint.cpp)です。一部を紹介しましょう。

256bit整数加算`Unit addPre(Unit *pz, const Unit *px, const Unit *py)`は次のように書けます（細部省略）。

```cpp
void gen_add()
{
    const int bit = 256;
    const int unit = 64;
    const int N = bit / unit;
    Operand r(Int, unit);                       // 戻り値
    Operand pz(IntPtr, unit);                   // pz
    Operand px(IntPtr, unit);                   // px
    Operand py(IntPtr, unit);                   // py
    Function f("mclb_add", r, pz, px, py);      // 関数のプロトタイプ宣言を表すインスタンス
    beginFunc(f);                               // 関数始まり
    Operand x = zext(loadN(px, N), bit + unit); // pxからN個分メモリから読み込みbit + unitにゼロ拡張
    Operand y = zext(loadN(py, N), bit + unit); // pyからN個分メモリから読み込みbit + unitにゼロ拡張
    Operand z;
    z = add(x, y);                              // 加算
    storeN(trunc(z, bit), pz);                  // 下位256ビットをpzにstore
    r = trunc(lshr(z, bit), unit);              // 256bit右シフトして64bitにtruncate
    ret(r);                                     // 値rを返す
    endFunc();                                  // 関数終わり
}
```

`Operand`は整数レジスタやポインタなどを表すクラスです。用途（`Int`や`IntPtr`）とサイズ（自然数）を指定してインスタンスを作ります。
LLVM IRの主に整数演算命令に対応する関数が用意されていて、それを呼び出すと対応するサイズつきのllコードを生成します。複数回代入可能で、自動的に名前の番号が増えます。

loadNやstoreNはN個分シフトしながら読み書きするサブ関数です。このコードをコンパイルして実行すると

```llvm
define i64 @mclb_add4(i64* noalias  %r2, i64* noalias  %r3, i64* noalias  %r4)
{
%r6 = bitcast i64* %r3 to i256*
%r7 = load i256, i256* %r6
%r8 = zext i256 %r7 to i320
%r10 = bitcast i64* %r4 to i256*
%r11 = load i256, i256* %r10
%r12 = zext i256 %r11 to i320
%r13 = add i320 %r8, %r12
%r14 = trunc i320 %r13 to i256
%r16 = getelementptr i64, i64* %r2, i32 0
%r17 = trunc i256 %r14 to i64
store i64 %r17, i64* %r16
%r18 = lshr i256 %r14, 64
%r20 = getelementptr i64, i64* %r2, i32 1
%r21 = trunc i256 %r18 to i64
store i64 %r21, i64* %r20
%r22 = lshr i256 %r18, 64
%r24 = getelementptr i64, i64* %r2, i32 2
%r25 = trunc i256 %r22 to i64
store i64 %r25, i64* %r24
%r26 = lshr i256 %r22, 64
%r28 = getelementptr i64, i64* %r2, i32 3
%r29 = trunc i256 %r26 to i64
store i64 %r29, i64* %r28
%r30 = lshr i320 %r13, 256
%r31 = trunc i320 %r30 to i64
ret i64 %r31
}
```

このようなllファイルが作成されます（cf. [mcl/src/bint64.ll](https://github.com/herumi/mcl/blob/master/src/bint64.ll#L238-L266)）。このllファイルをx64用のasmに変換すると

```shell-session
$ clang++-12 -S -o - -O2 -masm=intel a.ll
        .text
        .globl  mclb_add4
mclb_add4:
    mov     r8, qword ptr [rdx]
    add     r8, qword ptr [rsi]
    mov     r9, qword ptr [rdx + 8]
    adc     r9, qword ptr [rsi + 8]
    mov     rcx, qword ptr [rdx + 16]
    adc     rcx, qword ptr [rsi + 16]
    mov     rdx, qword ptr [rdx + 24]
    adc     rdx, qword ptr [rsi + 24]
    setb    al
    movzx   eax, al
    mov     qword ptr [rdi], r8
    mov     qword ptr [rdi + 8], r9
    mov     qword ptr [rdi + 16], rcx
    mov     qword ptr [rdi + 24], rdx
    ret
```

最適化により冗長なtruncやzest, lshrなどは全て消えて望みのコードが生成されました。もちろんAArch64用のコードも生成できます。

```shell-session
$ clang++-12 -S -o - -O2 --target=aarch64 a.ll
        .globl  mclb_add4
mclb_add4:
    ldp     x9, x8, [x1]
    ldp     x11, x10, [x2]
    ldp     x13, x12, [x1, #16]
    ldp     x14, x15, [x2, #16]
    adds    x9, x11, x9
    adcs    x8, x10, x8
    adcs    x10, x14, x13
    stp     x9, x8, [x0]
    adcs    x9, x15, x12
    adcs    x8, xzr, xzr
    stp     x10, x9, [x0, #16]
    mov     x0, x8
    ret
```

llファイルを手書きするのはなかなか大変ですが、今回のケースでは`llvm_gen.hpp`のような補助ツールを使うと読みやすく、それなりによいコードを得られることが分かりました。
[mcl](https://github.com/herumi/)のAArch64向けMontgomery乗算などはこの方法を用いて実装しています。
