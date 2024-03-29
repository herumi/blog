---
title: "2次拡大体の基本"
emoji: "🧮"
type: "tech"
topics: ["2次拡大体", "Karatsuba法"]
published: true
---
## 初めに
[有限体の実装](https://zenn.dev/herumi/articles/finite-field-01-add#%E6%9C%89%E9%99%90%E4%BD%93%E3%81%AE%E5%AE%9F%E8%A3%85%E4%B8%80%E8%A6%A7)では加減乗算の方法を解説しました。
ここではそれらを元に2次拡大体の計算方法を紹介します。2次拡大体は[ペアリング](https://zenn.dev/herumi/articles/pairing-l2he)や[BLS署名](https://zenn.dev/herumi/articles/bls-signature)などで使われます。

## 2次拡大体
以前、[標数2の体](https://zenn.dev/herumi/articles/extension-field-of-f2)の話で標数2の体の2次拡大体を紹介しました。
ここではBLS署名などで利用される大きな素数の体の2次拡大体を説明します。

$p$ を2より大きな素数とし、有限体 $𝔽_p=\Set{0, 1, \dots, p-1}$ を考えます。
$𝔽_p$ の中で $X$ を変数とする方程式 $X^2+1=0$ を考えます。
たとえば、$p=5$ なら $2^2+1=5 \equiv 0 \pmod{5}$ なので $X=2$ が解です。しかし $p=7$ なら $X^2+1$ に $X = 0, 1, \dots, 6$ を代入すると順に 1, 2, 5, 3, 3, 5, 2 となり解が存在しません。
以下では後者のような $X^2+1=0$ が解を持たない素数 $p$ を考えます。

$X$ を変数とし $𝔽_p$ 係数の1変数多項式の全体を $X^2+1$ で割った余りの集合を $𝔽_p[X]/(X^2+1)$ と書きます。
$X^2+1$ で割った余りは $a X + b$ ($a, b \in 𝔽_p$)という1次式です。つまり、 $𝔽_p[X]/(X^2+1)$ は1次式の集合です。

$$
𝔽_p[X]/(X^2+1)=\Set{a X + b | a , b \in 𝔽_p}
$$

この集合を $𝔽_p$ の2次拡大体 $𝔽_{p^2}$ といいます。

## 2次拡大体の四則演算

$𝔽_{p^2}$ は四則演算ができます。確認しましょう。
$𝔽_{p^2}$ の要素 $f=a+b X$, $g=c+d X$ に対して $f \pm g = (a \pm c) + (b \pm d)X$ として加減算が入ります。
乗算は $fg = (a + b X)(c + d X) = ac + (ad + bc)X + bd X^2$ として $X^2 \equiv -1 \pmod{X^2+1}$ なので
$fg = (ac-bd) + (ad + bc)X$ となります。
最後に $f=a + b X \neq 0$ の逆元 $1/f$ は分子分母に $a - b X$ を掛けて

$$
1/f=\frac{a - b X}{(a + b X)(a - b X)} = \frac{a - b X}{a^2 - b^2 X^2}=\frac{a - b X}{a^2+b^2}
$$
となります。$a^2+b^2 \in 𝔽_p$ は 0 ではないので $𝔽_p$ の中で逆元があり、$1/f$ を計算できます。

毎回「 $X^2+1$ で割った余り……」と書くのは面倒なので $X$ を $i$ と置き直して $i^2=-1$ という記号にしてしまいましょう。

$f = a + b i$, $g=c + d i \in 𝔽_p$ に対して
- $f \pm g = (a \pm c)+(b\pm d)i$
- $fg = (a c - b d) + (ad + bc)i$
- $1/f= (a-bi) / (a^2+b^2)$

と複素数の計算と同じ感覚で扱えます。

## Karatsuba法
上記の $f$, $g$ の乗算では $(ac-bd) + (ad + bc) i$ なのでそのまま計算すると $ac$, $bd$, $ad$, $bc$ の4回 $𝔽_p$ の乗算が必要です。
その代わりに次のようにすると乗算回数を3回に減らせます。

$(a+b)(c+d)=ac + ad + bc + bd$ なので $ad + bc = (a+b)(c+d) - ac - bd$ です。
そこで $ac$, $bd$, $(a+b)(c+d)$ を計算してから この式で $ad + bc$ を計算するのです。
元の方法では、乗算が4回, 加算が $ac-bd$, $ad + bc$ の2回でした。
新しい方法では, 乗算が3回, 加算が5回です。加算が3回増える代わりに乗算が1回減りました。
つまり、乗算のコストが加算のコストの3倍以上ならば新しい方法が速くなります。
この方法をKaratsuba法といい、様々な場所で使われます。

## 2乗算の高速化
$f=a+bi$ の2乗 $f^2$ は

$$
f^2=(a+bi)^2=(a^2-b^2)+2abi
$$

です。乗算回数は $a^2$, $b^2$, $ab$ の3回ですが次のようにすると2回に減らせます。

$$
a^2-b^2=(a+b)(a-b)
$$

このように拡大体の高速実装では、乗算回数を減らす式変形が重要です。

## 2次拡大体上の楕円曲線
当たり前ですが、2次拡大体も「体」なのでそれを用いた楕円曲線を考えられます。
$a, b \in 𝔽_{p^2}$ として（細かい条件を省略して）

$$
E=\Set{(x,y)\in (𝔽_{p^2})^2 | y^2 = x^3 + ax + b}\cup O
$$

を $𝔽_{p^2}$ 上の楕円曲線といいます。
[BLS署名](https://zenn.dev/herumi/articles/bls-signature)では2種類の楕円曲線 $G_1$, $G_2$ が登場していましたが、通常 $G_1$ が $𝔽_p$ 上の楕円曲線, $G_2$ が $𝔽_{p^2}$ 上の楕円曲線です。
$G_2$ が $G_1$ の2倍の大きさで、演算コストが約3倍というのは $G_1$ に対して要素の数が2倍、乗算コストが約3倍(Karatsuba法)だからです。
