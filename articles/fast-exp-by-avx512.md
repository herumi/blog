---
title: "AVX-512による最速のexp(x)を目指して"
emoji: "🧮"
type: "tech"
topics: ["ASM", "AVX512", "exp"]
published: true
---
## 初めに
ここではfloat配列に対する指数関数exp(x)のAVX-512による近似計算例を紹介します。

## exp(x)の近似計算方法
まず $\exp(x) = e^x$ を2べきの形に変形します。

$$ e^x = 2^{x \log_2(e)} = 2^y.$$

ここで $y=x \times \log_2(e)$ としました。次に $y$ を整数 $n$ と小数部分 $a$ ($|a|\le 1/2$)の和に分解します。

$$ y = n + a.$$

すると $\exp(x) = 2^n \times 2^a$ となります。$2^n$ は整数での計算なので残りの $2^a$ に焦点を当てます。
以前は一度 $2^a = e^{a \log(2)}$ と底の変換をしてから計算していたのですが、直接 $2^a$ のまま計算する方が乗算を1回減らせてよいです。

$2^a$ をマクローリン展開します。

$$ 2^a = 1 + c_1 a + c_2 a^2 + c_3 a^3 + \cdots.$$

$|a| \le 1/2$ を考慮してfloatの精度`1b-23`= $2^{-23}$ に近い値を出すには何次まで求めればよいかを考えます。

