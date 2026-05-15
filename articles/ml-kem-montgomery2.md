---
title: "ML-KEMのNTTにおける乗算の最適化手法詳解2"
emoji: "📖"
type: "tech"
topics: ["mlkem", "ntt", "montgomery", "simd", "aarch64"]
published: false
---
## 初めに
[前回](https://zenn.dev/herumi/articles/ml-kem-montgomery1)ではML-KEMのNTTにおけるMontgomery乗算のAVX向け最適化手法を解説しました。
今回はAArch64用SIMDでの最適化手法を解説します。

## Barrett reductionの改善
Barrett reductionは定数除算を逆数の近似値の乗算とシフトに置き換える手法です。
$x$ に対する $p$ で割った余りを求めるために商 $q:=⌊x/p⌋$ の近似値を求めて余り $r:=x-p q$ を計算します。
ここで整数 $x$ は $a$, $b ∈𝔽_p$ の積 $x=ab$ を想定しているので普通にやると途中で32ビットの整数値に関する演算が必要になります。

しかし、前回と同じく片方の値が定数であることを用いて $x/p=ab/p=a(b/p)$ として処理を改善する手法が提案されています。

実際に使われている方法をSIMDの1プレーンで説明しましょう。

## SIMDでの実装
```cpp
// z = (y * (R/2)) / p
// return (x * y) % p
int modp1(int x, int y, int z) {
	int t1 = vpmulhrsw(x, z);
	int t2 = pmullw(t1, p);
	int t3 = pmullw(x, y);
	int r = psubw(t3, t2);
	return r;
}
```
