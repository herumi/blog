---
title: "有限体の実装3（Montgomery乗算の紹介）"
emoji: "🧮"
type: "tech"
topics: ["有限体", "mul", "Montgomery乗算", "llvm", "x64"]
published: false
---
## 初めに

今回は有限体の山場、Montgomery乗算を実装します。
Montgomery乗算は普通の乗算の代わりとなる重要な演算です。
記事全体の一覧は[有限体の実装一覧](https://zenn.dev/herumi/articles/finite-field-01-add#%E6%9C%89%E9%99%90%E4%BD%93%E3%81%AE%E5%AE%9F%E8%A3%85%E4%B8%80%E8%A6%A7)参照。

## Montgomery乗算

素数 $p$ の有限体の元 $x, y$ において乗算は $xy \bmod{p}$ です（ $p$ で割った余り）。
しかし $p$ で割る処理は重たいので、それを避ける方法の一つがMontgomery乗算です。

しばらく数学的な話が続きます。まずいくつか記号の準備をします。
$L$ をCPUのレジスタサイズ（64ビットなら64, 32ビットなら32）、$M=2^L$ とします。以下L=64と仮定します。
$N$ は素数 $p$ を $L$ ビット整数で表現したときの配列の大きさです。
$M$ と $p$ は互いに素なので $M'M - p' p = 1$ となる整数 $0 < M' < p$ と $0 < p' < M$ が存在します。
$p' \equiv p^{-1} \pmod{M}$, $M' \equiv M^{-1} \pmod{p}$ という意味です。

次のコードで示す関数 mont(x, y) をMontgomery乗算と呼ぶことにします。
Pythonならもっと簡潔に記述できますが、C/C++で実装するためにやや冗長に書いています。コードのipはp'と読み替えてください。

```python
def mont(x, y):
  MASK = 2**L - 1
  t = 0
  for i in range(N):
    t += x * ((y >> (L * i)) & MASK) # (A)
    q = ((t & MASK) * self.ip) & MASK # (B)
    t += q * self.p # (C)
    t >>= L # (D)
  if t >= self.p: # (E)
    t -= self.p
  return t
```

コードの説明をしましょう。
### (A)
x, yがN桁の`uint64_t`の配列とすると `(y >> (L * i)) & MASK` は `y[i]` を表します。
つまり `x * y[i]` を[mulUnit](https://zenn.dev/herumi/articles/bitint-04-mul)を使って計算し、tに足します。

### (B)
(A)で計算したtの下位64ビットにp'(=ip)を掛けてその下位64ビットをqとします。
p'もuint64_tなのでここは

```cpp
uint64_t q = t[0] * ip;
```

と同じです。

### (C)
`mulUnit`を使って p と q を掛けて t に足します。

### (D)
t を 64ビット右シフトします。

### (E)
ループを抜けたら t が p より大きければ p を引いて値を返します。

## Montgomery乗算の意味

さて、mont(x, y)が数学的に何をしているか考察しましょう。

定理 : mont(x, y) = $x y M'^{N} \bmod{p}$ である。

### ${}\bmod{p}$で一致することの証明

まず準備として補助関数として整数 $x$ に対して $f(x) = x + ((x * p') \bmod{M})p$ とします。
${}\bmod{M}$ で考えると $f(x) \equiv x + (x * p')p = x(1 + p'p) = xM'M$ なので $f(x)$ は $M$ で割れます。
${}\bmod{p}$ で考えると $f(x) \equiv x$ なので $f(x)/M \equiv x/M \equiv xM'$ です（ $p$ と $M$ が互いに素なので）。

さてPythonのコードに戻ります。$y = \sum_{i=0}^{N-1} y_i M^i$ とします。
最初 t = 0 で (A)から(D)まで実行すると $f(x y_0)M' = x y_0 M'$ を計算したことになります。

次のループでは $t = x y_0 M'$ から出発して同様のことをすると $t = (x y_0 M' + x y_1)M' = (x y_0 + x y_1 M)M'^2$ となります（${}\bmod{p}$ を省略）。
これを $N$ 回繰り返すと $t = (x y_0 + x y_1 M + \cdots + x y_{N-1} M^{N-1})M'^N = xy M'^{N}$ です。

### (E)を実行するだけで両辺が一致することの証明

最後に mont(x, y) が $x y M'^{N} \bmod{p}$ に一致することを確認しましょう。
i回目のループのときにステップ(A)に入る直前は $t \le 2p-1$ とします（i=0のときはt=0なので成立）。

- (A)の右辺の最大値は $(p-1)(M-1)$ なので $t \le 2p-1 + (p-1)(M-1)$ 。
- (B)の右辺の最大値は $M-1$ 。
- (C)の計算後は $t \le 2p-1 + (p-1)(M-1) + (M-1)p=(2p-1)M$ 。
- (D)の計算後は $t \le (2p-1)M / M = 2p-1$ 。

よってループを何度繰り返しても $t \le 2p-1$ が成り立ちます。
したがって $t \bmod{p}$ を計算するには $p$ 以上だったら $p$ を引く(E)だけで $x y M'^{N} \bmod{p}$ を計算できます。

以上で定理の証明が終わりました。
数学の話が続いたのでいったんここで終わります。
