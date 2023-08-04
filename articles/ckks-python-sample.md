---
title: "準同型暗号CKKSその4 Pythonによる動作確認"
emoji: "🧮"
type: "tech"
topics: ["準同型暗号", "CKKS", "Python"]
published: false
---
## 初めに
今回は準同型暗号CKKSの理解を深めるためにPythonによる簡単なサンプルを動かしてみます([ckks.py](https://github.com/herumi/misc/blob/main/crypto/src/ckks.py))。実用的なものでは全く無い（特にレベルの扱いはいいかげん）ので注意してください。
記事一覧は[準同型暗号CKKSその1 多項式環](https://zenn.dev/herumi/articles/ckks-ring-iso)

## パラメータ設定
多項式と行列を扱うためにnumpyをインストールしておきます。[準同型暗号CKKSその1 多項式環](https://zenn.dev/herumi/articles/ckks-ring-iso)に従っていくつかのパラメータを適当に設定します。
Pythonの複素数は`i`ではなく`1j`なのに注意してください。添え字が数式と異なってるのはご勘弁。
- $M$ : 2のべき乗
- $N:=M/2$
- $\xi:=exp(2 \pi i / M)$
- $S:=(\xi^{(2i+1)*j})$
- invS $:=S^{-1}$
- cycloPoly $:= X^N+1$

```python
import numpy as np
from numpy.polynomial import Polynomial as poly

def init(M: int, delta = 1000):
  global g_
  g_ = Param(delta)
  g_.M = M
  g_.N = M//2
  g_.halfN = g_.N//2
  g_.xi = np.exp(2 * np.pi * 1j / g_.M)

  # g_.S = (a_ij) = (((g_.xi^(2*i + 1))^j)
  for i in range(g_.N):
    root = g_.xi ** (2 * i + 1)
    row = []
    for j in range(g_.N):
      row.append(root ** j)
    g_.S.append(row)
  g_.invS = np.linalg.inv(g_.S)

  # M-th cyclotomic poly : phi_M(X) = X^N + 1
  g_.cycloPoly = poly([1] + [0] * (g_.N-1) + [1])
  return g_
```

## エンコード・デコード実装の準備
エンコードとデコード関数に必要な部品を準備します。

### $σ$と$σ^{-1}$の実装
$σ:ℂ[X] \rightarrow ℂ^N$は多項式を受け取り、$\xi^{2j+1}$の値を代入して並べたベクトルでした。`init`で設定した行列$S$の成分$S[i][1]$を多項式で評価してリストを作り、それを`np.array`として返します。
逆関数$σ^{-1}$は$S$の逆行列を`np.array`の`b`に掛けて多項式とみなします。

```python
def sigma(p: poly):
  f = []
  for i in range(g_.N):
    f.append(p(g_.S[i][1]))
  return np.array(f)

def invSigma(b: np.array):
  return poly(np.dot(g_.invS, b))
```

### 多項式の係数を丸める`roundCoeff`
`roundCoeff`は係数を取り出し、`np.round`を適用してリストにし、結果を多項式とみなします。
```python
def roundCoeff(p: poly):
  if type(p) == poly:
    p = p.convert().coef
  p = poly(list(map(np.round, p)))
  return p
```

### 複素数係数多項式を実数係数多項式に変換する`getRealPoly`
複素数係数多項式のままでも構わないのですが、実数係数と分かっているときのために簡略化します。念のため虚部があればエラーにしています。
```python
def getRealPoly(p: poly):
  def f(x):
    if x.imag != 0:
      raise('not zero')
    return x.real
  if type(p) == poly:
    p = p.convert().coef
  return poly(list(map(f, p)))
```

### $ℂ^N$から$ℂ^{N/2}$への射影(projection)とその逆写像
関数$\pi:ℂ^N → ℂ^{N/2}$は前半の要素を取り出すだけの関数`proj`です。`halfN`は`N/2`として設定しています。

```python
def proj(z: np.array):
  return z[0:g_.halfN]
```

$\pi$の逆関数`invProj`は、前半の要素を逆順にし、複素共役をとったものを前半にくっつけたものでした。
```python
def invProj(z: np.array):
  w = z.conjugate()[::-1]
  return np.hstack([z, w])
```

## EcdとDcdの実装
これらの関数を組み合わせて[CKKSの平文空間とエンコード・デコード](https://zenn.dev/herumi/articles/ckks-encoding#ckks%E3%81%AE%E5%B9%B3%E6%96%87%E7%A9%BA%E9%96%93%E3%81%A8%E3%82%A8%E3%83%B3%E3%82%B3%E3%83%BC%E3%83%89%E3%83%BB%E3%83%87%E3%82%B3%E3%83%BC%E3%83%89)を実装します。
$Ecd(z)=[Δσ^{-1}(\pi^{-1}(z))]$をそのまま実装します。

```python
def Ecd(z: np.array):
  p = invSigma(invProj(z)) * g_.delta
  p = roundCoeff(p)
  p = getRealPoly(p)
  return p
```
$Dcd(f)=\pi(σ(Δ^{-1}f))$も同様です。

```python
def Dcd(m: poly):
  return proj(sigma(m) / g_.delta)
```

動作確認をしてみましょう。[論文](https://eprint.iacr.org/2016/421)の9ページ冒頭を引用。

![サンプル](/images/ckks-2016-421-p9.png)

```python
init(8, delta=64)
z = np.array([3+4j, 2-1j])
print(f'{z=}')
m = Ecd(z)
print(f'ecd={m}')
print(f'ded={Dcd(m)}')
```

```shell
z=array([3.+4.j, 2.-1.j])
ecd=160.0 + 91.0 x + 160.0 x**2 + 45.0 x**3
dcd=[3.008233+4.00260191j 1.991767-0.99739809j]
```

$M=8$, $Δ=64$として`init`を初期化し、$z=(3+4i, 2-i)$をエンコードすると、$160+91X+160X^2+45X^3$となり、それをデコードすると$[3.008233+4.00260191j, 1.991767-0.99739809j]$という値になりました。
エンコードしてデコードした時点で元に戻らず誤差があるのを確認できます。

## 鍵生成
今回の紹介ではパラメータや乱数は極めて雑なので、ここも雑ですいませんがこんな感じです。

```python
def KeyGen():
  # secret
  s = randHWT()
  # public
  a = randRQL()
  e = randDG()
  b = modPoly(-a * s + e)
  # evalutate
  n = g_.P * g_.qL
  ap = randRQL()
  ep = randDG()
  bp = modPoly(-ap * s + ep + g_.P * s * s)
  bp = modCoeff(bp, n)
  return (s, (b, a), (bp, ap))
```
`randHWT`は$\Set{-1,0,1}$から適当に$N$個選び、それを秘密鍵`s`とします。`randRQL`や`randDG`は適当に`[-100,100]`, `[-3, 3]`あたりの乱数を生成します。
`b = modPoly(-a * s + e)`として`(b, a)`が公開鍵、`(bp, ap)`が評価鍵です。

## 暗号化と復号
$m \in R$の暗号化は公開鍵$pk=(b,a)$に対して乱数$v, e_0, e_1$を選んで$c_0:=v+m+e_0$, $c_1:=va+e_1$を求めればよいので

```python
def Enc(pk, m):
  v = randZO()
  e0 = randDG()
  e1 = randDG()
  t0 = modPoly(v * pk[0] + m + e0)
  t1 = modPoly(v * pk[1] + e1)
  t0 = modCoeff(t0, g_.qL)
  t1 = modCoeff(t1, g_.qL)
  t0 = getRealPoly(t0)
  t1 = getRealPoly(t1)
  return (t0, t1)
```
復号は$c=(b, a)$に対して秘密鍵$s$を使って$Dec(s, c)=b + a s$なので
```python
def Dec(sk, c):
  b, a = c
  t = modPoly(b + a * sk)
  ql = get_ql(g_.l)
  t = modCoeff(t, ql)
  return t
```

`z1= [0.+8.j 1.+0.j]`をエンコード→暗号化→復号→デコードとすると次のようになりました。やはり誤差を含んでいます。
```pyhon
z1= [0.+8.j 1.+0.j]
m1= 500.0 + 2475.0 x + 4000.0 x**2 + 3182.0 x**3
c1.0= 539.0 + 2404.0 x + 3697.0 x**2 + 2970.0 x**3
  .1= 15.0 - 126.0 x - 109.0 x**2 + 193.0 x**3
dec c1= 497.0 + 2473.0 x + 4001.0 x**2 + 3190.0 x**3
dcd c1= [-0.00999556+8.00534570e+00j  1.00399556+3.34570186e-03j]
```

## 暗号文の加算と乗算

暗号文の加算は要素ごとの加算なので簡単です。
```python
def add(c1, c2):
  b1, a1 = c1
  b2, a2 = c2
  return (b1 + b2, a1 + a2)
```

乗算は暗号文の構造が増える$(d_0, d_1, d_2)$を計算し、評価鍵を使って$(t_0, t_1):=[P^{-1} d_2 evk]$を計算し、最後に$(d_0, d_1)+(t_0, t_1)$を求めます。

```python
def mul(c1, c2, ek):
  b1, a1 = c1
  b2, a2 = c2
  d0 = modPoly(b1 * b2)
  d1 = modPoly(a1 * b2 + a2 * b1)
  d2 = modPoly(a1 * a2)
  t0 = modPoly(d2 * ek[0])
  t1 = modPoly(d2 * ek[1])
  t0 = roundCoeff(t0 / g_.P)
  t1 = roundCoeff(t1 / g_.P)
  t0 = d0 + roundCoeff(t0)
  t1 = d1 + roundCoeff(t1)
  return (t0, t1)
```

`z1= [0.+8.j 1.+0.j]`と`z2= [5.+2.j 7.+6.j]`の暗号文`c1`, `c2`に対して`c1 + c2`や`c1 * c2 * c2`を計算してみました。

```shell
z1= [0.+8.j 1.+0.j]
z2= [5.+2.j 7.+6.j]
m1= 500.0 + 2475.0 x + 4000.0 x**2 + 3182.0 x**3  # ecd(z1)
m2= 6000.0 + 2121.0 x - 2000.0 x**2 + 3536.0 x**3 # ecd(z2)
dcd1= [7.55057011e-05+8.00010306e+00j 9.99924494e-01+1.03061172e-04j] # dcd(m1)
dcd2= [4.9994439+2.00010306j 7.0005561+6.00010306j]                                    # dcd(m2)
c1.0= 539.0 + 2404.0 x + 3697.0 x**2 + 2970.0 x**3  # enc(m1)
  .1= 15.0 - 126.0 x - 109.0 x**2 + 193.0 x**3
c2.0= 6288.0 + 2252.0 x - 2095.0 x**2 + 3316.0 x**3 # enc(m2)
  .1= 12.0 - 166.0 x - 68.0 x**2 - 54.0 x**3
dec c1= 497.0 + 2473.0 x + 4001.0 x**2 + 3190.0 x**3 # dec(c1)
dec c2= 6000.0 + 2118.0 x - 1995.0 x**2 + 3538.0 x**3 # dec(c2)
dcd c1= [-0.00999556+8.00534570e+00j  1.00399556+3.34570186e-03j] # dcd(dec(c1))
dcd c2= [4.99590837+2.00439595j 7.00409163+5.99439595j] # dcd(dec(c2))
add.0= 6827.0 + 4656.0 x + 1602.0 x**2 + 6286.0 x**3 # add(c1, c2)
   .1= 27.0 - 292.0 x - 177.0 x**2 + 139.0 x**3
dec= 6497.0 + 4591.0 x + 2006.0 x**2 + 6728.0 x**3
dcd= [4.98591281+10.00974166j 8.00808719 +5.99774166j] # dec(add(c1, c2))
org= [5.+10.j 8. +6.j] # z1 + z2
mul.0= -3531567.0 + 10190964.0 x + 17534228.0 x**2 + 23647268.0 x**3 # mul(c1, c2)
   .1= 732188.0 + 349472.0 x - 1873330.0 x**2 + 513572.0 x**3
dec= -4541853.0 + 8099018.0 x + 16966140.0 x**2 + 24438938.0 x**3
dcd= [-16.09592124+39.97394933j   7.01221524 +6.04166933j] # dec(mul(c1, c2))
org= [-16.+40.j   7. +6.j] # z1 * z2
c3.0= -7.16206987e+10 + (4.83031357e+10) x + (6.23488876e+10) x**2 + # c1 * c2 * c2
(1.54530147e+11) x**3
  .1= 3.98552924e+09 + (1.44165597e+10) x - (1.42138081e+10) x**2 -
(2.40068148e+09) x**3
dcd c3= [-160.53731976+167.44293205j   12.90006264 +84.3506978j ] # dec(c1 * c2 * c2)
org= [-160.+168.j   13. +84.j] # z1 * z2 * z2
```
誤差がありつつ計算できてるのが見えます。

## まとめ
雑な実装ではありますが、原理の理解の助けになれば幸いです。
