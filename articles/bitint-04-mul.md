---
title: "多倍長整数の実装4（乗算の基礎）"
emoji: "🧮"
type: "tech"
topics: ["int","mul", "cpp", "intrinsic"]
published: true
---
## 初めに

今回はN桁x1桁の固定多倍長整数の乗算を実装します。ここで1桁はUnit（32bit or 64bit）です。
記事全体の一覧は[多倍長整数の実装1（C/C++）](https://zenn.dev/herumi/articles/bitint-01-cpp)参照。

## 筆算

まず基本に戻りましょう。10進数3桁x1桁の乗算を考えます。

```
  2 4 7
x     9
-------
    6 3
  3 6
1 8
-------
2 2 2 3
```

7 x 9 = 63, 4 x 9 = 36を計算したあと36 + 6を計算します。このとき加算のときのように繰り上がりが発生するので、それを順次処理していけばよいです。
「36 + 6」は2桁+1桁の足し算なので、Unitの2倍のbit数の整数型の加算をしないといけないことに注意してください。

## Unit = uint32_tのとき

N桁x1桁の結果はN+1桁になるので、N桁出力したあと最上位の分はUnitを返す関数を作ることにします。
Unitが32bit整数のときは次のようになります。

```cpp
// ret:z[N] = x[N] x y
template<size_t N>
Unit mulUnit(Unit *z, const Unit *x, Unit y)
{
  uint64_t H = 0;
  for (size_t i = 0; i < N; i++) {
    uint64_t v = uint64_t(x[i]) * y;
    v += H;
    z[i] = uint32_t(v);
    H = v >> 32;
  }
  return uint32_t(H);
}
```
ところで`v += H`のところで結果が64bitを越えることはないのでしょうか。
vとHに何も制約がない場合は当然64bitを越えることはあります。しかしここでは大丈夫です。
なぜなら、vは`v = x * y + H`の結果であり、Mを`1 << 32`（64bitなら`1 << 64`）とするとx, y, Hは全て最大値が`M - 1`なのでvの最大値は`(M - 1) * (M - 1) + (M - 1) = M * (M - 1) < M^2`と上から抑えられるからです。

## Unit = uint64_tのとき（Visual Studioの場合）

64bit整数同士の乗算は結果が128bitです。
Visual Studio (VC)には`_umul128`という結果をuint64_t 2個のペアで返すintrinsic関数があるのでそれを使います。

gccやclangにはそのような関数はありませんが、代わりに`__attribute__((mode(TI)))`という128bit整数の型が存在します。typedefしておきましょう。

```cpp
typedef __attribute__((mode(TI))) unsigned int uint128_t;
```

「TIってなんだよ」と思われたかもしれません。これは[gccのマニュアル](https://gcc.gnu.org/onlinedocs/gccint/Machine-Modes.html)によると「Tetra Integer(?)」で16byte(=128bit)整数を表すようです。
その下に、OI(32byte)やXI(64byte)もあったので試してみましたが、

```
error: unable to emulate 'OI
```
というエラーになりました。残念。

閑話休題。両者を統一的に扱うためにラップ関数を用意します。

```cpp
inline uint64_t mulUnit1(uint64_t *pH, uint64_t x, uint64_t y)
{
#ifdef MCL_DEFINED_UINT128_T
  uint128_t t = uint128_t(x) * y;
  *pH = uint64_t(t >> 64);
  return uint64_t(t);
#else
  return _umul128(x, y, pH);
#endif
}
```

これを使うと次のように書けます。

```cpp
// (A)
template<size_t N>
Unit mulUnitT(Unit *z, const Unit *x, Unit y)
{
  Unit H = 0;
  for (size_t i = 0; i < N; i++) {
    Unit t = H;
    Unit L = mulUnit1(&H, x[i], y);
    z[i] = t + L;
    if (z[i] < t) {
      H++;
    }
  }
  return H; // z[n]
}
```
`z[i] < t`は加算のときと同じくキャリーフラグCFをチェックしています。

## Unit = uint64_tのとき（gcc/clangの場合）

uint128_tを使える環境なら128bit整数同士の加算もできるので、上記よりももう少し簡潔に次のように書けます。

```cpp
// (B)
template<size_t N>
Unit mulUnitT(Unit *z, const Unit *x, Unit y)
{
  uint64_t H = 0;
  for (size_t i = 0; i < N; i++) {
    uint128_t v = uint128_t(x[i]) * y;
    v += H;
    z[i] = uint64_t(v);
    H = uint64_t(v >> 64);
  }
  return uint64_t(H); // z[n]
}
```
(A)よりも(B)の方が最適化がかかりやすい可能性があります。
試したところclang-12では元々高度な最適化をするので変わりませんでしたが、gcc-9.4では(B)が若干よいコードを生成しました。

```nasm
// gccで(A)をコンパイルしたもの
.L50:
    movq    %r9, %rax
    mulq    (%rsi,%rcx,8)
    addq    %r8, %rax
    setc    %r8b
    movq    %rax, (%rdi,%rcx,8)
    movzbl  %r8b, %r8d
    cmpq    $1, %r8
    sbbq    $-1, %rdx
    addq    $1, %rcx
    movq    %rdx, %r8
    cmpq    $4, %rcx
    jne .L50
```

```nasm
// gccで(B)をコンパイルしたもの
.L46:
    movq    %r11, %rax
    mulq    (%rsi,%rcx,8)
    xorl    %r9d, %r9d
    addq    %r10, %rax
    adcq    %r9, %rdx
    movq    %rax, (%rdi,%rcx,8)
    addq    $1, %rcx
    movq    %rdx, %r10
    cmpq    $4, %rcx
    jne .L46
```
加算のときと同様-O2だけではループアンロールされませんでした。
それに対してclangは次のコードを生成しました。なかなか頑張ってますね。
mulが4回、addが3回、adcが3回、setbが1回です。

```nasm
    movq    %rdx, %r10
    movq    %rdx, %rax
    mulq    (%rsi)
    movq    %rdx, %r8
    movq    %rax, (%rdi)
    movq    %r10, %rax
    mulq    8(%rsi)
    movq    %rdx, %r9
    movq    %rax, %rcx
    addq    %r8, %rax
    movq    %rax, 8(%rdi)
    movq    %r10, %rax
    mulq    16(%rsi)
    movq    %rdx, %r11
    addq    %r8, %rcx
    adcq    %r9, %rax
    setb    %cl
    movq    %rax, 16(%rdi)
    movq    %r10, %rax
    mulq    24(%rsi)
    addb    $255, %cl
    adcq    %r11, %rax
    adcq    $0, %rdx
    movq    %rax, 24(%rdi)
    movq    %rdx, %rax
    retq
```

次回はもう少しよい方法が無いか、その辺りを考察してみましょう。
