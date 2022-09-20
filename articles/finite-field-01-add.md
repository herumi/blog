---
title: "有限体の実装1（加算）"
emoji: "🧮"
type: "tech"
topics: ["有限体", "add", "x64"]
published: false
---
## 初めに

前回まで[多倍長整数の実装1（C/C++）](https://zenn.dev/herumi/articles/bitint-01-cpp)からの一連の記事で多倍長整数演算の実装の紹介をしました。今回から有限体の実装の紹介をします。有限体で利用する素数は256bit～512bitを想定しています。

## 有限体の実装一覧

- [有限体の実装1（加算）（この記事）](https://zenn.dev/herumi/articles/finite-field-01-add)

## 有限体の復習

有限体についてはまず、[楕円曲線暗号のPythonによる実装その1（有限体とECDH鍵共有）](https://zenn.dev/herumi/articles/sd202203-ecc-1#%E6%9C%89%E9%99%90%E4%BD%93)をごらんください。
ごく簡単に復習すると$p$を素数としたとき、$0$以上$p$未満の整数の集合を$F_p$と書き、有限体と呼びます。

$$
F_p = \{0, 1, 2, ..., p-1\}.
$$

有限体は通常の四則演算と似た性質を持つ四則演算を定義できます。「似た性質」とは$(a+b)+c=a+(b+c)$, $a(bc) = (ab)c$とか$a(b + c) = ab + ac$といった結合法則や分配法則を指します。
四則演算のうち、加算、減算、乗算については$x, y \in F_p$について、$x$, $y$を整数と思って加減乗算したあと$p$で割ったものです。

$$
（有限体での）x + y = （整数での）x + y \mod p.\\
（有限体での）x - y = （整数での）x - y \mod p.\\
（有限体での）x \times y = （整数での）x \times y \mod p.
$$

ここで$\mod p$は$p$で割った余り（$0$以上$p$未満の整数）を表します。

## 有限体の加減算の実装概略

まず、比較的容易な加減算の実装について詳細に入ります。
「$p$で割った余り」を求める操作は通常重たいです。有限体の演算は極限まで高速化したいのでそのような操作は極力避けたいです。

元の$x$, $y$は$0 \le x, y \le p-1$なのですから、$x+y$の範囲は$0 \le x+y \le 2p-2$です。
もし$z=x+y$が$p$以上なら$z$から$p$を引くと$0 \le z-p \le p-2$となります。これは$x+y$を$p$で割った余りに一致します。すなわち

```python
def add(x, y):
  z = x + y
  if z >= p:
    z -= p
  return z
```
とすると結果は常に$F_p$の元（要素）となり、有限体の加算が実装できたことになります。

引き算の場合は$0 \le x, y \le p-1$より$-(p-1) \le x - y \le p-1$です。したがって$x - y < 0$のとき$p$を足せば$0 < x - y \le p-1$となります。

```python
def sub(x, y):
  z = x - y
  if z < 0:
    z += p
  return z
```

## 加減算の実装詳細

前節で紹介したPythonによる実装をC/C++で置き換えるためにaddT, subTを使ってより詳しく考えましょう。

```cpp
// x[] < y[]なら-1. x[] == y[]なら0. x[] > y[]なら1
template<size_t N>
int cmpT(const Unit *x, const Unit *y)
{
  for (size_t i = 0; i < N; i++) {
    Unit a = x[N - 1 - i];
    Unit b = y[N - 1 - i];
    if (a < b) return -1;
    if (a > b) return 1;
  }
  return 0;
}

// z[] = x[] + y[]. 繰り上がりがあればreturn 1. なければ0
template<size_t N>
Unit addT(Unit *z, const Unit *x, const Unit *y);

// z[] = x[] - y[]. 繰り下がりがあればreturn 1. なければ0
template<size_t N>
Unit subT(Unit *z, const Unit *x, const Unit *y);
```

Pythonでの`z = x + y`をN個のUnitに対する操作であるaddTを使った場合、繰り上がりがある場合（CF = 1）は問答無用にpよりも大きく、繰り上がりCFがない場合（CF = 0）はcmpTを使ってp以上であることを確認します。
つまり、

```cpp
// z[] = (x[] + y[]) % p[]
template<size_t N>
void Fp_add(Unit *z, const Unit *x, const Unit *y, const Unit *p)
{
  Unit CF = addT<N>(z, x, y);
  if (CF || cmpT<N>(z, p) >= 0) { // z[] = x[] + y[] >= p[]ならp[]を引く
    subT<N>(z, z, p);
  }
}
```

となります。
引き算の場合は、もう少し簡単で引いて繰り下がりがあるときだけpを足せばよいです。

```cpp
// z[] = (x[] - y[]) % p[]
template<size_t N>
void Fp_sub(Unit *z, const Unit *x, const Unit *y, const Unit *p)
{
  if (subT<N>(z, x, y)) { // z[] = x[] - y[] < 0ならp[]を足す
    addT<N>(z, z, p);
  }
}
```

## x64向け最適化

前節のコードFp_addをx64向けに最適化しましょう。例によってN = 4 or 6とします。Fp_addには二つの高速化の余地があります。一つは不要なメモリ読み書きの削除です。

```cpp
Unit CF = addT<N>(z, x, y);
```
では`x[]`と`y[]`の加算結果を一度`z[]`に格納します。その直後に

```cpp
if (CF || cmpT<N>(z, p) >= 0) {
```
で`z[]`と`p[]`の値を比較しています。途中結果をメモリに保存せずにレジスタ上で処理すれば高速化が望めます。

もう一つは条件分岐です。xとyを足した結果がpを越える確率はxとyがランダムな場合約1/2です。暗号で利用する場合、その傾向は予測できないと考えてよいのでCPUの分岐予測は1/2の確率で外れます。またcmpT関数は条件分岐命令が続くのでパイプラインを乱しがちです。
そこでパイプラインを乱さないように条件移動命令cmovを使います。cmovはフラグに応じてレジスタの移動をするかしないかを選択できる命令です。

```nasm
cmp rax, rcx
cmovc rbx, rdx ; if (rax < rcx) rbx = rdx
```

この命令を使って次のような擬似コードにします。コード中に現れる変数X, YなどはN個のレジスタの組とします。

```cpp
// px : xのアドレスを保持するレジスタ
// py : yのアドレスを保持するレジスタ
// pp : pのアドレスを保持するレジスタ
xor_(CF, CF); // CF = 0
load_rm(X, px); // X = x[]
sub_rm(X, py); // X[] -= y[]
setc(CF.cvt8()); // CFの下位8bitをセット
```
