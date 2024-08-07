---
title: "楕円曲線暗号のための数学1（射影座標）"
emoji: "🧮"
type: "tech"
topics: ["楕円曲線暗号", "射影座標"]
published: true
---
## 初めに
[多倍長整数の実装](https://zenn.dev/herumi/articles/bitint-01-cpp), [有限体の実装](https://zenn.dev/herumi/articles/finite-field-01-add)と解説してきたので次は楕円曲線暗号の高速な実装方法を紹介します。
しばらくは数学の準備で、まずは射影座標を解説します。射影座標は通常の2次元座標 $(x, y)$ と無限遠点 $\infty$ を統一的に扱う座標です。

## 一覧

- [楕円曲線暗号のための数学1（射影座標）](https://zenn.dev/herumi/articles/projective-coordinate)(これ)
- [楕円曲線暗号のための数学2（バイナリ法によるスカラー倍算）](https://zenn.dev/herumi/articles/ecc-binary-method)
- [楕円曲線暗号のための数学3（ヤコビ座標）](https://zenn.dev/herumi/articles/ecc-jacobi-coordinate)
- [楕円曲線暗号のための数学4（ウィンドウ法）](https://zenn.dev/herumi/articles/ecc-mul-window)
- [楕円曲線暗号のための数学5（NAF）](https://zenn.dev/herumi/articles/ecc-mul-win-naf)
- [楕円曲線暗号のための数学6（GLV法と自己準同型写像）](https://zenn.dev/herumi/articles/ecc-mul-glv-endo)
- [楕円曲線暗号のための数学7（マルチスカラー倍算概要）](https://zenn.dev/herumi/articles/ecc-multi-scalar-multiplication)
- [楕円曲線暗号のための数学8（マルチスカラー倍算のパラメータ最適化）](https://zenn.dev/herumi/articles/ecc-multi-scalar-multiplication2)

## 楕円曲線の定義
Pythonを使った楕円曲線を実装する話は[楕円曲線暗号のPythonによる実装その2（楕円曲線とECDSA）](https://zenn.dev/herumi/articles/sd202203-ecc-2)でも解説してるので参考にしてください。
ここでは最小限の説明をしておきます。

楕円曲線は、有限体の元 $a, b \in  𝔽_p$ を固定して

$$
E=\Set{(x,y)\in {𝔽_p}^2 | y^2=x^3+ax+b} \cup \infty
$$
で定義される集合です（ $a, b$ が満たすべき細かい条件は省略）。
$P=(x_1,y_1)$, $Q=(x_2, y_2) \in E$ に対して $P$ と $Q$ の加算 $R=P+Q$ を

$x_1 \neq x_2$ ならば $L:=\frac{y_2-y_1}{x_2-x_1}$, $x_1 = x_2$ ならば $L:=\frac{3x_1^2+a}{2y_1}$ として

$x_3:=L^2-(x_1+x_2)$, $y_3:=L(x_1-x_3)-y_1$, $R:=(x_3, y_3)$

とします。
更に、無限遠点 $\infty$ を単位元 $O$ と記号を変えて

- $P+O=O+P=P$
- $-P:=(x_1,-y_1)$
- $P+(-P)=O$

とも定義します。これらを楕円曲線の加法公式といいます。
このように定義すると、任意の $P, Q, R \in E$ に対して、

1. （結合則） $(P+Q)+R=P+(Q+R)$.
2. （単位元の存在） $P+O=O+P=P$.
3. （逆元の存在） $P+(-P)=O$.
4. （可換）$P+Q=Q+P$.

が成り立ち、これにより $E$ は加法群となることが知られています。
結合則については[desmosによるグラフ](https://www.desmos.com/calculator/28wbmxtqiu?lang=ja)も参考にしてください。$P$, $Q$, $R$ を線に沿って動かせます。

## 除算のコスト
[楕円曲線暗号のPythonによる実装その2（楕円曲線とECDSA）](https://zenn.dev/herumi/articles/sd202203-ecc-2)では有限体の元 $x \neq 0$ に対して、

$$
x \cdot x^{p-2}=x^{p-1} \equiv 1 \pmod{p}
$$
が成り立つことを利用して $x^{-1}:=x^{p-2}$ と計算しました。今後これよりも高速な逆元計算方法を紹介する予定ですが、概ね

$$
\text{加算} \colon \text{乗算} \colon \text{逆元算} =1:10:100
$$
ぐらいのコストです。楕円曲線の加法公式には除算が含まれているので演算コストが高いです。
そこで昔から除算を避ける方法が考えられてきました。その一つが射影座標です。

たとえば $a/b + c/d$ を計算するときに、そのままなら $/b$, $/d$ の除算（逆元算）が2回必要です。
しかし、通分して $a/b+c/d=(ad+bc)/(bd)$ とすれば乗算が3回増えますが、逆元は1回に減ります。
前回の[lazyリダクションのアイデア](https://zenn.dev/herumi/articles/extended-field-2#lazy%E3%83%AA%E3%83%80%E3%82%AF%E3%82%B7%E3%83%A7%E3%83%B3%E3%81%AE%E3%82%A2%E3%82%A4%E3%83%87%E3%82%A2)と似てますね。
もう少しちゃんと説明しましょう。

## 射影座標
射影座標は $(x, y) \in 𝔽_p$ の代わりに3個の $𝔽_p$ の元 $X, Y, Z$ を用いて $x=X/Z$, $y=Y/Z$ と表現する方法です。 $[X:Y:Z]$ と表記することが多いです。
$(x, y)$ は楕円曲線 $E$ の点なので $y^2=x^3+ax+b$ を満たしました。そのため $x, y, z$ は $(Y/Z)^2=(X/Z)^3+a(X/Z)+b$, つまり $Y^2Z = X^3 + aX Z^2 + b Z^3$ を満たす必要があります。
$x=X/Z$, $y=Y/Z$ なので、$X$, $Y$, $Z$ を0でないある数 $c$ 倍して $X':=cX$, $Y':=cY$, $Z':=cZ$ としても $x=X'/Z'$, $y=Y'/Z'$ は成り立ちます。

$$
[X:Y:Z] = [cX:cY:cZ] \text{ for } c \neq 0.
$$

したがって、$(x, y)$ と $[X:Y:Z]$ の表記は1対1ではないことに注意してください。今までの $(x, y)$ という座標をAffine座標といいます。

### 無限遠点

$[X:Y:Z]=[0:1:0]$ は $Y^2 Z = X^3 + a X Z^2 + b Z^3$ を満たします。
しかし $Z$ が0のときは逆元が存在しないので対応するAffine座標の点がありません。実は $[0:1:0]$ はAffine座標で特別扱いしていた無限遠点 $O$ に対応します。
射影座標を用いると無限遠も統一的に扱えるというメリットがあります。

### 射影座標の点の同値性

$[X:Y:Z] = [X':Y':Z']$ である条件は $X/Z=X'/Z'$, $Y/Z=Y'/Z'$ ですが、等しいことを確認するのに除算は避けたいものです。そこで分母を払います。
すると条件は $X Z'=X'Z$, $YZ' = Y' Z$ に置き換えられます。この2個の等号が成り立てば元の点が等しいと言えます。


## 射影座標を用いた加法公式
$P=(x_1,y_1)=[X_1:Y_1:Z_1]$, $Q=(x_2,y_2)=[X_2:Y_2:Z_2]$ として加法公式に代入してみましょう。とりあえず $x_1 \neq x_2$ とします。


$$
L=\frac{y_2-y_1}{x_2-x_1}=\frac{Y_2/Z_2-Y_1/Z_1}{X_2/Z_2-X_1/Z_1}=\frac{Y_2 Z_1 - Y_1 Z_2}{X_2 Z_1 - X_1 Z_2}.
$$
分子を $S$, 分母を $T$ とすると $L=S/T$.

$$
x_3=S^2/T^2 - (X_1/Z_1+X_2/Z_2)=S^2/T^2-\frac{X_1 Z_2 + X_2 Z_1}{Z_1 Z_2}=\frac{S^2 Z_1 Z_2 - (X_1 Z_2 + X_2 Z_1)}{T^2 Z_1 Z_2}.
$$
煩雑になりますが、 $y_3$ も同様に $X_i, Y_i, Z_i$ の多項式の分数の形で表現できます。加減乗算回数は増えますが除算は取り除けました。
除算を取り除いた状態で、いかに加減乗算の回数を減らすかという研究が昔から続けられています。楕円曲線の定数 $a$, $b$ が特別な値のとき専用の公式もあります。
[Short Weierstrass curves](https://hyperelliptic.org/EFD/g1p/auto-shortw.html)には詳しい公式のリストが載っています。
mclでは乗算が12回, 2乗算が2回の[方法](https://github.com/herumi/mcl/blob/f1b89e84166b312bf2319b5e392de0439b6aa231/include/mcl/ec.hpp#L734)を使っています。

### ベンチマーク
secp256k1という楕円曲線でAffine座標を用いた場合と射影座標を用いた場合の加算公式のベンチマークを簡単にとってみました。

方式|Affine座標|射影座標
-|-|-
add|15660|1104

射影座標による計算が10倍以上速いことが分かります。
