---
title: "多倍長整数の実装1（C/C++）"
emoji: "🧮"
type: "tech"
topics: ["int","add", "cpp"]
published: true
---
## 初めに

これからしばらくC/C++による多倍長整数演算の実装を解説します。
主なターゲットはx64ですがAarch64やWebAssembly（Wasm）なども考慮します。
利用目的が楕円曲線やペアリングを使った暗号なので、bit長は256～512bitの固定長とします。
それよりも長いbit長や可変長なものは扱いません。

## 多倍長整数の表現

現在のC++(20)では`uint64_t`より大きい整数の型はありません。
そこでより大きい整数は自分で実装する必要があります。

`Unit`をCPUの汎用レジスタのサイズ（64bit or 32bit）に合わせて`uint64_t`か`uint32_t`とします。
いくつか記号を準備します。

- `UnitBitSize`を`sizeof(Unit) * 8`（= 64 or 32）
- `M = 1 << UnitBitSize`

符号無し多倍長整数を`Unit`の配列で表現します。
たとえば64bit CPUなら`Unit = uint64_t`なので256bit整数は256 / 64 = 4個の配列です。

```
Unit x[4];
```

ここでは配列の小さい添え字が下位の値を表現すると決めます。
たとえば`x[4] = { 1, 2, 3, 4 };`とすると、この`x`は

```
x = (4 << 192) | (3 << 128) | (2 << 64) | (1 << 0) = 0x4000000000000000300000000000000020000000000000001
```
という値を意味します。
一般に`[a:b:c:d]`と書くと、断りがない限り`a`, `b`, `c`, `d`はそれぞれ`Unit`のサイズで
全体で`(a << (UnitBitSize * 3)) | (b << (UnitBitSize * 2)) | (c << UnitBitSize) | d`を意味することにします。

## 加算

多倍長整数同士の加算から始めます。

たとえば10進数の25+67を筆算で計算すると、

```
  2 5
+ 6 7
-----
  1 2
  8
-----
  9 2
```

となります。1桁目の`5 + 7`は1繰り上がって2桁目の`2 + 6`に`+1`します。

`Unit`を使う場合は64（32）bit進数となりますが、計算結果が`M`（= `1 << UnitBitSize`）を越えると`M`で割ったあまりになるので繰り上がりが無くなります。
繰り上がりがあったかどうかは、加算後の結果が加算前の値より小さくなっているかどうかで判定します。

```cpp
// 繰り上がりがあればtrue
bool hasCarry(Unit x, Unit y)
{
  Unit z = x + y;
  return z < x;
}
```

結局`N`桁の`Unit`の配列`x[N]`と`y[N]`の加算は次で計算できます。

```cpp
// x[N]とy[N]を足してz[N]に保存する
// 繰り上がりがあればtrueを返す
template<size_t N>
Unit addT(Unit *z, const Unit *x, const Unit *y)
{
  Unit c = 0; // 最初は繰り上がりはない
  for (size_t i = 0; i < N; i++) {
    Unit xc = x[i] + c;
    c = xc < c;
    Unit yi = y[i];
    xc += yi;
    c += xc < yi;
    z[i] = xc;
  }
  return c;
}

```

## asmで確認する

`N = 4`のときのclang-12での出力を見てみましょう。

```
// clang-12 -O2 -DNDEBUG -S -masm=intel t.cpp

add4:
  mov r10, qword ptr [rdx]      // r10 = y[0]
  mov r8, qword ptr [rsi]       // r8 = x[0]
  mov rax, r10
  add rax, r8                   // rax = x[0] + y[0]
  mov qword ptr [rdi], rax      // z[0] = rax
  mov r9, qword ptr [rsi + 8]   // r9 = x[1]
  mov rcx, qword ptr [rdx + 8]  // rcx = y[1]
  mov rax, rcx
  adc rax, r9                   // eax = x[1] + y[1] + CF
  add r10, r8
  mov qword ptr [rdi + 8], rax
  mov r8, qword ptr [rsi + 16]
  adc rcx, r9
  mov rax, qword ptr [rdx + 16]
  setb    r9b
  mov rcx, rax
  adc rcx, r8
  mov qword ptr [rdi + 16], rcx
  mov rcx, qword ptr [rsi + 24]
  add r9b, 255
  adc rax, r8
  mov rax, qword ptr [rdx + 24]
  setb    dl
  mov rsi, rax
  adc rsi, rcx
  add dl, 255
  adc rax, rcx
  setb    al
  movzx   eax, al
  mov qword ptr [rdi + 24], rsi
  ret
```

今はまだ詳細には入りませんが、ごちゃごちゃと計算していることが分かります。
[多倍長整数の実装2（Xbyak）](https://zenn.dev/herumi/articles/bitint-02-xbyak)に続く。
