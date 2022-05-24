---
title: "多倍長整数の実装3（intrinsic）"
emoji: "🧮"
type: "tech"
topics: ["int","add", "cpp", "intrinsic"]
published: true
---
## 初めに

前々回[多倍長整数の実装1（C/C++）](https://zenn.dev/herumi/articles/bitint-01-cpp)、前回[多倍長整数の実装2（Xbyak）](https://zenn.dev/herumi/articles/bitint-02-xbyak)でC++やXbyakによる実装をしました。
今回からXbyakに頼らずに、いくつかの方法を試します。まずはコンパイラのintrinsic関数を使ってみましょう。

## _addcarry_u64

[_addcarry_u64](https://www.intel.com/content/www/us/en/develop/documentation/cpp-compiler-developer-guide-and-reference/top/compiler-reference/intrinsics/intrinsics-for-later-gen-core-proc-instruct-exts/intrinsics-for-multi-precision-arithmetic/addcarry-u32-addcarry-u64.html)はx64命令のadcに相当するコンパイラの組み込み関数です。
利用するにはWindowsなら`intrin.h`, Linuxなら`x86intrin.h`をインクルードします。

```cpp
#ifdef _WIN32
#include <intrin.h>
#else
#include <x86intrin.h>
#endif
```

プロトタイプ宣言は

```cpp
unsigned char _addcarry_u64(unsigned char c_in, unsigned __int64 src1, unsigned __int64 src2, unsigned __int64 *sum_out);
```
で
```cpp
*sum_out = src1 + src + (c_in != 0 ? 1 : 0);
```
で戻り値は結果が64bitを超えた or 越えないに応じて1 or 0を返します。

`__int64`がどんな型のaliasかはコンパイラによりますが、"unsigned long long"にキャストすればよいです。

## _addcarry_u64を使った実装

キャリーは最初は0なのでループに関するテンプレート関数を作ると次のようになります。

```cpp
template<size_t N>
static inline Unit addT(Unit *z, const Unit *x, const Unit *y)
{
  uint8_t c = 0;

  for (size_t i = 0; i < N; i++) {
    c = _addcarry_u64(c, x[i], y[i], (unsigned long long*)&z[i]);
  }
  return c;
}
```

clang-12のコンパイル結果を見ましょう。

```cpp
extern "C" Unit add4(Unit *z, const Unit *x, const Unit *y)
{
  return addT<4>(z, x, y);
}

# clang++-12 -S -O2 -DNDEBUG -masm=intel add-intrin.cpp
```

```
add4:
    mov rax, qword ptr [rsi]
    add rax, qword ptr [rdx]
    mov qword ptr [rdi], rax
    mov rax, qword ptr [rsi + 8]
    adc rax, qword ptr [rdx + 8]
    mov qword ptr [rdi + 8], rax
    mov rax, qword ptr [rsi + 16]
    adc rax, qword ptr [rdx + 16]
    mov qword ptr [rdi + 16], rax
    mov rax, qword ptr [rsi + 24]
    adc rax, qword ptr [rdx + 24]
    setb    cl
    mov qword ptr [rdi + 24], rax
    movzx   eax, cl
    ret
```

なんと前回のXbyakによる実装とほぼ同じ（少しだけ命令の順序が異なる）です。
わざわざアセンブラを使わなくてよいので優秀ですね。

## gccとVC(Visual Studio)の結果

前節で終わればよいのですが、残念ながらそうはうまくいきません。

g++-10の結果を見てみます。

```
add4:
    mov r8, rdi
    xor eax, eax
    mov rdi, rsi
    xor ecx, ecx
    mov rsi, rdx
.L2:
    mov rdx, QWORD PTR [rdi+rax]
    add cl, -1
    adc rdx, QWORD PTR [rsi+rax]
    mov QWORD PTR [r8+rax], rdx
    setc    cl
    add rax, 8
    cmp rax, 32
    jne .L2
    movzx   eax, cl
    ret
```

ループが展開されていないのでがっかりです。次にVCも見てみましょう。

```
  xor  r9b, r9b
  sub  rcx, r8
  sub  rdx, r8
  mov  r10d, 4
$lp:
  mov  rax, QWORD PTR [rdx+r8]
  add  r9b, -1
  adc  rax, QWORD PTR [r8]
  mov  QWORD PTR [rcx+r8], rax
  lea  r8, QWORD PTR [r8+8]
  setb r9b
  sub  r10, 1
  jne  SHORT $lp
  movzx eax, r9b
  ret
```

こちらも残念ながらループ展開されませんでした。
VCは昔からintrinsic関数を含むループはアンロールしないという挙動があるので仕方ありません。

## gccの場合の強制アンロール

gccにはいくつかループを強制アンロールさせる方法があります。

1. -O2に加えて-funroll-loopsオプションをつける。ループアンロールして欲しくないところもアンロールされて無駄にコードが大きくなってしまう可能性があります。
2. pragmaで指定する。こちらは関数単位で指示できるので小回りが利きます。

```cpp
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC push_options
#pragma GCC optimize ("unroll-loops")
#endif

// アンロールさせたい部分

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC pop_options
#endif
```

`#pragma GCC optimize ("unroll-loops")`でループアンロールを指示します。
このプラグマはclangにはないので、

```cpp
#if defined(__GNUC__) && !defined(__clang__)

...

#endif
```
でgccのみをターゲットにします。

コンパイラオプション制御はグローバルに影響を与えるのでターゲット箇所を

```cpp
#pragma GCC push_options

...

#pragma GCC pop_options
```

で囲むとこのコードをヘッダファイルに記述しても他に影響を与えなくなるのでよいです。


結果は次のようになりました。

```
    mov rax, QWORD PTR [rdx]
    add rax, QWORD PTR [rsi]
    setc    cl
    mov QWORD PTR [rdi], rax
    mov rax, QWORD PTR 8[rsi]
    add cl, -1
    adc rax, QWORD PTR 8[rdx]
    setc    cl
    mov QWORD PTR 8[rdi], rax
    mov rax, QWORD PTR 16[rsi]
    add cl, -1
    adc rax, QWORD PTR 16[rdx]
    mov QWORD PTR 16[rdi], rax
    setc    cl
    mov rax, QWORD PTR 24[rsi]
    mov rdx, QWORD PTR 24[rdx]
    add cl, -1
    adc rax, rdx
    mov QWORD PTR 24[rdi], rax
    setc    al
    movzx   eax, al
    ret
```

ループアンロールされました。しかし演算結果をsetcでclに設定し、その値をいちいち足しています。
adcは使っているのですが、_addcarry_u64の仕様`(c_in != 0 ? 1 : 0)`の部分をわざわざ処理しているようです。
少々残念ですね。

## C++03でも使えるtemplateによる強制アンロール

VCにはアンロールを強制するプラグマやオプションがありません。
こういうときの定番テクニックとしてtemplateによる再帰があります。

```cpp
template<size_t N, size_t I = N>
struct UnrollT {
  static inline Unit call(uint8_t c, Unit *z, const Unit *x, const Unit *y) {
    c = _addcarry_u64(c, x[N - I], y[N - I], (unsigned long long *)&z[N - I]);
    return UnrollT<N, I - 1>::call(c, z, x, y);
  }
};

template<size_t N>
struct UnrollT<N, 0> {
  static inline Unit call(uint8_t c, Unit *, const Unit *, const Unit *) {
    return c;
  }
};

extern "C" Unit add4_3(Unit *z, const Unit *x, const Unit *y)
{
  return UnrollT<4>::call(0, z, x, y);
}
```

- まず`struct UnrollT`のtemplateクラスを作ります。templateパラメータに引数を設定します。
- 処理したい関数をcallの中に記述します。
  - アンロールしたい処理を書いたら、残りを再帰で呼び出します。
  - `return UnrollT<N, I - 1>::call(c, z, x, y);`
- このままだとループが終わらないので終了条件をtemplateクラスの特殊化で記述します。
  - `template<size_t N> struct UnrollT<N, 0> {...`
- 作ったテンプレートクラスを実体化させて呼び出します。
  - `return UnrollT<4>::call(0, z, x, y);`

結果を見ましょう。

```
  mov   rax, QWORD PTR [rdx]
  add   rax, QWORD PTR [r8]
  mov   QWORD PTR [rcx], rax
  mov   rax, QWORD PTR [rdx+8]
  adc   rax, QWORD PTR [r8+8]
  mov   QWORD PTR [rcx+8], rax
  mov   rax, QWORD PTR [rdx+16]
  adc   rax, QWORD PTR [r8+16]
  mov   QWORD PTR [rcx+16], rax
  mov   rdx, QWORD PTR [rdx+24]
  adc   rdx, QWORD PTR [r8+24]
  mov   QWORD PTR [rcx+24], rdx
  setb  al
  movzx eax, al
```
VCでもclangと同じコードが生成されました。よいですね。
gccはunrollオプションや#pragmaを使わなくてもループアンロールされましたが、フラグの扱いが微妙なのは変わりませんでした。

## C++17のconstexpr

前出のtemplateテクニックはC++03でも利用できる汎用性の高いものでしたが、C++17で導入された[constexpr if](https://cpprefjp.github.io/lang/cpp17/if_constexpr.html)を使うともう少しスマートに記述できます。

```cpp
template<size_t N, size_t I = 0>
Unit addT2(Unit *z, const Unit *x, const Unit *y, uint8_t c = 0)
{
  if constexpr (I < N) {
    c = _addcarry_u64(c, x[I], y[I], (unsigned long long *)&z[I]);
    return addT2<N, I + 1>(z, x, y, c);
  } else {
    return c;
  }
}

extern "C" Unit add4_2(Unit *z, const Unit *x, const Unit *y)
{
  return addT2<4>(z, x, y);
}
```
`if constexpr 条件`は条件が偽のときはその中の文がコンパイルされず再帰が止まります。そのため特殊化を作らなくてすむのです。出力結果は前述と同じでした。

### まとめ

- `_addcarry_u64`を使うとclangではXbyakと同等のコードを生成できる。
- VCでもtemplateテクニックを使ってループアンロールを強制させると同等のコードを生成できる。
- gccは今のところCFについての最適化が不十分である。
- 残念ながら`_addcarry_u64`はx64環境でしか使えない。

次回はclang独自の構文を使ってみます。