[Sollya](https://www.sollya.org/)という近似計算のためのソフトを利用します。`guessdegree`を使って $2^x$ を区間`[-0.5, 0.5]`で精度 `1b-23`を得るための次数を求めます。
プロンプトで

```shell
> guessdegree(2^x,[-0.5,0.5],1b-23);
[5;5]
```
とします。 5次まで求めるとよいようです。
次に近似式を求めます。`fpminimax`は引数にかなり癖があってマニュアルを読んでもよく分かりません。とりあえず次のようにしたらうまく出来ました。

```shell
fpminimax(2^x,[|1,2,3,4,5|],[|D...|],[-0.5,0.5],1);
Warning: For at least 5 of the constants displayed in decimal, rounding has happened.
1 + x * (0.69314697759916432673321651236619800329208374023437
   + x * (0.24022242085378028852993281816452508792281150817871
   + x * (5.5507337432541360711102385039339424110949039459229e-2
   + x * (9.6715126395259202324306002651610469911247491836548e-3
   + x * 1.326472719636653634089906717008489067666232585907e-3))))
```
それぞれの係数を配列`float c[6] = {1, 0.6931, 0.24022, ... }`として利用します。

$$ 2^a = 1 + c[1] a + c[2] a^2 + c[3] a^3 + c[4] a^4 + c[5] a^5. $$

最終的に次のステップでexp(x)を計算します。

1. $y \leftarrow x \times \log_2(e).$
1. $n \leftarrow {\tt round}(y).$  ここで ${\tt round}$ は四捨五入関数。
1. $a \leftarrow y - n.$
1. $w=2^a \leftarrow 1 + a(c[1] + a(c[2] + a(c[3] + a(c[4] + a c[5])))).$
1. $z \leftarrow 2^n.$
1. return $zw$.

## AVX-512による実装

前節の方針にしたがってAVX-512による実装を行います。

### $2^n x$ の計算
順序が前後しますが、先にステップ5とステップ6をまとめた $2^n x \leftarrow (n, x)$ の処理の解説をします。
AVX2までは次の方法をとっていました。（非負）整数 $n$ に対する $2^n$ は結果が整数型なら`1 << n`ですが、ここで必要なのはfloat型なのでちょっとしたビット演算をします。

floatのビット表現（符号s : 1ビット、指数部e : 8ビット、仮数部f : 23ビット）

floatのビット表現|符号s|指数部e|仮数部f
-|-|-|-
ビット長|1|8|23

に合わせて`(n + 127) << 23`を求めてそれをfloat値として扱うのです。しかし、AVX-512では`vscalefps`があるのでそれを使って直接計算できます。

$$ {\tt vscalefps}(n, x) = 2^n \times x.$$

しかもレイテンシ4と乗算と同じコストでできます。とても便利ですね。ただ $n$ はint型ではなくfloat型であることに注意です。

### 四捨五入
続いてステップの最初の四捨五入に戻ります。
SSE時代からある`cvtps2dq`（float→int変換）はAVX-512で拡張されて丸めモードを指定できます。しかし、結果はint型なので、小数部を求めたり前述の`vscalefps`に渡すためにはfloat型に戻さなければなりません。
SSE4(AVX)で登場した`vroundps`は結果をfloatの型として受け取れますが、AVX-512には拡張されていません。
そこでAVX-512では`vrndscaleps`を使います。これは $x$ の他に4ビットの $M$ と丸めモードctlを引数にとり、

$$ {\tt ROUND}(x) = 2^{-M} {\tt round}(x \times 2^M, {\tt ctl})$$

を求める、ちょっと変わった命令です。$M=0$, ctl=nearest even integerとして使います。

が、今回は`vreduceps`を使うことにしました。これは小数部

$$ x - {\tt ROUND}(x)$$

を直接求める命令です。つまり、上記ステップ3の小数部 $a = y - n$ を先に求めるのです。$n$ は $n=y-a$ として後で求めます。

- $n \leftarrow {\tt vrndscaleps}(y).$
- $a \leftarrow y - n.$

を

- $a \leftarrow {\tt vreduceps}(y).$
- $n \leftarrow y - a.$

とする。$a$ は続く重たい処理であるローラン展開の計算に必要なので先に求められるならその方が都合がよいです。
加えて、レイテンシは`vrndscaleps`が8clkなのに対して`vreduceps`だと4clkです。より速くローラン展開を開始できます（マニュアルを何度も眺めていて気がついた）。

### ローラン展開

この多項式の計算は前回[AVX-512のFMAを用いた多項式の評価](https://zenn.dev/herumi/articles/poly-evaluation-by-fma)で紹介したFMAを使います。

### 実装例

`v0`が入力値を表すzmmレジスタ、`v1`, `v2`は一時レジスタ、`self.log2_e`や`self.expCoeff[]`は定数を格納しているレジスタとします。
```python
vmulps(v0, v0, self.log2_e)
vreduceps(v1, v0, 0) # a = x - n
vsubps(v0, v0, v1) # n = x - a = round(x)

vmovaps(v2, self.expCoeff[5])
for i in range(4, -1, -1):
  vfmadd213ps(v2, v1, self.expCoeff[i])
vscalefps(v0, v2, v0) # v2 * 2^n
```

## ループアンロール
ここは純粋にPythonによる話になります。
ループアンロールするにはそれぞれの命令に対して必要なレジスタをいくつか用意し、繰り返し並べる必要があります。
たとえば`v0 = [zmm0, zmm1, zmm2]`, `v1=[zmm3, zmm4, zmm5]`, `v2=[zmm6, zmm7, zmm8]`のとき、

```python
Unroll(3, vaddps, v0, v1, v2)
```

と書くと

```python
vaddps(zmm0, zmm3, zmm6)
vaddps(zmm1, zmm4, zmm7)
vaddps(zmm2, zmm5, zmm8)
```
となって欲しいです。引数がアドレスだったら、

```python
# Unroll(2, vaddps, [zmm0, zmm1], [zmm2, zmm3], ptr(rax))
vaddps(zmm0, zmm3, ptr(rax))
vaddps(zmm1, zmm2, ptr(rax+64))
```
のようにオフセットがずれて欲しいです。また多項式の計算では引数の一部が配列ではない（定数なので）ときもあります。

これらのことを考慮して

```python
def Unroll(n, op, *args, addrOffset=None):
  xs = list(args)
  for i in range(n):
    ys = []
    for e in xs:
      if isinstance(e, list): # 引数が配列ならi番目を利用する
        ys.append(e[i])
      elif isinstance(e, Address): # 引数がアドレスなら
        if addrOffset == None:
          if e.broadcast:
            addrOffset = 0 # broadcastモードならオフセット0
          else:
            addrOffset = SIMD_BYTE # そうでないときはSIMDのサイズずらす(addrOffsetで細かい制御はできる)
        ys.append(e + addrOffset*i)
      else:
        ys.append(e)
    op(*ys)
```

という関数を作ってみました。そしてアンロール回数を毎回書かずに、また一斉置換しやすいように次のヘルパー関数を用意しました。

```python
def genUnrollFunc(n):
  """
    return a function takes op and outputs a function that takes *args and outputs n unrolled op
  """
  def fn(op, addrOffset=None):
    def gn(*args):
      Unroll(n, op, *args, addrOffset=addrOffset)
    return gn
  return fn
```
命令オペランドを引数にとり、Unrollするための関数を返す関数です。これらを使うと前節のAVX-512のコードは次のように書けます。

```python
un = genUnrollFunc(n) # アンロール回数を指定する
un(vmulps)(v0, v0, self.log2_e)
un(vreduceps)(v1, v0, 0) # a = x - n
un(vsubps)(v0, v0, v1) # n = x - a = round(x)

un(vmovaps)(v2, self.expCoeff[5])
for i in range(4, -1, -1):
  un(vfmadd213ps)(v2, v1, self.expCoeff[i])
un(vscalefps)(v0, v2, v0) # v2 * 2^n
```

元のASMのオペコードopをun(op)に置換しただけです。。`v0`などはアンロールしたいだけのレジスタを割り当てておきます。C++の場合は命令ごとに型が異なって変なマクロや、マクロを使わないならトリッキーなtemplateが必要でしたが、Pythonだと自由度が高いので便利ですね。

## ベンチマーク
今回計算中に必要な定数は6個、exp一つあたりに必要なレジスタは3個なので、8回アンロールしても $8 \times 3 +6 = 30$ で32個に収まります。
アンロール回数を1から8まで変更しながら測定してみました（Xeon Platinum 8280 Turbo Boost off）。

アンロール回数|1|2|3|4|5|6|7|8
-|-|-|-|-|-|-|-|-
allreg|17.91|15.89|14.14|13.85|13.68|13.08|13.03|13.78
allmem|18.06|16.21|14.82|14.37|14.54|14.61|14.66|16.19

allregが全てレジスタに載せた状態だとN=7が最も速かったです。allmemは[ptr_bを使う](https://zenn.dev/herumi/articles/poly-evaluation-by-fma#ptr_b%E3%82%92%E4%BD%BF%E3%81%86)バージョン。こちらはN=4が最速でした。
allregでN=8が遅くなっている要因の一つはコードが肥大化し過ぎたせいかなと思います。全体のコードは[fmath](https://github.com/herumi/fmath)の[gen_fmath.py](https://github.com/herumi/fmath/blob/master/gen_fmath.py)です。

## まとめ
AVX-512を使ったstd::expfの近似計算例を紹介しました。小数部を求める`vreduceps`がポイントです。今回[s_xbyak](https://github.com/herumi/s_xbyak)を使ってみて、かなり便利だと思いました。
