---
title: "有限体の実装1（加算）"
emoji: "🧮"
type: "tech"
topics: ["有限体", "add", "x64"]
published: true
---
## 初めに

前回まで[多倍長整数の実装1（C/C++）](https://zenn.dev/herumi/articles/bitint-01-cpp)からの一連の記事で多倍長整数演算の実装の紹介をしました。今回から有限体の実装の紹介をします。有限体で利用する素数は256bit～512bitを想定しています。

## 有限体の実装一覧

- [有限体の実装1（加算）（この記事）](https://zenn.dev/herumi/articles/finite-field-01-add)

## 有限体の復習

有限体についてはまず、[楕円曲線暗号のPythonによる実装その1（有限体とECDH鍵共有）](https://zenn.dev/herumi/articles/sd202203-ecc-1#%E6%9C%89%E9%99%90%E4%BD%93)をごらんください。
ごく簡単に復習すると$p$を素数としたとき、$0$以上$p$未満の整数の集合を$F_p$と書き、有限体と呼びます。

$$
F_p := \{0, 1, 2, ..., p-1\}.
$$

有限体は通常の四則演算と似た性質を持つ四則演算を定義できます。「似た性質」とは$(a+b)+c=a+(b+c)$, $a(bc) = (ab)c$とか$a(b + c) = ab + ac$といった結合法則や分配法則を指します。
四則演算のうち、加算、減算、乗算については$x, y \in F_p$について、$x$, $y$を整数と思って加減乗算したあと$p$で割ったものです。

$$
（有限体での）x + y := （整数での）(x + y) \mod p.\\
（有限体での）x - y := （整数での）(x - y) \mod p.\\
（有限体での）x \times y := （整数での）(x \times y) \mod p.
$$

ここで$x \mod p$は$x$を$p$で割った余り（$0$以上$p$未満の整数）を表します。

## 有限体の加減算の実装概略

まず、比較的容易な加減算の実装に入ります。
「$p$で割った余り」を求める操作は通常重たいです。有限体の演算は極限まで高速化したいのでそのような操作は避けたいです。

元の$x$, $y$は$0 \le x, y \le p-1$なのですから、$z:=x+y$の範囲は$0 \le x+y \le 2p-2$です。
もし$z$が$p$以上なら$z$から$p$を引くと$0 \le z-p \le p-2$となります。これは$z$を$p$で割った余りに一致します。すなわち

```python
# assume 0 <= x, y < p
def fp_add(x, y):
  z = x + y
  if z >= p:
    z -= p
  return z
```
とすると結果は常に$F_p$の元（要素）となり、有限体の加算が実装できたことになります。

引き算の場合は$0 \le x, y \le p-1$より$-(p-1) \le x - y \le p-1$です。したがって$x - y < 0$のとき$p$を足せば$0 < x - y \le p-1$となります。

```python
def fp_sub(x, y):
  z = x - y
  if z < 0:
    z += p
  return z
```
この変形で$p$で割る操作をなくせました。

## C++での実装

前節で紹介したPythonによる実装をC++で置き換えるためにaddT, subTを使ってより詳しく考えましょう。

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
void fp_add(Unit *z, const Unit *x, const Unit *y, const Unit *p)
{
  Unit CF = addT<N>(z, x, y);
  if (CF || cmpT<N>(z, p) >= 0) { // z[] = x[] + y[] >= p[]ならp[]を引く
    subT<N>(z, z, p);
  }
}
```

となります。引き算の場合は、もう少し簡単で引いて繰り下がりがあるときだけpを足せばよいです。

```cpp
// z[] = (x[] - y[]) % p[]
template<size_t N>
void fp_sub(Unit *z, const Unit *x, const Unit *y, const Unit *p)
{
  if (subT<N>(z, x, y)) { // z[] = x[] - y[] < 0ならp[]を足す
    addT<N>(z, z, p);
  }
}
```

## 最適化への準備

前節のコードfp_addをx64向けに最適化しましょう。fp_addには二つの高速化の余地があります。一つは不要なメモリ読み書きの削除です。

```cpp
Unit CF = addT<N>(z, x, y);
```
では`x[]`と`y[]`の加算結果を一度`z[]`に格納します。その直後に

```cpp
if (CF || cmpT<N>(z, p) >= 0) {
```
で`z[]`と`p[]`の値を比較しています。途中結果をメモリに保存せずにレジスタ上で処理すれば高速化が望めます。

もう一つは条件分岐です。xとyを足した結果がpを越える確率はxとyがランダムな場合約1/2です。暗号で利用する場合、その傾向は予測できないと考えてよいのでCPUの分岐予測は1/2の確率で外れます。またcmpT関数は条件分岐命令が続くのでパイプラインを乱しがちです。
そこでパイプラインを乱さないように分岐を削除する必要があり、そのために条件移動命令cmovを使います。

```nasm
cmp rax, rcx
cmovc rbx, rdx ; if (rax < rcx) rbx = rdx
```

cmovはフラグに応じてレジスタの移動をするかしないかを選択できますが、それしかできません。前述のPythonのコードのようなifが成り立つときだけ計算するコードを陽にifを含まないように変形します。
cmpTによる比較は実際に引き算して負になるかで判断しましょう。CPU上でのsubは2引数しか取れないことを考慮し、簡略化のためにPythonでfp_addを変形すると次のようになります。select関数はcmovに相当します。

```python
def select(cond, x, y):
  return x if cond else y

def fp_add(x, y):
  x += y
  t = x
  t -= p # t = x + y - p
  return select(t < 0, x, t)
```

同様にfp_subをselectを使って書き直すと

```python
def fp_sub(x, y):
  x -= y
  t = x
  t += p # t = x - y + p
  return select(t < 0, x, t)
```

## x64asmコード

このコードを念頭にasm（を生成するPython DSL）を書くと次のようになります。コード中に現れる変数X, TはN個のレジスタの組とします。

```python
# px : xのアドレスを保持するレジスタ
# py : yのアドレスを保持するレジスタ
# pp : pのアドレスを保持するレジスタ
xor_(eax, eax)     # eax = 0
add_rmm(X, px, py) # X = px[] + py[]
setc(al)           # eax = CF（Xが繰り上がれば1）
copy_rr(T, X)      # T = X
sub_rm(T, pp)      # T -= pp[]
sbb(eax, 0)        # CFのチェック
cmovc_rr(T, X)     # T < 0ならT = X
store(pz, T)
setc(al); // eax = CF (Xが繰り上がれば1)
mov_rr(T, X); // T = X
```

## LLVMによる実装

[多倍長整数の実装8（LLVMを用いたasmコード生成）](https://zenn.dev/herumi/articles/bitint-08-llvm)で紹介したllファイル生成補助ツール（DSL）を使った方法も紹介しましょう。こちらはAArch64（やRISC-VなどLLVM対応環境で）で動きます。

```cpp
// unit = 256 or 320など
Operand pz(IntPtr, unit);
Operand px(IntPtr, unit);
Operand py(IntPtr, unit);
Operand pp(IntPtr, unit);

Function mcl_fp_add("mcl_fp_add", Void, pz, px, py, pp);

beginFunc(mcl_fp_add);

Operand x = loadN(px, N);
Operand y = loadN(py, N);
x = zext(x, bit + unit);
y = zext(y, bit + unit);
x = add(x, y);
Operand p = loadN(pp, N);
p = zext(p, bit + unit);
y = sub(x, p);
Operand c = trunc(lshr(y, bit), 1);
x = select(c, x, y);
x = trunc(x, bit);
storeN(x, pz);
ret(Void);

endFunc();
```

私が実装したDSLを簡単に解説します。
OperandはLLVM-IRのレジスタを表すクラスです。

```cpp
Operand pz(IntPtr, unit);
Operand px(IntPtr, unit);
Operand py(IntPtr, unit);
Operand pp(IntPtr, unit);
```
でunit（ここでは64）ビットサイズのポインタを表します。関数の引数`Unit *z, const Unit *x, const Unit *y, const Unit *p`に対応するインスタンスを用意します。

```cpp
Function mcl_fp_add("mcl_fp_add", Void, pz, px, py, pp);

beginFunc(mcl_fp_add);
...
endFunc();
```

`mcl_fp_add`という名前の関数で戻り値の型が`void`, 引数が4個で`Unit*`型を示します。`beginFunc`と`endfunc`の中で関数を実装します。

```cpp
Operand x = loadN(px, N);
Operand y = loadN(py, N);

x = zext(x, bit + unit);
y = zext(y, bit + unit);
x = add(x, y);
```

bit = 64ビット x Nで、`loadN`はポインタpxからbit分のメモリを読み込み、zextでそれらをunitゼロ拡張します。
LLVM-IRのレジスタははC/C++のunsigned int的な挙動を示し、CFなどはありません。したがってx + yが元のサイズを越える場合を扱うにはレジスタを大きくしなければならないのです。
そのあとxとyを加算してxに代入します。

```cpp
Operand p = loadN(pp, N);
p = zext(p, bit + unit);
y = sub(x, p);
```

同様に素数pを読み込みゼロ拡張し、pを引いてyに代入します。

```cpp
Operand c = trunc(lshr(y, bit), 1);
```

unit分ゼロ拡張されているので、もしy（元のx + y - p）の上からunitビット目cが1なら、それは`x + y < p`を意味します。

bit+Unitビットの変数|上位Unitビットの値|下位bitビットの値
-|-|-
x|0|x
y|0|y
x+y|CF|x+yの下位bit
p|0|p
x+y-p|x+y>=0なら0、それ以外は-1|x+y-pの下位bit

```cpp
x = select(c, x, y);
x = trunc(x, bit);
```
cが1ならx + y < pなので元のx + y、そうでなければ元のx + y - pを選択（select）し、元のbitサイズに切り捨て（trunc）ます。

```cpp
storeN(x, pz);
ret(Void);
```
最後に結果を保存します。

ちなみに後半を普通の条件ジャンプを使って次のようにも記述できます。

```cpp
    x = zext(x, bit + unit);
    y = zext(y, bit + unit);
    Operand t0 = add(x, y);
    Operand t1 = trunc(t0, bit);
    storeN(t1, pz);               // x + yの下位bitをpzに保存
    Operand p = loadN(pp, N);
    p = zext(p, bit + unit);
    Operand vc = sub(t0, p);      // x + y - pのフラグを
    Operand c = lshr(vc, bit);    // cとする
    c = trunc(c, 1);
Label carry("carry");
Label nocarry("nocarry");
    br(c, carry, nocarry);        // フラグに応じてジャンプ
putLabel(nocarry);
    storeN(trunc(vc, bit), pz);
    ret(Void);
putLabel(carry);
    ret(Void);
```

## x64でのベンチマークと考察

LLVM-IRで条件分岐を使ったもの、selectを使ったもの、手書きx64-asmで速度比較しました。
yの値を常に分岐する(p-1)、常に分岐しない(1), ランダムになるパターンを与えます。環境はUbuntu 22.04.1 LTS + clang++-12 no Platinum 8280(turbo boost off)、pは381ビット素数です。分散が結構大きいので目安程度にしてください。

パターン|LLVM-IR条件分岐あり|LLVM-IR select使用|手書きx64-asm
-|-|-|-
常に分岐する|23|36|29
常に分岐しない|18|34|29
ランダム|48|38|32

次のことが読み取れます。

- 「LLVM-IR select」と「手書きx64-asm」ではcmovを使うためパターンによらず速度はほぼ一定である。
- 「条件分岐あり」では、常に分岐する/分岐しないパターンではcmovを使った場合よりも速いが、ランダムなデータに対しては遅い。これが分岐予測が外れたときのペナルティです。

なお、CPU環境やLLVMのバージョン、オプションによってはbrを使っていてもターゲットasmを生成するときにLLVMがcmov相当に変換することがあります。生成コードを確認してください。

一連のコードの詳細は[gen_test.cpp](https://github.com/herumi/mcl/blob/dev/misc/gen_test.cpp)などを参照してください。

## まとめ

有限体の加減算における高速化のポイント

- mod pをせずに加減算だけですませる。
- 分岐予測が外れたときのペナルティは加減算に対して無視できないコストである。
- できるだけcmovなどの条件分岐を使わないコードを考える。
