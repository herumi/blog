# 足し算のループ

## 配列の足し算

2個の配列x, yのそれぞれの要素を足して配列zに代入するCの関数。
```
void intAdd(int *z, const int *x, const int *y, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        z[i] = x[i] + y[i];
    }
}
```

## SVEで実装
これを[xbyak_aarch64](https://github.com/fujitsu/xbyak_aarch64/tree/master)で記述する。
fjmasterブランチは名前空間や一部のクラスがちょっと変わっている(masterになりしだい変更する)。

```
// workaround.hpp
#define Xbyak Xbyak_aarch64
#define CodeGenerator CodeGeneratorAArch64
#define Label LabelAArch64
```
というファイルを用意して`-include workaround.hpp`するとfjmasterを従来通りの形で扱える(2020/7/30現在のバージョン)。

SVEのマニュアルは[The Scalable Vector Extension (SVE), for ARMv8-A]( https://static.docs.arm.com/ddi0584/a/DDI0584A_a_SVE_supp_armv8A.pdf)など参照。

```
#include <xbyak_aarch64/xbyak_aarch64.h>
using namespace Xbyak;

struct Code : CodeGenerator {
    Code()
    {
        const auto& out = x0;
        const auto& src1 = x1;
        const auto& src2 = x2;
        const auto& n = x3;
        Label cond;
        mov(x4, 0);
        b(cond);
    Label lp = L();
        ld1w(z0.s, p0/T_z, ptr(src1, x4, LSL, 2));
        ld1w(z1.s, p0/T_z, ptr(src2, x4, LSL, 2));
        add(z0.s, z0.s, z1.s);
        st1w(z0.s, p0, ptr(out, x4, LSL, 2));
        incd(x4);
    L(cond);
        whilelt(p0.s, x4, n);
        b_first(lp);
        ret();
    }
};

int main()
{
    Code c;
    c.ready();
    auto f = c.getCode<void (*)(int *, const int *, const int *, size_t)>();
    ...
```
## コード生成
- CodeGeneratorクラスを継承する。
- その中で関数呼び出しの形でニーモニックを記述する。

```
    Code c;
    c.ready();
    auto f = c.getCode<void (*)(int *, const int *, const int *, size_t)>();
```
内部でコードの書き換えをするため`read()`メソッドはCPUの命令キャッシュをフラッシュする。

## 関数の引数

`intAdd(int *z, const int *x, const int *y, size_t n)`という形なのでz, x, y, nがそれぞれレジスタのx0, x1, x2, x3に対応する。

```
    const auto& out = x0;
    const auto& src1 = x1;
    const auto& src2 = x2;
    const auto& n = x3;
```

## データの読み込み

```
    ld1w(z0.s, p0/T_z, ptr(src1, x4, LSL, 2));
```
メモリから1word(32bit)分データを読む。

```
float *src1;
int x4;
float z = src1[x4]; // アドレスは((uint8_t*)src1) + (x4 << 2)
```
のSIMD版(LSLは論理左シフト)。z0.sはz0レジスタをfloat型として扱う。
z0が512bitの場合、float(32bit)が16個分。

p0は述語レジスタ。1が立っているところはデータを読み込む。0のところはT_z(zero)にする。

memory |x0|x1|x2|x3|...
-|-|-|-|-|-
z0.s |x0|0|x2|x3|...
p0|1|0|1|1|...

p0の設定は後述する。

## add

```
    add(z0.s, z0.s, z1.s);
```
`z0 += z1;`のSIMD版。

## データの書き込み

```
  st1w(z0.s, p0, ptr(out, x4, LSL, 2));
````
z0の値をout + (x4 << 2)に書き込む。
述語レジスタp0の値が0のところは書き込まない。

## オフセット更新

```
    incd(x4);
```
512/32 = 16だけx4を増やす。

## 述語レジスタの更新

```
    whilelt(p0.s, x4, n);
```
`x4 + i < n`が成り立つところまでp0のi番目のbitを立てる。そうでなければゼロ。

たとえば`x4 + 16 <= n`なら全部1。
`x4 + 3 = n`なら0, 1, 2番目だけが1で残り0。

こうすると次のld1wでn未満のところだけデータを読み込める。
ビットが立っていないところはメモリのread属性がなくても例外が飛ばない。

## ループ終了判定

```
  b_first(lp);
```
`b.mi`のalias(x4 < nである限りlpにジャンプ)。

## 全体
- これだけで16個ずつデータを処理する。しかも端数処理も行える。
- SVEが512bitレジスタでなく256や1024bitであっても同じコードで動く(はず)。
- SVEはScalable Vector Extensionの略。
  - [The ARM Scalable Vector Extension](https://arxiv.org/pdf/1803.06185)

## ベンチマーク
1024個のfloatに対して
C|360nsec
-|-
SVE|277nsec

1024/16 = 64回のループなので1ループあたり4.3nsec。2GHzなら9clk(clock cycle)ぐらい。
