---
title: "定数除算最適化再考1"
emoji: "📖"
type: "tech"
topics: ["整数", "除算", "逆数乗算"]
published: true
---
## 整数の除算
[有限体の実装](https://zenn.dev/herumi/articles/finite-field-01-add)の話では加減乗算はしていましたが、除算はやってませんでした。
除算を避けるために[Montgomery乗算](https://zenn.dev/herumi/articles/finite-field-03-mul)を紹介していましたが、除算が必要になる場面はどうしてもあります。
ここでは手始めにuint32_tを対象としたコンパイラに組み込まれている定数除算最適化アルゴリズムを紹介します。

## 定数除算の最適化のアイデア
`uint32_t`などの符号なし整数 $n$ を同じく符号なし整数 $d$ で除算することを考えます。このとき余りは切り捨てます。
ここではPythonの除算記号`//`を使って $n \texttt{//} d$ と書くことにします。

$$
n \texttt{//} d := \mathrm{floor}(n/d).
$$

大抵のCPUは整数の論理右シフト命令を持ち、それにより2べきの整数 $A=2^a$ による除算は高速に実行できます。

$$
n \texttt{//} 2^a = n \texttt{>>} a.
$$

そこで前もって $1/d$ に近くなるような整数 $c$ と2べきの整数 $A$ を選びます。

$$
1/d \sim c / A.
$$

そして $n \texttt{//} d \sim (n \times c)/A = (n \times c)\texttt{>>}a$ と除算を乗算1回と右シフト1回に変換するのが定数除算のアイデアです。
1970年代から提案され, Barret Reductionと呼ばれる手法の論文  "Implementing the Rivest Shamir and Adleman Public Key Encryption Algorithm on a Standard Digital Signal Processor"(Barret, CRYPTO'86) が有名です。
近似なのでどううまく $c$ や $a$ をとると誤差が小さくなるかがポイントです。
そして["Division by Invariant Integers using Multiplication"(Granlund, Montgomery, SIGPLAN 1994)](https://gmplib.org/~tege/divcnst-pldi94.pdf) とその改善版『Hacker's Delight 1st. edition』(Warren, 2002)アルゴリズムがGCC/Clang/Visual Studioなどのコンパイラに組み込まれています（と思われます）。

## コンパイラの出力例
コンパイラの出力例をいくつか見てみましょう。

```cpp
#include <stdint.h>

uint32_t div3(uint32_t x)
{
    return x/3;
}

uint32_t div7(uint32_t x)
{
    return x/7;
}
```

```nasm
; x64
; gcc-14 a.c -S -masm=intel -O3
div3:
    mov eax, edi
    mov edx, 2863311531
    imul    rax, rdx
    shr rax, 33
    ret

div7:
    mov eax, edi
    imul    rax, rax, 613566757
    shr rax, 32
    sub edi, eax
    shr edi
    add eax, edi
    shr eax, 2
    ret
```

```nasm
; AArch64
; clang-18 --target=aarch64 -O3 -S a.c
div3:
    mov w8, #43691
    movk    w8, #43690, lsl #16
    umull   x8, w0, w8
    lsr x0, x8, #33
    ret

div7:
    mov w8, #18725
    movk    w8, #9362, lsl #16
    umull   x8, w0, w8
    lsr x8, x8, #32
    sub w9, w0, w8
    add w8, w8, w9, lsr #1
    lsr w0, w8, #2
    ret
```

いくつかマジックナンバーが登場しています。
アルゴリズムの詳細は後述しますが、x64のdiv3に現れる `2863311531` は16進数で `0xaaaaaaab` であり、これはAArch64のdiv3の`43691 + (43690<<16)`と同じです。
x64のdiv7の`613566757`もAArch64のdiv7の`18725+(9362<<16)`に等しく、同じ定数除算最適化手法を用いていることが伺われます。

## 定数の求め方
今回は["Integer division by constatns: optimal bounds" (Lemire, Barlett, Kaser, 2020)](https://arxiv.org/abs/2012.12369)を参考にしつつ、定式化しましょう（今回紹介する定理は論文とは見かけが異なりますが同値なものです）。

まず記号を準備します。$M$ を1以上の整数、割る数 $d \in [1, M]$ を定数とします。
非負の整数 $a$, $b$ に対して $a$ を $b$ で割った余りを $a \texttt{\%} b$ と書きます。
それから天下り的に $M_d$ を $d$ で割って余りが$d-1$ になる $M$を越えない最大の整数とします。

$$
M_d:=\max \Set{n \in [1, M] | n \texttt{\%} d = d-1}=M-((M+1)\texttt{\%}d).
$$

なぜ、こんな数字を定義するかはこの後説明します。

**定理**
整数 $A \ge d$ をとり、$c :=  \mathrm{ceil}(A/d)=(A+d-1) \texttt{//} d$, $e := d c - A$ とします。
もし $e M_d < A$ ならば全ての整数 $x \in [0, M]$ について $x \texttt{//} d = (x c)\texttt{//}A$ が成り立つ。

**解説**
まず $e$ の構成法から $0 \le e \le d-1 < A$ です。
$1/d = c/(A+e)$ なので $1/d$ を $c/A$ で近似したときの誤差的なものが $e$ です。
その $e$ に対して $e M_d < A$ という条件が成り立つ2べきの $A$ が見つかれば、定数除算を乗算+シフト演算に置き換えられるということです。
まず定理の証明をしましょう。

**証明**
$x \in [0, M]$ に対して $(q, r) := \mathrm{divmod}(x, d)$ とします。つまり $q := x \texttt{//} d$, $r := x \texttt{\%} d$ で $x = qd + r$.
このとき

$$
x c = q d c + r c = q (A + e) + r c = q A + (q e + r c).
$$

$y:=q e + r c$ とおくと、もし $0 \le y < A$ ならば $(x c) \texttt{//} A = q = x \texttt{//} d$ となり証明が完了します。そこで $y$ を $d$ 倍して

$$
f(x):=y d = (q e + r c)d = (x - r)e + r(A + e) = e x + A r
$$

として $x$ が $[0, M]$ を動いたときの $f(x)$ の最大値 $B:=\max f(x)$ を考えましょう。
$r=x \texttt{\%} d$ は $x$ に依存して動くことに注意してください。
$(q_0, r_0):=\mathrm{divmod}(M, d)$ とします。

*$(x,r)$のとり得る範囲*
![xとrのとり得る範囲](/images/range-x-r.png =600x)

$x$ が0から $M$ まで増えるとき、$r$ は $[0, d-1]$ の範囲を繰り返します。
$M_d$ は $x\texttt{\%} d$ が $d-1$ になるときの最大の $x$ でした。
$A$ と $e$ は0以上の定数なので $f(x)$ が最大値を取るのは $(x,r)=(M_d,d-1)$ か $(M,r_0)$ のどちらかです。

### $M_d=M$ のとき
$r_0 = d-1$ なので $B=f(M_d) = e M_d + A (d-1)$.
仮定 $e M_d < A$ より $B < A + A d - A = A d$. よって $\max(y) = \max f(x)/d = A$ が示せました。

### $M_d < M$ のとき
最大値をとる候補は2個 $B_1:=f(M_d)=e M_d + A(d-1)$ か $B_2:=f(M)= eM + A r_0$ です。
$B_1$ は $M_d=M$ のときと同じなので $B_2$ を考えます。$B_2 < B_1$ を示せば $B=\max(B_1,B_2)=B_1$ となります。

$M_d < M$ つまり $r_0 \le d-2$ かつ $M= M_d + 1 + r_0$ を代入すると

$$
\begin{align*}
B_1-B_2&=(d-1-r_0)A - e(M - M_d)=(d-1-r_0)A - e(1 + r_0)\\
& \ge (d-1-(d-2)) A - e(1 + d-2) = A - e d + e \ge d(c - e) > 0.
\end{align*}
$$

$c > e$ は $e M_d < A$ と $d-1 \le M_d$ から $e(d-1) < A = c d - e$ から従います。
いずれのときも $B=\max f(x) = f(M_d) < A d$ が示せました。

またこの定理により、$M_d$ という値が重要な定数であることが分かりました。

長くなったので、ひとまずここまで。
