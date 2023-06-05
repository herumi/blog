---
title: "AVX-512によるvpermpsを用いたlog(x)の実装"
emoji: "🧮"
type: "tech"
topics: ["ASM", "AVX512", "log", "vpermps"]
published: true
---
## 初めに
今回はfloat配列に対する対数関数 $\log(x)$ のAVX-512による近似計算例を紹介します。

## log(x)の近似計算方法
まず $x$ を $2^n a$($n$ は整数 $1 \le a < 2$)の形に分解します。すると

$$ \log(x) = n \times \log(2) + \log(a).$$

よって $1 \le a < 2$ に対する $\log(a)$ を求めることに専念します。

$\log(1+x)$ のローラン展開は $x - x^2/2 + x^3/3 - x^4/4 + \cdots$ ですがこの級数の収束は遅いので、ここでは次のトリックを使います。

$b$ を $1/a$ の近似値とします。$c = ab-1$ と置くと $c$ は0に近くなります（$b\sim 1/a$ なので）。$a=(1+c)/b$ と変形し、対数を取ると

$$ \log(a) = \log(1+c) - \log(b).$$

もし $\log(b)$ を計算できるなら、$a$ よりもより0に近い $c$ について $\log(1+c)$ を求めればよいことになります。

ところで「$b$ を $1/a$ の近似値とする」とか「$\log(b)$ を計算できる」はなんとも怪しい仮定(X)に思います。これはテーブルを事前計算すれば可能です。節を改めて解説します。

## テーブルの作成

floatのバイナリ表現は

符号|指数部|仮数部
-|-|-
1|8|23

でした。今 $1 \le a < 2$ なので符号ビット $s$ は0、指数部 $e$ は127固定で、下位23ビットが $a$ を表現します。ここで $a$ の上位 $L$ より下位ビットを0にしたバイト表現を考えます。
正確には $a$ の仮数部 $m$ を $23-L$ ビット論理右シフトした値を $d$ とすると、$(127<<23)|(d<<(23-L))$ で表現される32ビットで表現されるfloat値 $u$ です。$a$ とは上位 $L$ ビットが同じなので差は $1/2^L$ 以下です。
$u$ に対する（floatとして）厳密な $1/u$ と $\log(1/u)$ のテーブルtbl1とtbl2を用意します。テーブルのインデックスは $d$ です。

![テーブル参照](/images/log-table.png =500x)
*テーブル参照*

$b = {\tt tbl1}[d]$ とすれば $c = ab-1$ は高々 $1/2^L$ 程度の大きさになります。${\tt tbl2}[d]$ が $\log(b)$ を与えます。これらのテーブル参照により仮定(X)が成り立ちます。
後は $L$ の大きさに応じて $\log(1+c)$ を計算すれば $\log(a)$ が求まります。

テーブルは次のようにして作成します。

