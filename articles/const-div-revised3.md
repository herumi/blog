---
title: "定数除算最適化再考3 コンパイラを越えろ"
emoji: "📖"
type: "tech"
topics: ["整数", "除算", "逆数乗算"]
published: true
---
## 初めに
整数の定数除算最適化の続きです。
今回は[前回](https://zenn.dev/herumi/articles/const-div-revised2)紹介した最適化コンパイラの生成するコードを改善します。
[Kernel/VM探検隊@東京 No18](https://kernelvm.connpass.com/event/355100/)で発表した[定数整数除算最適化再考 ](https://speakerdeck.com/herumi/constant-integer-division-faster-than-compiler-generated-code)の詳細な話です。

$d > 2^{30}$ のときは1回の比較、$d$ が2の巾のときは右シフト、$c$ が32ビットに入るとき(e.g. $d=3$)は乗算+右シフトでこれ以上は改善できないので残りの $c$ が33ビットのとき(e.g. $d=7$)を考えます。
数えたところ $2^{31}$ 個中23%が今回の対象になります。

## コンパイラの生成コード
定数 $d=7$ で割るコードはアルゴリズム的には $x \texttt{//} d = (x \times \texttt{0x124924925}) \texttt{>>} 35$ でした。
これに相当するコンパイラが生成したアセンブリ言語に対応するCのコードを再掲します。

```cpp
uint32_t div7org(uint32_t x)
{
    uint64_t v = x * uint64_t(0x24924925);
    v >>= 32;
    uint32_t t = uint32_t(x) - uint32_t(v);
    t >>= 1;
    t += uint32_t(v);
    t >>= 2;
    return t;
}
```

## 128ビット乗算を使う
x64には64ビット×64ビット=128ビット乗算命令があります。
それを素直に使うとどうなるでしょう。

```cpp
const uint64_t c_ = 0x124924925;
const uint32_t a_ = 35;
uint32_t div7opti1(uint32_t x)
{
    uint128_t v = (x * uint128_t(c_)) >> a_;
    return uint32_t(v);
}
```

x64での生成コードは

```nasm
// mul64+shrd
mov rdx, 0x124924925
mul rdx
shrd rax, rdx, 0x23
```
です。乗算の128ビットの結果はrax, rdxを二つ合わせた`[rdx:rax]`に入るので、それを`shrd`を使って35ビット右シフトします。

## ベンチマーク方法

コンパイラのコードとこのコードとの速度比較をしましょう。
上記コードを単純に10億回($N=10^9$)ループさせてかかった時間を測定しようとするとコンパイラの最適化が掛かって分かりづらくなります。
最適化を阻害するために、関数ポインタとして呼び出すと対象コードが小さいのでその影響が大きいです。
そこでコンパイラの影響を受けないように今回は[Xbyak](https://github.com/herumi/xbyak)を使ってループ込みでJIT生成することにしました。

```cpp
for (int i = 0; i < N; i++) {
    for (int j = 0; j < lpN; j++) {
        func();
    }
}
```
funの部分をインライン展開、内側の`j`に関するlpN回のループも展開、外側の`i`に関するループは展開しないという形でコード生成します。
詳細は[constdiv.hpp](https://github.com/herumi/constdiv/blob/main/include/constdiv.hpp)を参照してください。

## x64向け最適化

まずGCCやClangが生成するコード(org)と上記mul64+shrdの時間を比較します。
CPUはXeon w9-3495X/Ubuntu 24.04.2でturbo boost on状態でlpN=1~4と変化させて計測しました。単位はMclkです。

lpN|1|2|3|4
-|-|-|-|-
org|1016|1699|2459|3191
mul64+shrd|802|1599|2402|3198

### shrdの分解
lp=1のときはmul64+shrdの方が明確に速くなっていますが、lpN=2, 3, 4と大きくなるにつれその差は小さくなっています。
これはshrdのレイテンシが意外と大きいためと思われます。そこで

```nasm
shrd rax, rdx, a
```
を
```nasm
shr rax, a
shl edx, 64-a
or eax, edx
```
に置き換えてみます。後者は命令数が増えますがshrとshlは同時に実行できるため全体の処理時間は減る可能性があります。
上記ベンチマーク結果にmul64+mixedとして追加します。

lpN|1|2|3|4
-|-|-|-|-
org|1016|1699|2459|3191
mul64+shrd|802|1599|2402|3198
**mul64+mixed**|929|1516|2138|2805

lpN=1のときは若干悪くなりましたがlpNが2以上では速くなっています。

### 32ビット乗算版
次にコンパイラの戦略と同じく、32ビット×32ビット乗算でやりくりする戦略で考えます。
64ビットレジスタがあるので、32ビット加算におけるオーバーフローは考えなくてよいです。

```cpp
uint32_t div7opti(uint32_t x)
{
    uint64_t v = x * uint64_t(0x24924925);
    v >>= 32;
    v += x;
    v >>= 3; // = a  - 32;
    return uint32_t(v);
}
```

とするわけです。アセンブリ言語では

```cpp
// mul32
mov(eax, c_ & 0xffffffff);
imul(rax, x.cvt64());
shr(rax, 32);
add(rax, x.cvt64());
shr(rax, a_ - 32);
```
としました。`x` は入力レジスタで、`imul` すると `rax` (=v)にその結果が入り、32ビット右シフトしてから `x` を足して残りの `a-32` ビット右シフトします。
依存関係はありますが、命令数は先程の`mul+mixed`と同じです。

### ベンチマーク結果
ベンチマークをとりましょう。

lpN|1|2|3|4
-|-|-|-|-
org|1016|1699|2459|3191
mul64+shrd|802|1599|2402|3198
mul64+mixed|929|1516|2138|2805
**mul32**|793|1334|1912|2491

かなり改善されました。mulのビット数が減ってそのレイテンシが小さくなったおかげと思われます。必要なレジスタ数が減ったおかげもあるかもしれません。

# M4向け最適化
次にApple M4向け最適化を考えてみましょう。JIT生成部分は[Xbyak_aarch64](https://github.com/fujitsu/xbyak_aarch64)を使います。

## 64ビット乗算版
ARM64(ARMv9)には一度に64ビット×64ビット=128ビットを求める乗算命令はありません。
その代わりに64ビット×64ビットの下位64ビットを求める`mul`と上位64ビットを求める`umulh`があります。33ビットの定数は3回に分けてレジスタに設定する必要があります。

```cpp
// c = [1:cH:cL]
mov(w9, cL); // cの下位16ビット
movk(w9, cH, 16); // cの次の16ビットを16ビット左シフトしてセット
movk(x9, 1, 32); // x9 = c = [1:cH:cL] // 残りの1ビットを32ビット左シフトしてセット
mul(x10, x9, x); // c * xの下位64ビット
umulh(x9, x9, x); // c * xの上位64ビット
extr(x, x9, x10, cd.a_); // x = [x9:x10] >> a_
```

## 32ビット乗算版
`div7opti` 相当のコードも先に用意しましょう。

```cpp
mov(w9, cL); // cの下位16ビット
movk(w9, cH, 16); // cの次の16ビットを16ビット左シフトしてセット
umull(x9, wx, w9); // x9 = [cH:cL] * x
add(x, x, x9, LSR, 32); // x += x9 >> 32;
lsr(x, x, cd.a_ - 32);
```
乗算結果が64ビットに収まるので`umull`命令1個でよく、x64版で`shr+add`していたところは `add(x, x, x9, LSR, 32)` の1命令で実現できます。かなりコンパクトですね。

## ベンチマーク結果
ベンチマークはMacBook Pro, Apple M4 Pro, Sequoia 15.6で測定しました。単位はmsecです。

lpN|1|2|3|4
-|-|-|-|-
org|372|637|911|1185
mul64|310|506|741|974
**mul32**|311|507|723|941

x64版と同じくmul32が一番よいということが分かりました。

# まとめ
64ビットCPUでは32ビット時代に想定された最適化コードは冗長な演算をしており、33ビットな `c` と 右ビットシフト `a` に対して

```cpp
uint32_t div7opti(uint32_t x)
{
    uint64_t v = x * uint64_t(c & 0xffffffff);
    v >>= 32;
    v += x;
    v >>= a  - 32;
    return uint32_t(v);
}
```

相当のアセンブリコードがより速く計算できることが分かりました。
2025年8月時点でのコンパイラx64用gcc 13.3.0, clang 18.1.3, Visual Studio compiler 19.44.35214, Apple clang 17.0.0などで確認したところ、全て古い方法の最適化手法が使われていたのでこの手法を提案していきたいなと思います。

[constdiv.hpp](https://github.com/herumi/constdiv/blob/main/include/constdiv.hpp)の`ConstDivGen`クラスは実行時に与えられた `uint32_t d` に対して最適な

```cpp
uint32_t divd(uint32_t x) { return x / d; }
```

を生成します。x64/M4 Appleで動作確認しています。興味ある方はご覧ください。
