---
title: "定数除算最適化再考2"
emoji: "📖"
type: "tech"
topics: ["整数", "除算", "逆数乗算"]
published: false
---
## 整数の定数除算最適化の話の続きをします。

## [前回](https://zenn.dev/herumi/articles/const-div-revised1)のまとめ
$M$ を1以上の整数、割る数 $d \in [1, M]$ を定数とします。
$M_d:=M-((M+1)\texttt{\%}d)$ とします。
**定理**
整数 $A \ge d$ をとり、$c :=  \mathrm{ceil}(A/d)=(A+d-1) \texttt{//} d$, $e := d c - A$ とします。
もし $e M_d < A$ ならば全ての整数 $x \in [0, M]$ について $x \texttt{//} d = (x c)\texttt{//}A$ が成り立つ。

## $A$ の探索コード

定理自体は $A$ が2べきでない整数でも成り立ちますが、実際に利用するのは $A=2^a$ の形で探します。

整数の範囲を`uint32_t`に限るなら、$M=2^{32}-1$ で $e < d \le M$ なので $A=2^{64}$ とすれば $e M_d < A$ が成り立ち、定理を満たす $A$ は存在します。

もし、$d$ が $2^{31}=\texttt{0x80000000}$ 以上ならば、$x \in [0, M]$ について $x\texttt{//}d \in [0, 1]$ なので $x \ge d$ なら1を返すだけでよいです。

```cpp
// return x//d for d >= 0x80000000
uint32_t divd(uint32_t x)
{
    return x >= d_ ? 1 : 0;
}
```

したがって、$d < \texttt{0x80000000}$ を仮定しましょう。
$A$ は $d$ より大きい2べきの整数から始めればよいので $a=\mathrm{ceil}(\log_2(d))$ として条件を満たす $a$ を探します。
コードは次の通り

```cpp
static const uint32_t M = 0xffffffff;
bool init(uint32_t d)
{
    const uint32_t M_d = M - ((M+1)%d);
    assert(d < 0x80000000);
    for (uint32_t a = ceil_ilog2(d); a < 64; a++) {
        uint64_t A = uint64_t(1) << a;
        uint64_t c = (A + d - 1) / d;
        uint64_t e = d * c - A;
        if (e * M_d < A) { // find
           // (a, A, c) が欲しい答え
           return true;
        }
    }
    return false;
}