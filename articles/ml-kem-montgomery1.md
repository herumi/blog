---
title: "ML-KEMのNTTにおけるMontgomery乗算の最適化手法詳解1"
emoji: "📖"
type: "tech"
topics: ["mlkem", "ntt", "montgomery", "avx"]
published: true
---
## 初めに
ML-KEMは耐量子計算機暗号PQC(Post-Quantum Cryptography)の一つで、2024年NICTで標準化され、既にChrome/Edge/Firefox/Safariなどの主要ブラウザで利用できます。
ML-KEMは多項式の乗算を多用するのですが、その高速化のためにNTT(Number Theoretic Transform)というFFTに類似の手法が用いられます。
[mlkem-native](https://github.com/pq-code-package/mlkem-native)はML-KEMの実装の一つで、NTTのAVX2などによる最適化実装を提供しています。
ここではそこで用いられている手法を解説します。

## Montgomery乗算の基本
ML-KEMでは素数 $p=3329$ の有限体 $𝔽_p=\{0, 1, \dots, p-1\}$ における乗算 $a b \bmod{p}$ ( $a, b ∈ 𝔽_p$ ) が頻出します。

この乗算と剰余算を高速化するためにMontgomery乗算が利用されます。
Montgomery乗算は、以前に[有限体の実装3（Montgomery乗算の紹介）](https://zenn.dev/herumi/articles/finite-field-03-mul)で解説しましたが、後述の記号や符号と合わせるために再度簡単に説明します。

### 記号の準備
ここだけの記号ですが、$x ≡ y \bmod{p}$ を短く $x ≡_p y$ と書くことにします。

$R=2^{16}$ とすると $p$ と $R$ は互いに素なのでEuclidの互除法を用いて $S R + q p = 1$ となる整数 $S$, $q$ が求まります。
このとき $S≡_p R^{-1}$, $q≡_R p^{-1}$ です。

**定理**: 整数 $x$ に対して $m:=(x q) \bmod{R}$, $y:=(x-m p)/R$ と定義すると $y≡_p x R^{-1}$ が成り立つ。

証明: $x-m p ≡_R x -(x q) p=x-x(qp) ≡_R x - x ≡_R 0$ より $x - m p$ は $R$ の倍数なので $y=(x - m p)/R$ は整数。そして$x - m p ≡_p x$ で $p$ と $R$ は互いに素なので両辺に $R^{-1}$ を掛けて $y=(x - m p)/R ≡_p x R^{-1}$.

ここで $MR(x):=x R^{-1} \bmod{p}$ をMontgomery reduction, 
$Mont(x, y):=MR(x y)$ と定義します。
すると $Mont(x R, y R)≡_p (x R) (y R) R^{-1}≡_p  x y R$ となり、これをMontgomery乗算を呼びます。

更に補助関数として

- $toMont(x):=Mont(x, R^2)≡_p x R^2 R^{-1}≡_p x R$
- $fromMont(xR):=Mont(xR, 1)≡_p xR R^{-1} ≡_p x$

も定義しておきます。

## 素朴なMontgomery reductionの実装
ML-KEMでのパラメータは $p=3329$ と16ビット整数に収まります。したがってSIMD実装を想定すると $R=2^{16}$ とし $q=62209≡_p -3327$ となります。


```cpp
const int p = 3329;
const int shift = 16;
const int R = 1<< shift;
const int p_inv = -3327; // p_inv ≡_R p^{-1}
const int R_inv = 169; // R_inv ≡_p R^{-1}

int MR(int x) { // MR(x) = x R_inv
  int t0 = (x * int64_t(p_inv)) & (R-1);
  int t1 = t0 * p;
  int r = (x - t1) >> shift;
  // clipping in [0, p-1]
  if (r >= p) r -= p;
  if (r < 0) r += p;
  return r;
}
```

SIMDで考慮すべきなのはレーンをまたがない演算を極力避けるということです。ここでレーンとは並列に実行される一つの処理単位（16ビット演算なら16ビット）のことを指します。
たとえば最初の

```cpp
  int t0 = (x * int64_t(p_inv)) & (R-1);
```

は乗算結果の下位16ビットを取るためにAVXのpmullw命令を使えます。

```cpp
int maskL(int x) { return x & (R-1); }

// x yの下位16ビットを返す
int pmullw(int x, int y) {
    return maskL(x * y);
}

// x yの上位16ビットを返す
int pmulhw(int x, int y) {
    return (x * y) >> shift;
}
```

次の `t1 = t0 * p` は乗算結果が16ビットに収まりませんが、`pmullw` と `pmulhw` を組み合わせて16ビットレーンのまま処理できます。
ただし、その次の `x - t1` は32ビット減算が必要です。一度16ビットレーンを32ビットレーンに拡張してから減算し、再度16ビットレーンに戻すか、16ビット減算を用いて32ビット減算をシミュレーションする必要があります。
SIMDはキャリーフラグをもたないため、[多倍長整数の実装1（C/C++）](https://zenn.dev/herumi/articles/bitint-01-cpp#%E5%8A%A0%E7%AE%97)で解説したような複雑な計算になります。

そこで16ビットレーンに特化したMontgomery乗算が開発されました。

## 16ビットレーンに特化したMontgomery乗算
16ビットレーンに特化したMontgomery乗算は、以下のように定義されます。
NTTの演算では $x$ と $y$ のMontgomery乗算のうち、片方は係数で固定です。そのため事前計算してテーブルに保持できます。コードの `z = pmullw(y, p_inv)` はそのようにして事前計算した値を利用します。

```cpp
// z = pmullw(y, p_inv)
// return mont(x, y)
int mont1(int x, int y, int z) {
    int t1 = pmullw(x, z);
    int t2 = pmulhw(t1, p);
    int t3 = pmulhw(x, y);
    int r = psubw(t3, t2);
    return r;
}
```

ここで `psubw` は16ビット減算を行う関数で、

```cpp
int psubw(int x, int y) {
    return short(x - y);
}
```

です。なぜこれで計算できるのか確認しましょう。
事前計算により $z ≡_R y p^{-1}$ なので $t_1 ≡_R x (y p^{-1})$.
よって $t_1 p≡_R x y$ です。
つまり $x y - t_1 p$ は $R$ の倍数なので上位16ビットだけを計算しても正しい結果が得られます。
したがって

$$r ≡_R t_3 - t_2 =  ⌊x y/R⌋ -  ⌊t_1 p/R⌋ =  ⌊(x y - t_1 p)/R⌋=(x y - t_1 p)/R.$$

そして

$$|x y - t_1 p|/R ≦ |x||y|/R+|t_1|p/R < (R/2)(R/2)/R + (R/2)p/R=R/4+p/2 < R/2$$

なので $r = (x y - t_1 p)/R$.

最後に $t_1 p≡_p 0$ より$x y - t_1 p ≡_p x y$ なので $r ≡_p (x y - t_1 p)/R ≡_p x y R^{-1}$ となります。

この手法をとることで、Montgomery乗算をレーンをまたがずにわずか乗算3命令と減算1命令で実現できます。
すばらしいテクニックですね。

一般にMontgomery乗算は最後に結果の絶対値が $p/2$ に収まるように調整する必要があります。
しかし、ML-KEMのNTTではその調整をスキップして8回Montgomery乗算を行い、必要に応じて最後に調整しています。

これは最初の入力値が $p/2$ に収まることと片方の定数の値が小さいことを組み合わせて最後まで計算しても16ビットに収まることを利用しています。そのあたりの話はまたいずれ。

実際のMontgomery乗算のコードは[dev/x86_64/src/fq.inc](https://github.com/pq-code-package/mlkem-native/blob/57d18f40a341fd65e7ba6a6405b95e215572fafc/dev/x86_64/src/fq.inc#L35-L42)や[dev/x86_64/src/ntt_avx2_asm.S](https://github.com/pq-code-package/mlkem-native/blob/57d18f40a341fd65e7ba6a6405b95e215572fafc/dev/x86_64/src/ntt_avx2_asm.S)などにあります。

```
/* Montgomery multiplication between b and ah,
 * with Montgomery twist of ah in al. */
.macro fqmulprecomp al, ah, b, x=12
vpmullw %ymm\al,%ymm\b,%ymm\x
vpmulhw %ymm\ah,%ymm\b,%ymm\b
vpmulhw %ymm0,%ymm\x,%ymm\x
vpsubw %ymm\x,%ymm\b,%ymm\b
.endm
```

次回はAArch64で使われているテクニックを解説します。
