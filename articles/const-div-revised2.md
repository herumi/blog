---
title: "定数除算最適化再考2"
emoji: "📖"
type: "tech"
topics: ["整数", "除算", "逆数乗算"]
published: true
---
## 初めに
整数の定数除算最適化の続きです。
今回は実際に最適化に必要な定数の求め方、コンパイラが出力したコードの解説をします。

## [前回](https://zenn.dev/herumi/articles/const-div-revised1)のまとめ
$M$ を1以上の整数、割る数 $d \in [1, M]$ を定数とします。
$M_d:=M-((M+1)\texttt{\%}d)$ とします。
**定理**
整数 $A \ge d$ をとり、$c :=  \mathrm{ceil}(A/d)=(A+d-1) \texttt{//} d$, $e := d c - A$ とします。
もし $e M_d < A$ ならば全ての整数 $x \in [0, M]$ について $x \texttt{//} d = (x c)\texttt{//}A$ が成り立つ。

## $A$ の探索コード

定理自体は $A$ が2べきでない整数でも成り立ちますが、実際に利用するのは $A=2^a$ の形で探します。

今回は `uint32_t` の範囲で考えるので $M=2^{32}-1$とします。もし、$d$ が $2^{31}=\texttt{0x80000000}$ 以上ならば、$x \in [0, M]$ について $x\texttt{//}d \in [0, 1]$ なので $x \ge d$ なら1を返すだけでよいです。

```cpp
// return x//d for d >= 0x80000000
uint32_t divd(uint32_t x)
{
    return x >= d_ ? 1 : 0;
}
```

したがって、$d < \texttt{0x80000000}$ を仮定しましょう。
$A$ は $d$ より大きい2べきの整数から始めればよいので $a=\mathrm{ceil}(\log_2(d))$ として条件を満たす $a$ を探します。
コードは次の通り:

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
```
$d$ が $b$ ビット、$M$ が $n$ ビットなら $A=2^a=2^{b+n}$ とすると $e M_d < d M \le A$ が成り立ち定理を満たす $A$ が存在します。
特に $b+n \le 31 + 32 = 63$ なのでループは必ず `true` で終了します。
また $A=2^{b+n}$ のとき $d \ge 2^{b-1}$ より $c=(A+d-1) \texttt{//} d < 2^{n+1}$ なので $c$ の最小値は大きくても $n+1=33$ ビットとなります。

（注意）$d$ が2べきのときは$A=d=2^a$, $c=1$, $e=0$ とできるので $e M_d < A$ が成り立つ（単なる論理右シフト）。

## 3で割る関数 `div3`
前節の探索コードで $d=3$ のとき計算すると $a=33$, $c=\texttt{0xaaaaaaab}$ となり、[コンパイラの出力例](https://zenn.dev/herumi/articles/const-div-revised1#%E3%82%B3%E3%83%B3%E3%83%91%E3%82%A4%E3%83%A9%E3%81%AE%E5%87%BA%E5%8A%9B%E4%BE%8B)で紹介した定数が現れます。

したがって3で割る関数は

```cpp
uint32_t div3(uint32_t x)
{
  const uint32_t c = 0xaaaaaaab;
  const int a = 33;
  // return x / 3; と同じ
  return uint32_t(x * uint64_t(c)) >> a);
}
```
となります。計算途中は `uint64_t` にキャストしないと正しく求まらないことに注意してください。
これがアセンブラの出力

```nasm
; x64
; gcc-14 a.c -S -masm=intel -O3
div3:
    mov eax, edi
    mov edx, 2863311531
    imul    rax, rdx
    shr rax, 33
    ret
```
に相当するコードです。

## 7で割る関数 `div7`
それでは7で割るコードはなぜもっと複雑なのでしょうか。
それは $d=7$ のときを計算すると $a=35$, $c=\texttt{0x124924925}$ となるからです。
一見、3で割るコードと同じでよいように見えます。しかしよくみると $c$ は33ビットあります。
したがって、`uint32_t` である $x$ との積は最大65ビットとなり `uint64_t` の範囲に入りません。

これを解決する一つの方法は`uint128_t`を使うことです。
GCC/Clangは

```cpp
typedef __attribute__((mode(TI))) unsigned int uint128_t;
```

により`uint128_t`が使えるので

```cpp
uint32_t div7_by_uint128_t(uint32_t x)
{
  const uint64_t c = 0x124924925;
  const int a = 35;
  // return x / 7; と同じ
  return uint32_t(x * uint128_t(c)) >> a);
}
```

しかし、コンパイラは64ビット×64ビット=128ビット乗算を避けるためにちょっとトリッキーなことをしています。
次節でそれを解説しましょう。

## コンパイラの`div7`

### コンパイラの生成コード
まず先にコンパイラが生成したアセンブリコードに相当するCのコードを示します。

```cpp
uint32_t div7org2(uint32_t x)
{
    uint64_t y = x * (c & 0xffffffff); // code1
    y >>= 32; // code2
    assert(x >= y);
    uint32_t t = uint32_t(x) - uint32_t(y); // code3-1
    t >>= 1; // code3-2
    t += uint32_t(y); // code3-3
    t >>= 2; // code3-4
    return t;
}
```

### アルゴリズム
$c$ が33ビットなので上位1ビットの $c_H$ と下位32ビットの $c_L$ に分けましょう。$c$ は33ビットとわかっているので $c_H = 1$ です。
32ビットの $x$ との積は $c x = (2^{32} + c_L) x = x*2^{32} + c_L x$.

![](/images/mul33x32.png)

$y = c_L x$ がコースコードの $y$ です(code1)。
積を$a=35$ビット論理右シフトするため $y$ の下位32ビットは不要です。そのため先に32ビットシフトしておきます(code2)。

したがって次に計算するのはシフト後の $((c_L x) \texttt{>>} 32) + x$ です。計算後に更に残りの $a-32$ ビット論理右シフトします。
ただその割にはソースコードはややこしいことをしているように見えます。以下、解説します。

### 32ビットのオーバーフロー対策
$x$, $y=(c_L x) \texttt{>>} 32$ を32ビット整数とし、$(x + y) \texttt{>>} s$, $s=a-32$ を求めます。
$z=(x + y) \texttt{>>} 1$ が求まれば後 $s-1$ ビットシフトすればよいので $z$ を求めることに専念します。
ここで $x + y$ は33ビットになりえることに注意しましょう。コンパイラはそれを避けるために次の方法をとっています。

$y$ は $c_L x$ の上位32ビットなので $x \ne 0$ なら $x$ より小さいです（$x$ より大きいと $c_L \ge 2^{32}$ となってしまう）。つまり $x \ge y$.
$(x+y)/2 = (x-y)/2+y$ より

$$
(x+y)\texttt{>>}1 = ((x-y)\texttt{>>}1)+y
$$

が成り立ちます。左辺は32ビットに収まるので右辺も収まります。よって右辺の方法で求めれば中間の値も32ビットに収まります。
これがcode3-1, ..., code3-4の部分に当たります。

以上でコンパイラの生成するコードの解説が終わりました。
