---
title: "多倍長整数の実装6（乗算後加算とintrinsicの限界）"
emoji: "🧮"
type: "tech"
topics: ["mul", "mulx", "adox", "adcx", "intrinsic"]
published: true
---
## 初めに

今回はN桁xN桁の固定多倍長整数の乗算の実装を高速化するための乗算後加算を実装します。
記事全体の一覧は[多倍長整数の実装1（C/C++）](https://zenn.dev/herumi/articles/bitint-01-cpp)参照。

## N桁xN桁の乗算

N桁xN桁の乗算結果は2N桁になります。既にN桁x1桁の乗算(mulUnit)とN桁同士の加算(add)を実装しているのでN桁xN桁は容易です。
まずN桁x1桁を実行し、一つ前の結果に加算していけばよいのです。

```
          3  2  1
        x 5  7  4
       -----------
       1 [2  8  4] ← [ret xy] = [3 2 1] x 4
    2 [2  4  7]    ← [ret xy]+= [3 2 1] x 7 ↑足し混む
 1 [6  0  5]       ← [ret xy]+= [3 2 1] x 5 ↑足し混む
------------------
[1  8  4  2  5  4]
```


```cpp
// z[2N] = x[N] * y[N]
template<size_t N>
void mulT(Unit *pz, const Unit *px, const Unit *py)
{
  pz[N] = mulUnitT<N>(pz, px, py[0]); // px[] * py[0]
  for (size_t i = 1; i < N; i++) {
    Unit xy[N], ret;
    ret = mulUnitT<N>(xy, px, py[i]); // px[] * py[i]
    pz[N + i] = ret + addT<N>(&pz[i], &pz[i], xy);
  }
}
```

## mulUnitAdd

ここでmulUnitTしてからaddTする部分を抜き出してmulUnitAddTという名前の関数を作りましょう。

```cpp
template<size_t N>
Unit mulUnitAddT(Unit *z, const Unit *x, Unit y)
{
  Unit xy[N], ret;
  ret = mulUnitT<N>(xy, x, y);
  ret += addT<N>(z, z, xy);
  return ret;
}
```

そうするとmulTは次のように書き直せます。

```cpp
// z[2N] = x[N] * y[N]
template<size_t N>
void mulT(Unit *pz, const Unit *px, const Unit *py)
{
  pz[N] = mulUnitT<N>(pz, px, py[0]);
  for (size_t i = 1; i < N; i++) {
    pz[N + i] = mulUnitAddT<N>(&pz[i], px, py[i]);
  }
}
```

## mulUnitAddの改善

mulUnitAddは一時変数Unit xy[N]を確保し、そこに乗算結果を保存した後その結果をUnit *zに足しています。
一時変数を経由せずにmulUnitしながらaddできないでしょうか。

```
[z3 z2 z1 z0] + [x3 x2 x1 x0] * y =

   [z3 z2 z1 z0]
          [x0*y]
       [x1*y]    ↑足し混む
    [x2*y]    ↑足し混む
 [x3*y]    ↑足し混む
```

mulUnitはHaswell以降のmulxを使った場合、add+adcのキャリーで実装しました（[多倍長整数の実装5（乗算とmulx）](https://zenn.dev/herumi/articles/bitint-05-mulx)）。
一時変数を経由せずに足し混むには`x0*y`のHighと`x1*y`のLow Unitを加算してから`z[]`に足す必要があります。
しかし、HighとLowを加算したときキャリーフラグCFが更新されるので、`z[]`に足せません。
これは困りました。

## adoxとadcx

前節の問題はキャリーフラグが1個しかないためmulUnitで使うCFと`z[]`に足し混むCFがバッティングしてしまうのが原因です。
この問題を解決するためにIntelはキャリーフラグを2個用意しました。

といっても新しいフラグを追加する訳にはいきません。CFの他に殆ど使われない（少なくとも私は使ったことがない）OF（オーバーフローフラグ）を使います。
Broadwell以降、CFを使う加算命令adcxとOFを使う加算命令adoxが追加されています。
AMD系は少し遅くてRyzenから対応です。
CFつきの加算命令adcとadcxの違いは、adcは処理後にCFとOFの両方（他のフラグも）を更新してしまうのに対して、adcxとadoxはそれぞれCFとOFしか更新しません。
そのため異なる多倍長加算を並行して行えるのです。

![adcxとadox](/images/bitint-06-adx.png =500x)
*adcxとadox*

## intrinsic関数とその限界

adcxとadoxに対応するintrinsic関数はどちらも[_addcarryx_u64](https://www.intel.com/content/www/us/en/develop/documentation/cpp-compiler-developer-guide-and-reference/top/compiler-reference/intrinsics/intrinsics-for-later-gen-core-proc-instruct-exts/intrinsics-for-multi-precision-arithmetic/addcarryx-u32-addcarryx-u64.html)です。
区別がないので変数を変えればよいのだろうと実装してみました。

```cpp
// [ret:z[N]] = z[N] + x[N] * y
template<size_t N>
uint64_t mulUnitAddT(uint64_t *z, const uint64_t *x, uint64_t y)
{
  assert(z != x);
  unsigned long L, H, t;
  uint8_t c1 = 0, c2 = 0;
  t = z[0];
  for (size_t i = 0; i < N; i++) {
    H = _mulx_u64(x[i], y, &L);
    c1 = _addcarryx_u64(c1, t, L, (unsigned long*)&z[i]);
    if (i == N - 1) break;
    c2 = _addcarryx_u64(c2, z[i + 1], H, &t);
  }
  c2 = _addcarryx_u64(c2, 0, H, &t);
  _addcarryx_u64(c1, 0, t, &t);
  return t;
}
```

しかし、残念ながらどのコンパイラ（gcc-10, clang-12, VC 2022, icc-2021）期待するコードを生成してくれません（-O2 -madx -mbmi2 -funroll-loops）。
gccは[adcxやadoxを使わない](https://gcc.gnu.org/legacy-ml/gcc-help/2017-08/msg00100.html)そうです。
コンパイラはOFとCFのチェインの区別ができないというコンパイラの限界だそうです。

iccはadcxは使ってくれたけどなんだか[微妙](https://godbolt.org/z/5Wrrq4K3q)です。
仮に対応したとしてもコンパイラ依存が強くて安定して使えそうに思えません。
残念ながらこの関数はasmで書くのがよさそうです。
