---
title: "楕円曲線暗号のための数学8（マルチスカラー倍算のパラメータ最適化）"
emoji: "🧮"
type: "tech"
topics: ["楕円曲線暗号", "スカラー倍算", "msm", "zkSNARK"]
published: false
---
## 初めに
前回、[楕円曲線の複数の点のスカラー倍算の和を求める方法](https://zenn.dev/herumi/articles/ecc-multi-scalar-multiplication)を紹介しました。
今回は、その手法が効率よくなるパラメータを探します。今まで紹介した技法の総まとめのようなものですので、過去の記事も復習してください。

## 記号の準備
楕円曲線 $E$ はBLS12-381で、$P_1, \dots, P_n$ を楕円曲線の点、$x_1, \dots, x_n$ を有限体の点としたとき、$Q=x_1 P_1 + \dots + x_n P_n$ を求めます。$n$ は数千から数万の規模を想定しています。

## マルチスカラー倍算適用前の準備
まず、[有限体の複数の元の逆数を一度に求める高速な方法](https://zenn.dev/herumi/articles/multi-inverse-of-ff)を用いて、入力された全ての楕円曲線の点 $P_1, \dots, P_n$ をアフィン座標に変換します。
こうすることで、以降の楕円曲線の加算について[ヤコビ座標とアフィン座標の併用](https://zenn.dev/herumi/articles/ecc-jacobi-coordinate#%E3%83%A4%E3%82%B3%E3%83%93%E5%BA%A7%E6%A8%99%E3%81%A8%E3%82%A2%E3%83%95%E3%82%A3%E3%83%B3%E5%BA%A7%E6%A8%99%E3%81%AE%E4%BD%B5%E7%94%A8)するテクニックが使えます。
BLS12-381の楕円曲線の[自己準同型写像](https://zenn.dev/herumi/articles/ecc-mul-glv-endo#%E8%87%AA%E5%B7%B1%E6%BA%96%E5%90%8C%E5%9E%8B%E5%86%99%E5%83%8F) $\varphi:E \ni P \mapsto L P \in E$ を使って $P'_i=\varphi(P_i)$ を計算し、約256ビットの $x_i$ も $x_i = a_i + b_i L$ となる128ビットの $a_i$, $b_i$ に分割します。
$\varphi(x,y)=(wx,y)$ なのでアフィン座標になった $P$ に対して $P'$ もアフィン座標のままです。
この準備により、$n → 2n$, $x_i$ は128ビット整数となり、以降これを改めてマルチスカラー倍算の入力とします。

## アルゴリズムの疑似コード
前回紹介したアルゴリズムを疑似コードに書き下ろします。

```python
# Σ (x[i]のposビットからwビット) * P[i]
def mulVecUpdateTable(x, pos, w, P):
  tblN = 2**w - 1
  tbl = [0] * tblN
  for i in range(n):
    v = (x[i] >> pos) & tblN # x[i]のposビットからwビット取り出す
    if v:
      tbl[v-1] += P[i]

  sum = tbl[tblN - 1]
  win = sum
  for i in range(1, tblN+1):
      sum += tbl[tblN - 1 - i]
      win += sum
  return win
```

```python
def msm(x, P):
  # 自己準同型写像適用後
  # x : 128ビット整数
  # P : アフィン座標化済
  w = ???
  tblN = 2**w - 1
  maxBitSize = 128
  winN = (maxBitSize + w-1) // w

  z = mulVecUpdateTable(x, w * (winN-1), w, P)
  for w in range(1, winN):
    for i in range(w):
      z = dbl(z)
    z += mulVecUpdateTable(x, w * (winN-1-w), w, P)
  return z
```

簡単に疑似コードの説明をします。
1. `msm(x, P)` が `Σ x[i] P[i]` を計算します。アフィン座標化や自己準同型写像適用済とします。`x[i]` の最大ビット長 `maxBitSize` は128です。
2. `w` ビットずつ `x[i]` を切り出す回数は `winN` です。`w` はこれから決めます。
3. `mulVecUpdateTable` (名前がよくないですが思いつかない) は `x[i]` の `pos` ビットから `w` ビット取り出して `P[i]` との積の和を求めます。

## 演算コストの見積もり
前節の疑似コードにしたがって演算コストを見積もりましょう。
`dbl` の回数は `w * winN` でほぼ `maxBitSize` で一定なので加算回数のみをカウントします。
`mulVecUpdateTable` ではおよそ `n` 回の `add` をした後、`2 * tblN` 回の `add` をして、それを `winN` 回呼び出して `add` するので、全体で

$$
(n + 2 * \text{tblN} + 1) * \text{winN}
$$
です（定数項は多少いいかげんです）。`tbl=2**w-1` で `winN=128/w` なので、関数の形にすると、

$$
f_n(w) = (n + 2(2^w-1)+1) \times 128/w = 128(n+2^{w+1}-1)/w.
$$

したがって、`n` が与えられたときに $f_n(w)$ が最小となる `w` を使うのがよいです。

## 演算コストが最小となるパラメータ
$f_n(w)$ が最小となる `w` を求めるのですが、解析的には解けません。LambertのW関数（関数 $x \mapsto x e^x$ の逆関数）を用いて表現できるようですが、その計算に時間がかかってはうれしくありません。
簡単な近似式が無いかと試行錯誤して、

```python
def ilog2(n):
  return n.bit_length()

def argmin_of_f(n):
  if n <= 16:
    return 2
  log2n = ilog2(n)
  return log2n - ilog2(log2n)
```
というのを見つけました。残念ながら、どうやって導出したのかは覚えていません。思い出せたらまた紹介します。

`ilog2(x)` は $2^n \le x$ となる最大の整数 $n$ を返します。x64では `bsr` 命令で簡単に求められます。
ざっと調べた感じでは1から1億までの範囲で厳密解との差は $\pm 1$ に納まっていました。
そのため、`argmin_of_f` で近似解 `w` を求めてからその前後の `f_n(w-1)`, `f_n(w+1)` を計算して一番小さいのを見つければ厳密解に一致します。

## msmの演算コスト評価
前節のargminを用いてコストを評価しましょう。

$w \sim \log_2(n) - \log_2(\log_2(n))$ のとき $f_n(w)$ をざっくりと評価すると $n \cdot 128(1+1/\log_2(n))/(\log_2(n)-\log_2(\log_2(n)))$ となります。
1回のスカラー倍算を $n$ 回するなら線形に増えますが、これは線形よりも若干コストが増えにくいことが分かります。

$n=8192$ のときは `argmin_of_f(n)` は `w=9` なので、$f_{8192}(w) \sim 16 n$.
1回だけのスカラー倍算の加算のコストはウィンドウサイズ $w=6$ のときで58だったので4倍近くコストが下がることが分かります。
逆に $n=64$ ぐらいだと1回ずつ計算するのと変わりません。$n$ の大きさに応じてアルゴリズムを切り換える必要があります。
