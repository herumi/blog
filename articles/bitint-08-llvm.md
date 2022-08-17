---
title: "多倍長整数の実装8（LLVMを用いたasmコード生成）"
emoji: "🧮"
type: "tech"
topics: ["llvm", "GMP", "clang"]
published: false
---
## 初めに

前回まではx64用asmコードを用いた実装の紹介をしました。今回はより汎用的に、効率よく（そこそこ）高速なコードを生成するためにLLVMを用いてみます。
記事全体の一覧は[多倍長整数の実装1（C/C++）](https://zenn.dev/herumi/articles/bitint-01-cpp)参照。

## 動機

LLVMは仮想機械用の中間コード(LLVM IR)をもち、そのアセンブリ言語（以下ll）の文法は[LLVM Language Reference Manual](https://llvm.org/docs/LangRef.html)に記されています。
llファイルを作ってclangに渡すとターゲットCPUに応じた最適化されたコードが生成されます。従って、llファイルを一つ作るとLLVMが対応する様々なアーキテクチャに対応できます。そこで、今回は今まで作ってきたadd, mulUnitなどの低レベル関数をllで記述してみましょう。

## llの概略

詳細は本家マニュアルを参照していただくことにして、ここでは今回のターゲットに必要最小限の話を紹介します。
LLMV IRは任意個の整数レジスタを扱えます。レジスタのビットサイズも固定ですが、任意サイズです。ただしキャリーフラグは存在しません。

まず64bit整数同士の加算結果を返す関数

```cpp
extern "C" int add(int x, int y)
{
    return x + y;
}
```
をどう表現するかを見てみましょう。

clangで`-S -O2 -emit-llvm`オプションでコンパイルするとllファイルが生成されます。
生成されたファイルにはいろいろな付加情報がついていますが、最小限を抜き出すと次のようになります。

```cpp
define i32 @add(i32 %0, i32 %1) {
  %3 = add i32 %1, %0
  ret i32 %3
}
```

このファイル(add32.ll)と次の(main1.cpp)を一緒にコンパイルするとちゃんと動きます。

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

```
clang++-12 -O2 main1.cpp add32.ll
% ./a.out
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

`add`関数に対応するコードは`-S -o - -O2`で確認できます（余計な部分を除去）。

```
clang++-12 -S -O2 -o - add32.ll
        .text
        .globl  add
add:
        leal    (%rdi,%rsi), %eax
        retq
```

`--target`オプションでLLVMがサポートする他のアーキテクチャのasmコードも生成できます。
たとえばM1などのAArch64なら

```
% clang++-12 -O2 -S -o - --target=aarch64 add32.ll
        .text
        .globl  add
add:
        add     w0, w1, w0
        ret
```

メモリの読み書きはloadとstoreを使います。レジスタのサイズ変更はtranc（小さくする方）やzext（0拡張）などがあります。こちらも詳細はマニュアルを参照ください。

## clangの`_ExtInt`

clangには整数型の拡張として[`_ExtInt`](https://blog.llvm.org/2020/04/the-new-clang-extint-feature-provides.html)というのがあります。
たとえば

```cpp
#include <stdint.h>
typedef unsigned _ExtInt(256) uint256_t; // 256bit符号無し整数
typedef unsigned _ExtInt(256+64) uint320_t; // 32bit符号無し整数

uint64_t mulPre256(uint256_t *pz, const uint256_t *px, uint64_t y)
{
  uint256_t x = *px;
  uint320_t z = uint320_t(x) * uint320_t(y);
  *pz = uint256_t(z);
  return uint64_t(z >> 256);
}
```
という記述できます(clang-12で確認)。
`typedef unsigned _ExtInt(n)`でnビットの整数を定義できます。256bit整数と64bit整数を掛けると320bitになるのでxとyの両方をuint320_tにキャストして掛けます。下位256bitをpzに保存し、上位の64bitをreturnします。

このコードのllファイルは

```cpp
define i64 @mulPre256(i256* %0, i256* readonly %1, i64 %2) {
  %4 = load i256, i256* %1    # %1(=px)から256bit読み込んで%4(=x)に代入
  %5 = zext i256 %4 to i320   # %4を320bitにzero拡張
  %6 = zext i64 %2 to i320    # %2(=y)を320bitにzero拡張
  %7 = mul i320 %5, %6        # 掛けて320bitの値を得る
  %8 = trunc i320 %7 to i256  # それを256bitに切り捨てて
  store i256 %8, i256* %0     # %0(=pz)に書き込み
  %9 = lshr i320 %7, 256      # 残りを256bitシフトして
  %10 = trunc i320 %9 to i64  # 64bitに切り捨てて
  ret i64 %10                 # returnする
}
```

となりました。コメントをつけたので概ね内容は理解できるでしょう。コードを見れば分かるようにllは静的単一代入SSA(Static Single Assignment form)で記述します。
このllファイルを`-S -O2 -mbmi2`をつけてコンパイルすると

```nasm
>clang++-12 -O2 -mbmi2 -S -o - a.cpp -masm=intel
        .text
        .globl  mulPre256
mulPre256:
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

となりました。[adcとmulxのintrinsicによる実装](https://zenn.dev/herumi/articles/bitint-05-mulx#adc%E3%81%A8mulx%E3%81%AEintrinsic%E3%81%AB%E3%82%88%E3%82%8B%E5%AE%9F%E8%A3%85)で紹介した生成コードにかなり近いです（最後のadc rax, 0は不要ですが）。たいしたものです。

全部これで書けばええやん、と思ってしまいますが、ここで残念なお知らせです。片方が64bitなら乗算はできるのですがそれより大きいビットサイズを指定するとランタイムエラーになります。
また、`_ExtInt`はCの新しい規格として実装されていたのですが、`_BitInt`という名前に変わってしまいました(cf. [Introduce _BitInt, deprecate _ExtInt](https://reviews.llvm.org/D108643))。更にclang-12より新しいバージョンのclangでは何故か128ビットまでしか指定できません。

新しいclangが出すエラー。

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
