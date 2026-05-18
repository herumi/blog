---
title: "ML-KEMのNTTにおける乗算の最適化手法詳解2"
emoji: "📖"
type: "tech"
topics: ["mlkem", "ntt", "montgomery", "simd", "aarch64"]
published: true
---
## 初めに
[前回](https://zenn.dev/herumi/articles/ml-kem-montgomery1)ではML-KEMのNTTにおけるMontgomery乗算のAVX向け最適化手法を解説しました。
今回はAArch64用SIMDでの最適化手法を解説します。

## Barrett reductionの改善
Barrett reductionは定数除算を逆数の近似値の乗算とシフトに置き換える手法です。
$x$ に対する $p$ で割った余りを求めるために商 $q:=⌊x/p⌋$ の近似値を求めて余り $r:=x-p q$ を計算します。
ここで整数 $x$ は $a$, $b ∈𝔽_p$ の積 $x=ab$ を想定しているので普通にやると途中で32ビットの整数値に関する演算が必要になります。

しかし、前回と同じく片方の値が定数であることを用いて $x/p=ab/p=a(b/p)$ と変形し、更に16ビット単位で処理できる手法が提案されています([V. Lyubashevsky and G. Seiler, "NTTRU: Truly Fast NTRU Using NTT", 2019](https://tches.iacr.org/index.php/TCHES/article/view/8293))。

実際に使われている方法をSIMDの1プレーンで説明しましょう。

## AVXでの実装
```cpp
// z = round((y * (R/2)) / p)
// return (x * y) % p
int modp1(int x, int y, int z) {
	int t1 = vpmulhrsw(x, z);
	int t2 = pmullw(t1, p);
	int t3 = pmullw(x, y);
	int r = psubw(t3, t2);
	return r;
}
```
まず事前計算として $y$ に対して $z=⌊y * (R/2)/p⌋$ を求めておきます。

`vpmulhrsw(x, z)` は $x$ と $z$ の符号付き16ビット整数乗算の上位16ビットを求める `vpmulhs` と似ているのですがのですが、そのときに四捨五入する（ $2^{14}$ を足して15ビット右シフト）のが特徴です。

```cpp
int vpmulhrsw(int x, int y) {
	assert((-32768 < x && x < 32768) && (-32768 < y && y < 32768));
	int prod = (x * y) + (R/4);
	return prod >> 15;
}
```

これでうまくいく理由を確認しましょう。
$z$ と $t_1$ の作り方から、$z=y(R/2)/p+ε_1$, $t_1=xz/(R/2)+ε_2$, $|ε_i|≦1/2$ と表せます。
すると

$$t_1=x(y(R/2)/p+ε_1)/(R/2)+ε_2=xy/p+(x/(R/2))ε_1+ε_2.$$

$|x/(R/2)|<1$ なので $|t_1-xy/p|≦ε_1+ε_2≦1/2+1/2=1$.
よって $|t_1 p - x y| < p < (R/2)$.
したがって $t_1 p$ と $x y$ の上位16ビットは無視してよく、下位16ビットの $t_2$ と $t_3$ の引き算 $r$ が $(x y) \bmod{p}$ となります。

## AArch64での実装
AArch64のSIMD命令にはAVXの `vpmulhrsw` に相当する命令として `sqrdmulh` があります（ $x=y=-32768$ のときの挙動は異なるが今回はそのような値は入力されない）。
また AVXと異なり整数に対する積和演算を搭載するため、`mls(t2, t1, p) = t2 - t1 * p` を用いるとAVXより1命令減らせて

```cpp
int modp1_aarch64(int x, int y, int z) {
    int t1 = sqrdmulh(x, z);
    int t2 = pmullw(x, y);
    return mls(t2, t1, p);
}
```

```cpp
int sqrdmulh(int x, int y) {
    return vpmulhrsw(x, y);
}

int mls(int acc, int x, int y) {
    return psubw(acc, pmullw(x, y));
}
```

とできます。AVXにも整数の積和演算命令があったらよかったですね。

## 実装実験
[mlkem-native](https://github.com/pq-code-package/mlkem-native)の実装は、現在AArch64は今回紹介した方法、AVXは前回紹介した方法を採用しています。
AVXでも今回の実装をするとワンチャン速くなるかもと思って[実装](https://github.com/herumi/mlkem-native/tree/nttru)してみましたが、変わりませんでした。
命令数とレイテンシ・スループットが同じなので変わらないのは当然なのですが、アルゴリズムがAArch64と同じになるのでプログラムの正しさの検証などはやりやすくなるかもしれません。
