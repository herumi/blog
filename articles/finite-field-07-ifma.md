---
title: "AVX-512 IFMAによる有限体の実装"
emoji: "🧮"
type: "tech"
topics: ["AVX512", "IFMA", "有限体"]
published: true
---
## 初めに

[有限体の実装](https://zenn.dev/herumi/articles/finite-field-01-add)では、主にx64向けの高速な実装方法を紹介してきました。
今回は、AVX-512 IFMA命令を使った実装方法を紹介します。

## AVX-512 IFMA
浮動小数点数におけるFMA (Fused Multiply-Add) とは浮動小数点数 $a$, $b$, $c$ に対して `std::fma(a, b, c)` = $a b + c$ を計算する命令・関数です。
AVX-512 IFMAはそれの整数版で、64ビット整数 $a$, $b$, $c$ に対して $a b + c$ を計算します。
ただし、$a$, $b$ は下位52ビットのみが利用され（$c$ は64ビット整数）、演算結果は最大104ビットです。更に、上位52ビットと下位52ビットを別々に求める必要があり、2個合わせて必要な結果を得られます。

*AVX-512 IFMA*
命令|$a$, $b$, $c$ に対する演算
-|-
vpmadd52luq|`uint52_t(a * b) + c`
vpmadd52huq|`uint52_t((a * b) >> 52) + c`

$a$, $b$ が52ビットという中途半端なビットになっているのは、double の仮数部が52ビットで、その回路部分を流用しているのかなと想像します。
また、$a b + c$ の上位52ビット, 下位52ビットでは無いことにも注意してください。
以下では `vpmadd52luq` を `mulL`, `vpmadd52huq` を `mulH` と表記します。

## 52ビット2進数
AVX-512 IFMAを使った384ビット進数の有限体の実装をするには今までのCPUのビットサイズ (32 or 64) とは異なるやり方が必要です。
52ビット整数を64ビット整数に保持すると12ビットの隙間ができます。384ビットの場合は `384 = 7 * 52 + 20` なので、64ビット整数を8個使い、7個には52ビットずつ、最後の1個に20ビットを格納します。各種演算は $2^{52}$ 進数（52ビット2進数）で行います。

*384ビット整数 $x$ を2進数表記 $[x_{383}:x_{382}:\cdots:x_0]$ して52ビット進数として格納する*
![](/images/52-bit-binary-number.png)

AVX-512 IFMAは512ビットSIMD幅なので、一つのレジスタに一つの384ビット整数を入れたくなりますが、そうすると並列処理が困難です。そのためZMMレジスタを8個用意し、8個の384ビット整数を分割して格納します。

*8個の384ビット整数 $x^{(0)}, \dots, x^{(7)}$ を8個のzmm0, ..., zmm7に格納する*
![](/images/52-bit-binary-number2.png)

## 加算
SIMDには加算時に1ビット増える値を扱うキャリーフラグ CF がありません。そのため [C++での実装](https://zenn.dev/herumi/articles/finite-field-01-add#c%2B%2B%E3%81%A7%E3%81%AE%E5%AE%9F%E8%A3%85) のように CF を自分で処理する必要があります。
ただ、52ビットを64ビット整数に格納しているので繰り上がりが消えることはありません。そのためC++での実装よりは簡単になります｡
$x$, $y$, $z$ の値をそれぞれ8個の64ビット整数の配列として表現し、$z=x+y$ を実現するC++による疑似コードは次の通りです。

```cpp
typedef uint64_t Unit;
const uint64_t g_mask = (uint64_t(1)<<52)-1;
const size_t N = 8;

Unit add(Unit z[N], const Unit x[N], const Unit y[N])
{
    Unit c = 0;
    for (size_t i = 0; i < N; i++) {
        z[i] = x[i] + y[i] + c;
        c = z[i] >> W;
        z[i] &= g_mask;
    }
    return Unit;
}
```

`x[i]` と `y[i]` を加算して結果が52ビットを越えていれば CF として次の位に加算します。

## 遅延 (lazy) 加算
「遅延加算」とはここだけの用語ですが、複数のaddをまとめてする場合は CF の処理を減らせます。
52ビット整数を $2^n$ 個加算すると結果は最大 $52+n$ ビットになります。$n \le 12$ の間は加算結果は64ビットを越えません。
したがって、CF を気にせずに要素ごとにまとめて加算してから、最後に各要素を52ビットずつに調整（ここでは、これを正規化と呼ぶことにする）してもよいです。
遅延加算は隣の要素と依存関係がないのでSIMD向きな計算なので効率よくできます。
次に紹介するMontgomery乗算の中では最大 N(=8) 回程度の加算なので、ほとんどの部分は遅延できます。

*疑似コード*
```cpp
// 繰り上がりを気にせずに加算する
void lazyAdd(Unit z[N], const Unit x1[N], const Unit x2[N], ...)
{
    for (size_t i = 0; i < N; i++) {
        z[i] = x1[i] + x2[i] + x3[i] + ...;
    }
}

// 52ビットを越えているx[i]を52ビットに収め直す
Unit normalize(Unit z[N], const Unit x[N])
{
    Unit c = 0;
    for (size_t i = 0; i < N; i++) {
        Unit t = x[i] + c;
        c = t >> W;
        z[i] = t & g_mask;
    }
    return c;
}
```

## SIMD版Montgomery乗算
通常版の詳細は[Montgomery乗算](https://zenn.dev/herumi/articles/finite-field-03-mul#montgomery%E4%B9%97%E7%AE%97)を参照してください。
配列の値に対して `Unit` 倍する `mulUnit` と別の配列の値に `mulUnit` の結果を加算する `mulUnitAdd` を用いて実装します。
ここで `Vec` は `uint64_t` が8個のSIMDレジスタを表す型、vpsrlqは左シフト、`vrp` $=-p^{-1} \pmod{2^{52}}$ です。
関数の最後の `uvselect(z, c, x, y)` は `z = c ? x : y` のSIMD版関数です。
`mulUnit` や `mulUnitAdd` は遅延版としますが、`q` を計算するときは正規化されていないといけないのでループの中で `t` の最下位だけ正規化しています。

*疑似コード*
```cpp
const size_t N = 8;
const size_t W = 52;
void uvmul(Vec *z, const Vec *x, const Vec *y)
{
    Vec t[N*2], q;
    // Montgomery乗算
    mulUnit(t, x, y[0]); // t = x * y[0]
    q = vmulL(t[0], vrp); // q = mask(t * vrp)
    t[N] = vadd(t[N], mulUnitAdd(t, vpN, q));
    for (size_t i = 1; i < N; i++) {
        t[N+i] = mulUnitAdd(t+i, x, y[i]); // t += x * y[i]
        t[i] = vadd(t[i], vpsrlq(t[i-1], W)); // tの最下位だけ正規化
        q = vmulL(t[i], vrp); // q = mask(t * vrp)
        t[N+i] = vadd(t[N+i], mulUnitAdd(t+i, vpN, q)); // t += p * q
    }
    // normalize
    for (size_t i = N; i < N*2; i++) {
        t[i] = vadd(t[i], vpsrlq(t[i-1], W));
        t[i-1] = vand(t[i-1], vmask);
    }
    // t >= p なら t -= p とする
    Vmask c = vrawSub(z, t+N, vpN);
    uvselect(z, c, t+N, z);
}
```

## SIMD版 `mulUnit` と `mulUnitAdd`
前節の中に出てくる `mulUnit` と `mulUnitAdd` ですが、次のように実装できます。

```cpp
inline void vmulUnit(Vec *z, const Vec *x, const Vec& y)
{
    Vec H;
    z[0] = vmulL(x[0], y);
    H = vmulH(x[0], y);
    for (size_t i = 1; i < N; i++) {
        z[i] = vmulL(x[i], y, H);
        H = vmulH(x[i], y);
    }
    z[N] = H;
}
```

```cpp
inline Vec vmulUnitAdd(Vec *z, const Vec *x, const Vec& y)
{
    Vec v = x[0];
    z[0] = vmulL(v, y, z[0]);
    Vec H = vmulH(v, y, z[1]);
    for (size_t i = 1; i < N-1; i++) {
        v = x[i];
        z[i] = vmulL(v, y, H);
        H = vmulH(v, y, z[i+1]);
    }
    v = x[N-1];
    z[N-1] = vmulL(v, y, H);
    H = vmulH(v, y);
    return H;
}
```
`mulUnitAdd` では元々の52ビットを越えてるかもしれない `z[i]` に対して `x[i] * y` の上位52ビット、下位52ビットを加算するので `mulL` と `mulH` を実行するだけでよいですね。

## ベンチマーク
通常版の加減乗算とSIMD版を比較しました。Xeon w9-3495X で `/sys/devices/system/cpu/intel_pstate/no_turbo` は 0 でデフォルトから設定いじらずの簡易的なベンチマークです。単位はclock cycle。

対象|add|sub|mul
-|-|-|-
Fp|4.42|6.08|52.03
SIMD|20.67|16.64|118.72
Fp/(SIMD/8)|1.7|2.9|3.5

SIMD版は1回の演算で8個分のFpを処理するのでFp 1個あたりの速度比 `Fp/(SIMD/8)` を求めると mul でSIMD版が3.5倍ぐらい速くなりました。4倍ぐらいになったらいいなと期待しましたが、Fpの方はこれ以上速くするのはほぼ無理なので、まあまずまずなところです。
大量の配列に対するSIMD演算ではなく、8個単位と小さく依存関係が大きい関数なので仕方ないところでしょうか。