```python
self.logTbl1 = []
self.logTbl2 = []
self.L = 4
LN = 1 << self.L
for i in range(LN):
  u = (127 << 23) | ((i*2+1) << (23 - self.L - 1))
  v = 1 / uint2float(u)
  v = uint2float(float2uint(v)) # enforce C float type instead of double
  # v = numpy.float32(v)
  self.logTbl1.append(v)
  self.logTbl2.append(math.log(v))
```
$u$ の計算を `i << (23 - self.L)`ではなく`(i*2+1) << (23 - self.L - 1)`としているのは、上図「テーブル参照」のように、$m$ の下位 $23-L$ ビットを0にすると、いつも切り捨てになってしまうので、`+(1 << (23 - L - 1))`して四捨五入の効果を狙うためです。
`v = uint2float(float2uint(v))`はPythonのfloat（つまりCのdouble）を正しくCのfloat値として扱うための処理です。
[Pythonでfloatの値をfloat32の値に変換する](https://zenn.dev/herumi/articles/float32-in-python#%E5%8E%9F%E5%9B%A0%E3%81%A8%E3%81%AA%E3%81%A3%E3%81%9F%E3%82%B3%E3%83%BC%E3%83%89)というちょっと変わった記事を書いたのは、このテーブルを正しく作成するためでした。

## vpermpsを用いたテーブル参照
AVXやAVX-512にはvgatherという複数のテーブル参照をまとめて行うSIMD命令があります。

```python
vgatherdd(out|k, ptr(rax + idx*4))
```
outとidxにSIMDレジスタを指定すると要素ごとにout[i] = *(rax + idx[i])となります。残念ながら、この命令はかなり遅く20clk以上かかります。したがって、出来るならこの命令は避けたいです。
そこで今回は $L=4$ でしか使えませんが、vpermpsを利用することにしました。`vpermps(x, y, z)`はZMMレジスタx, y, zに対してyの`z[i]`番目の要素`y[z[i]]`を`x[i]`設定する命令です（`y[i]`は0から15までの値です）。

```python
vpermps(x, y, z)
x[i] = y[z[i]]
```
vpermpsはメモリにアクセスせず、レジスタ間だけの処理で高速（レイテンシ3）です。


expのときと同様 $L=4$ として $\log(1+x)$ の多項式近似を求めて係数を設定します。今回は4次の多項式を使いました。

## AVX-512による実装
入力をv0としてtbl1, tbl2や1などをレジスタに置いて計算します。

```python
un(vgetexpps)(v1, v0) # n
un(vgetmantps)(v0, v0, 0) # a
un(vpsrad)(v2, v0, 23 - self.L) # d
un(vpermps)(v3, v2, self.tbl1) # b
un(vfmsub213ps)(v0, v3, self.one) # c = a * b - 1
un(vpermps)(v3, v2, self.tbl2) # log_b
un(vfmsub132ps)(v1, v3, ptr_b(rip+self.LOG2)) # z = n * log2 - log_b
```
`vgetexpps`は v0($x=2^n f$) の指数部$n$、`vgetmantps`は仮数部 $f$ を得る命令です。[AVX-512による最速のexp(x)を目指して](https://zenn.dev/herumi/articles/fast-exp-by-avx512)で紹介した`vrndscaleps`や`vreduceps`と同じく便利ですね。
テーブル参照として使う`vpermps`はインデックスで使うレジスタの下位4ビットだけを参照するので、`vpsrad`で右シフトした後マスクするする必要はありません。

## 1の付近での例外処理
上記アルゴリズムでほぼ完成なのですが、元々の入力値 $x$ が1に近いところで相対誤差の劣化が起こり得ます。$x \sim 1$ なら余計なテーブル参照をせずにローラン展開したほうがよいのです。
そのため、$|x-1| < 0.02$ ならテーブル参照を経由しない処理に移行します（0.02は適当に数値実験して決めました）。SIMDで分岐は御法度なのでマスクレジスタを使います。

```python
# precise log for small |x-1|
un(vsubps)(v2, keepX, self.one) # x-1
un(vandps)(v3, v2, ptr_b(rip+self.C_0x7fffffff)) # |x-1|
un(vcmpltps)(vk, v3, ptr_b(rip+self.BOUNDARY))
un(vmovaps)(zipOr(v0, vk), v2) # c = v0 = x-1
un(vxorps)(zipOr(v1, vk), v1, v1) # z = 0
```
$x$ から1を引き(vsubps)、`0x7fffffff`とアンドをとる(vandps)ことで絶対値 $|x-1|$ をえます。その値が `BOUNDARY`よりも小さければ(vcmpltps)マスクレジスタvkを設定します。
続く2行で、それまで計算していたテーブル引きの値v1, v2を御破算して $x-1$ と0を設定します。
全体のコードは[fmath](https://github.com/herumi/fmath/blob/master/gen_fmath.py)の`logCore`を参照ください。今回は必要な一時レジスタが多いためアンロール回数は4に設定しています。

## まとめ
AVX-512を使ったstd::logfの近似計算例を紹介しました。AVX-512にはvgetexppsやvgetmantpsなどの浮動小数点数の指数部や仮数部を得る便利な命令があります。
vpermpsは小さいテーブル参照として使えると便利です。
