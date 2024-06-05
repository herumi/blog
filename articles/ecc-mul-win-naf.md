---
title: "楕円曲線暗号のための数学5（NAF）"
emoji: "🧮"
type: "tech"
topics: ["楕円曲線暗号", "スカラー倍算", "NAF"]
published: true
---
## 初めに
前回、楕円曲線のスカラー倍算の高速化手法の一つ、[ウィンドウ法](https://zenn.dev/herumi/articles/ecc-mul-window)を紹介しました。
今回は、それを改善してテーブル作成コストを半減するNAFという手法を紹介します。

## 符号つき2進数
楕円曲線の点の減算 $P - Q$ は $P + (-Q)$ と考えて処理します。$-Q$ は $Q$ の $y$ 座標の符号を反転させるだけなので計算が容易です。
たとえば $23P$ をバイナリ法で計算する場合、23は2進数で `0b10111` なので $23P=(16P) + (4P) + (2P) + P$ と処理します。この場合、dblの処理が1→2→4→8→16で4回、addが3回です。
しかし、$23=16+8-1$ でもあるので $23P=(16P) + (8P) + (-P)$ と計算すると、dblの回数は4回と変わりませんが、addは2回に減ります。
これは、通常の2進数が0と1だけで表現するのに対して、0, 1, -1で表現していることになります。この様な表現を符号つき2進数と呼んで、$23=[1:1:0:0:-1]$ と表記することにします。

$$
23=1 \cdot 2^4 + 1 \cdot 2^3 + 0 \cdot 2^2 + 0 \cdot 2^1 + (-1) \cdot 2^0.
$$

## NAF
符号つき2進数は冗長で、単なる1でも $1=2-1=[1:-1]$ とでも $1=4-2-1=[1:-1:-1]$ と、何通りも書けてしまいます。
元々の目的は、楕円曲線の点のスカラー倍算を効率よく計算したいというものですから、普通の2進数で計算するよりもコストがかかっては意味がありません。
1（あるいは-1）の立ってるビットが一番小さくなる形が望ましいです。そのような性質をハミング重みが最小といい、そのように表現された符号つき2進数の中でも、0でないビット（$\pm 1$）が隣り合わないように調整したものを非隣接形式 (NAF : Non-Adjacent Form)  といいます。

たとえば $1=[1:-1]$ や $23=[1:1:0:0:-1]$ は $\pm 1$ が連続しているところがあるのでNAFではありません。
23の場合は $[1:0:-1:0:0:-1]$ がNAFです。

$$
23=1 \cdot 2^5 + 0 \cdot 2^4 + (-1) \cdot 2^3 + 0 \cdot 2^2 + 0 \cdot 2^1 + (-1) \cdot 2^0.
$$

アルゴリズム的には普通の2進数展開した後、下位ビットから1が連続しているところがあれば $[0:1:\dots:1:1]$ を $[1:0:\dots:0:-1]$ にするだけです。

*通常2進数からNAFへの変換の例*

ビット|5|4|3|2|1|0
-|-|-|-|-|-|-
2進数|0|1|1|1|1|1|1
NAF|1|0|0|0|0|-1

NAFにすると元の2進数のビット長よりも1ビット長くなる場合があることに気をつけてください。

## ウィンドウ法とNAFの組み合わせ
ウィンドウ法は $w$ ビットずつ区切ってテーブルルックアップと組み合わせる方式でした。

$w=4$ の場合、テーブルは $0, P, 2P,  \dots, 15P$ を計算し、改善されたウィンドウ法では奇数項のみ計算するので $P, 3P, 5P, 7P, 9P, 11P, 13P,  15P$ を計算しました。
ここで $9=16-7$, $11=16-5$, $13=16-3$, $15=16-1$ なので、$9P$ 以上は $16P - iP$ （$0 \le i \le 7$）とすれば、テーブルは $P, 3P, 5P, 7P$ と更に半分にできます。

このような数値に分解するには普通の2進数を下位ビットからみて、偶数(0)である限りスキップし、1が始まったらそこから $w$ ビット切り出して、その値が $2^{w-1}$ 以上なら繰り上げて、切り出した値の符号を反転する、ということを繰り返します。
Pythonで記述すると次のようになります。

```python
def naf(x, w):
  tbl = []
  H = 1<<(w-1)
  W = H*2
  mask = W-1
  zeroNum = 0
  while x > 0:
    while x & 1 == 0:
      x >>= 1
      zeroNum += 1
    for i in range(zeroNum):
      tbl.append(0)
    v = x & mask
    x >>= w
    if v & H:
      x += 1
      v -= W
    tbl.append(v)
    zeroNum = w-1
  return tbl
```

## $w=4$ のときの具体例

$x$ が0から31まで2進数表記と、$w=4$ のときの出力を並べました。$w=4$ のときは、$\pm 1, \pm 3, \pm 5, \pm 7$ だけが現れているのが分かります。
また、係数 $[x_{l-1}:\dots:x_0]$ について普通の2進数と同じく $x=\sum_{i=0}^{l-1} x_i 2^i$ が成り立っていることを確認してください。

*4ビットNAF的分解*
x|2進数|4ビットNAF的分解
-|-|-
0|0|0
1|1|1
2|10|1 0
3|11|3
4|100|1 0 0
5|101|5
6|110|3 0
7|111|7
8|1000|1 0 0 0
9|1001|1 0 0 0 -7
10|1010|5 0
11|1011|1 0 0 0 -5
12|1100|3 0 0
13|1101|1 0 0 0 -3
14|1110|7 0
15|1111|1 0 0 0 -1
16|10000|1 0 0 0 0
17|10001|1 0 0 0 1
18|10010|1 0 0 0 -7 0
19|10011|1 0 0 0 3
20|10100|5 0 0
21|10101|1 0 0 0 5
22|10110|1 0 0 0 -5 0
23|10111|1 0 0 0 7
24|11000|3 0 0 0
25|11001|1 0 0 0 0 -7
26|11010|1 0 0 0 -3 0
27|11011|1 0 0 0 0 -5
28|11100|7 0 0
29|11101|1 0 0 0 0 -3
30|11110|1 0 0 0 -1 0
31|11111|1 0 0 0 0 -1

## コスト計算

$x$ が256ビットのときは、$w=6$ がaddのコスト最小となり、そのときのコストはテーブル作成に $2^{w-2}-1=15$, ループ中の加算が $\text{ceil}(256/6)=43$ で合計58です。
前回の修正ウィンドウ法の67よりもかなり小さくなりました。