---
title: "より一般の2次拡大体"
emoji: "🧮"
type: "tech"
topics: ["一般の2次拡大体", "四則演算"]
published: true
---
## 初めに
[2次拡大体の基本](https://zenn.dev/herumi/articles/extended-field-1)では2次拡大体の四則演算を解説しました。
そこでは $X^2+1$ という多項式を利用した2次拡大体を構成しましたが、その式を利用できない場合の方法を紹介します。たとえばBLS12-377曲線がそれに該当します。
今回は前回の続きなのでまずはそちらを読んで復習しておいてください。

## より一般の2次拡大体
$p=7$ のとき2乗して$-1 \equiv 6 \pmod{7}$ になる値は存在しませんでした。
つまり $X^2+1=0$ は有限体 $𝔽_p=\Set{0, 1, \dots, p-1}$ の中に解を持ちません。
このとき $X$ を変数とする $𝔽_p$ を係数とする多項式全体の集合 $𝔽_p[X]$ を $X^2+1$ で割った余りの集合 $𝔽_p[X]/(X^2+1)$ が2次拡大体となりました。
このロジックは $p=5$ のときは利用できません。なぜなら $2 \times (-2) = -4 \equiv 1 \pmod{5}$ なので $X^2+1=(X-2)(X+2)$ と因数分解できてしまうからです。

その代わり、$p=5$ のとき方程式 $X^2+2=0$ は解を持ちません。何故なら $X=0, 1, 2, 3, 4$ のとき $X^2+2 \bmod{5}$ は $2, 3, 1, 1, 3$ で $0$ にならないからです。
そのため $𝔽_p[X]/(X^2+2)$ が $𝔽_5$ の2次拡大体となります。

## 複素数的な表記
[複素数](https://zenn.dev/herumi/articles/extension-field-of-f2#%E8%A4%87%E7%B4%A0%E6%95%B0)で紹介したように、$X^2+1=0$ は実数 $ℝ$ の中で解を持たないため
$ℝ[X]/(X^2+1)$ は $ℝ$ の2次拡大体、つまり複素数体 $ℂ$ となりました。多項式を $X^2+1$ で割った余りは1次式なので $a + bX$ ($a, b, \in ℝ$)となり、$X$ を $i$ と書き直して複素数 $a+bi$ と表記しました。
$i^2$ を多項式とみなすと $X^2$ であり、$X^2+1$ で割ると $X^2=(X^2+1)-1 \equiv -1$。つまり $i^2=-1$ です。
今回の一般化で一般に方程式 $X^2+u=0$ が $𝔽_p$ に解を持たないとき $𝔽_p[X]/(X^2+u)$ が $𝔽_p$ の2次拡大体となり、複素数的に表現すると $a+bi$ で $i^2=-u$ です。
BLS12-377曲線では $u=5$ が使われています。

## 四則演算
2次拡大体 $𝔽_p^2=𝔽_p[X]/(X^2+u)$ の四則演算を見てみましょう。
$x=a+bi, y=c+di \in 𝔽_p^2$ とします。

### 加減算
$$
x \pm y = (a\pm c)+(b\pm d)i
$$

です。普通の複素数と同じです。

### 乗算
$x y = (a+bi)(c+di) = (ac-bd u) + (ad+bc)i$ となります。
今までは $u=1$ だったので $ac-bd$ だったところが $ac - bd u$ に変わっています。

$𝔽_p$ の乗算回数を減らすためのKaratsuba法のテクニック $ad+bc=(a+b)(c+d)-ac-bd$ も使えます。

### 平方
$x^2=(a+bi)^2=(a^2-b^2 u) + 2ab i$ です。
$u=1$ のときは $a^2-b^2 u= a^2 - b^2 = (a+b)(a-b)$ だったので $𝔽_p$ の乗算回数を2回にできましたが $u \neq 1$ の場合はそのまま適用できません。

しかし次のようにすると効率化できます。

$$
a^2-b^2 u = (a-b)(a + b u) - a (u-1)b.
$$

$b$ から $2b$ と $(u-1)b$, $ub$ を求めます。$u$ は通常小さい定数なのでこれらの値は簡単に求められます。その後、この関係式を用いて $a^2-b^2 u$ を求めます。

### 逆数
$1/x$ を計算するには分子・分母に $a-bi$ を掛けます。

$$
1/x=\frac{1}{a+bi}=\frac{a-bi}{(a+bi)(a-bi)}=\frac{a-bi}{a^2+b^2 u}.
$$

複素数では $x$ のノルム（絶対値の2乗）を $\|x\|^2=a^2+b^2$ としていましたが、$\|x\|^2=a^2 + b^2 u$ と拡張しておくといろいろ便利です。

$u$ が1でなくなるといろいろな演算が少しずつ変わりましたが概ね同じような方法で計算できると分かりました。


## 平方根
$x \in 𝔽_p$ の $𝔽_p$ 内での平方根を求める関数を $(b, v) = \text{Fp::sqrRoot}(x)$ とします。平方根は $x$ によって存在するときと存在しないときがあるので存在すれば $b = \text{true}$ で $v=\sqrt{x}$（2個あるうちのどちらか）、存在しなければ $b=\text{false}$, $v=\text{None}$ とします。
この関数を使って $x \in 𝔽_p^2$ の平方根 $y$ を求めましょう。$\text{Fp::sqrRoot}(x)$ 自体の計算方法についてはまたいずれ。

$x=c+di$, $y=\sqrt{x}=a+bi$ とします。
$(a+bi)^2=(a^2-b^2 u) + 2ab i = c + di$ なので与えられた $c, d$ に対して $a^2-b^2 u= c$, $2ab = d$ となる $a, b$ を求めます。
見やすさのために $a^2=A$, $b^2=B$ とします。
$2ab=d$ の両辺を2乗すると $4 A B = d^2$。よって $B = d^2/(4 A)$ を $A - B u = c$ に代入して整理します。

$$
4 A^2 - 4 c A - d^2 u = 0.
$$

$A$ に関する2次方程式を解いて $A =(c \pm \sqrt{c^2+d^2 u})/2=(c \pm \sqrt{\|x\|^2})/2$。
従ってまず $c^2+d^2u = \|x\|^2$ の平方根を求めます。存在しなければ $y$ は存在しません。
存在すれば $A=(c + \sqrt{\|x\|^2})/2$ を計算します。
存在すれば $a=\sqrt{A}$ を求め、$b=d/2a$ から $b$ も求めます。
存在しなければ $A=(c - \sqrt{\|x\|^2})/2$ に対して同様のことをして平方根を求めます。
