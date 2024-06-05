---
title: "楕円曲線暗号のための数学6（GLV法）"
emoji: "🧮"
type: "tech"
topics: ["楕円曲線暗号", "スカラー倍算", "MSM", "GLV"]
published: false
---
## 初めに
今回は、楕円曲線の数学的な性質を用いたスカラー倍算の高速化手法の一つ、GLV法（Gallant-Lambert-Vanstone）を紹介します。
GLV法は小さいマルチスカラー倍算を利用するので、まずそちらから解説しましょう。

## マルチスカラー倍算
一般に、楕円曲線の点 $P_1, \dots, P_n$, 整数 $a_1, \dots, a_n$ が与えられたときに

$$
Q=a_1 P_1 + \dots + a_n P_n
$$

を求める操作をマルチスカラー倍算 （MSM : multi-scalar multiplication）といいます。
zk-SNARKなどでは大きな $n$（数千~数万）に対するMSMが現れます。そのアルゴリズムはまたの機会に紹介するとして、ここでは $n=2$, つまり楕円曲線の点 $P_1$, $P_2$ と、整数 $x_1$, $x_2$ に対して $Q=x_1 P_1 + x_2 P_2$ を求める手法を考察します。
もちろん、$x_1 P_1$ と $x_2 P_2$ を求めて足せば $Q$ が求まりますが、それよりもよい方法があります。
$x_i P_i$ の計算は前回までの $w$ ビットの[NAF](https://zenn.dev/herumi/articles/ecc-mul-win-naf)など用いて計算するのですが、そのメインループは次の形でした。

```python
# return P * x
def mul(P, x, w):
  # w ビットNAFテーブル作成
  naf = make_NAF(x, w)
  tbl = make_table(P, w)
  Q = Ec()
  for v in range(naf):
    Q = dbl(Q)
    if v:
      Q = add(Q, tbl[v]) if v > 0 else sub(Q, tbl[v])
  return Q
```

ループの中で $Q$ をdblしながらテーブル引きをしてadd (or sub) します。ここで $x_1 P_1$ と $x_2 P_2$ のループをそれぞれ回すのではなく、dblを共通化し、addだけをするのです。

```python
# return P1 * x1 + P2 * x2
def mul(P1, x1, P2, x2, w):
  # w ビットNAFテーブル作成
  naf1 = make_NAF(x1, w)
  naf2 = make_NAF(x2, w)
  tbl1 = make_table(P1, w)
  tbl2 = make_table(P2, w)
  Q = Ec()
  for i in range(len(naf1)):
    Q = dbl(Q)
    v = naf1[i]
    if v:
      Q = add(Q, tbl1[v]) if v > 0 else sub(Q, tbl1[v])
    v = naf2[i]
    if v:
      Q = add(Q, tbl2[v]) if v > 0 else sub(Q, tbl2[v])
  return Q
```

このようにすると、$x_i$ が $n$ ビット長なら個別に計算するとdblの回数が $2n$ だったのが、$n$ に減ります。
[ECDSAの検証](https://zenn.dev/herumi/articles/sd202203-ecc-2#ecdsa%E3%81%AE%E6%A4%9C%E8%A8%BC)では、`Q=P * u1 + S * u2` という処理があります。
実は、ここでもマルチスカラー倍算のテクニックが利用できます。

## GLV法のコアアイデア
楕円曲線が与えられたときに、
1. ある特殊な定数 $L$ があって、任意の楕円曲線の点 $P$ の $L$ 倍 $L P$ が高速に求めらる（algo1）
2. 任意の整数 $x$ に対して $x = a + b L$ となる整数 $a$, $b$ でビット長が $x$ の半分ぐらいのサイズになるものを見つけられる（algo2）

とします。
そうすると、$Q=x P = (a+b L)P = aP + b (L P)$ となり、$L P$ を高速に求めた後、$n=2$ のマルチスカラー倍算を利用して $Q$ を求めるのです。
$a$, $b$ のビット長が $x$ の半分なのでdbl の回数が半分となり、高速化されるというものです。

とても面白いアイデアです。が、そのような都合のよいalgo1, algo2は見つかるのでしょうか。
以下では、特に曲線BLS12-381を念頭に説明します。

## 自己準同型写像
$p$ を3以上の素数、$𝔽_p$ を有限体、$𝔽_p$ の要素からなる楕円曲線の点の集合を

$$
E(𝔽_p) = \Set{(x,y)|y^2=x^3+ax + b} \cup O
$$

とします。$\varphi : E \rightarrow E$ で $\varphi(P+Q)=\varphi(P)+\varphi(Q)$ となる写像を自己準同型写像 (endomorphism) といいます。
