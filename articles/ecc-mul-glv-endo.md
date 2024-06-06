---
title: "楕円曲線暗号のための数学6（GLV法と自己準同型写像）"
emoji: "🧮"
type: "tech"
topics: ["楕円曲線暗号", "スカラー倍算", "MSM", "GLV"]
published: true
---
## 初めに
今回は、楕円曲線の数学的な性質を用いたスカラー倍算の高速化手法の一つ、GLV法（Gallant-Lambert-Vanstone）を紹介します。
GLV法は小さいマルチスカラー倍算を利用するので、まずそちらから解説しましょう。

## マルチスカラー倍算
一般に、楕円曲線の点 $P_1, \dots, P_n$, 整数 $a_1, \dots, a_n$ が与えられたときに

$$
Q=a_1 P_1 + \dots + a_n P_n
$$

を求める操作をマルチスカラー倍算 （MSM : multi-scalar multiplication）といいます。
zk-SNARKなどでは大きな $n$（数千~数万）に対するMSMが現れます。そのアルゴリズムはまたの機会に紹介するとして、ここでは $n=2$, つまり楕円曲線の点 $P_1$, $P_2$ と、整数 $x_1$, $x_2$ に対して $Q=x_1 P_1 + x_2 P_2$ を求める手法を考察します。
もちろん、$x_1 P_1$ と $x_2 P_2$ を求めて足せば $Q$ が求まりますが、それよりもよい方法があります。
$x_i P_i$ の計算は前回までの $w$ ビットの[NAF](https://zenn.dev/herumi/articles/ecc-mul-win-naf)など用いて計算するのですが、そのメインループは次の形でした。

```python
# return P * x
def mul(P, x, w):
  # w ビットNAFテーブル作成
  naf = make_NAF(x, w)
  tbl = make_table(P, w)
  Q = Ec()
  for v in range(naf):
    Q = dbl(Q)
    if v:
      Q = add(Q, tbl[v]) if v > 0 else sub(Q, tbl[-v])
  return Q
```

ループの中で $Q$ をdblしながらテーブル引きをしてadd (or sub) します。ここで $x_1 P_1$ と $x_2 P_2$ のループをそれぞれ回すのではなく、dblを共通化し、addだけをするのです。

```python
# return P1 * x1 + P2 * x2
def mul(P1, x1, P2, x2, w):
  # w ビットNAFテーブル作成
  naf1 = make_NAF(x1, w)
  naf2 = make_NAF(x2, w)
  tbl1 = make_table(P1, w)
  tbl2 = make_table(P2, w)
  Q = Ec()
  for i in range(len(naf1)):
    Q = dbl(Q)
    v = naf1[i]
    if v:
      Q = add(Q, tbl1[v]) if v > 0 else sub(Q, tbl1[-v])
    v = naf2[i]
    if v:
      Q = add(Q, tbl2[v]) if v > 0 else sub(Q, tbl2[-v])
  return Q
```

このようにすると、$x_i$ が $n$ ビット長なら個別に計算するとdblの回数が $2n$ だったのが、$n$ に減ります。
[ECDSAの検証](https://zenn.dev/herumi/articles/sd202203-ecc-2#ecdsa%E3%81%AE%E6%A4%9C%E8%A8%BC)では、`Q=P * u1 + S * u2` という処理があります。
実は、ここでもマルチスカラー倍算のテクニックが利用できます。

## GLV法のコアアイデア
楕円曲線が与えられたときに、
1. ある特殊な定数 $L$ があって、任意の楕円曲線の点 $P$ の $L$ 倍 $L P$ が高速に求めらる（algo1）
2. 任意の整数 $x$ に対して $x = a + b L$ となる整数 $a$, $b$ でビット長が $x$ の半分ぐらいのサイズになるものを見つけられる（algo2）

とします。
このとき、$Q=x P = (a+b L)P = aP + b (L P)$ となり、$L P$ を高速に求めた後、$n=2$ のマルチスカラー倍算を利用して $Q$ を求められます。
$a$, $b$ のビット長が $x$ の半分なのでdbl の回数が半分となり、高速化されるというものです。

とても面白いアイデアです。が、そのような都合のよいalgo1, algo2は見つかるのでしょうか。
以下では、特に曲線BLS12-381を念頭に説明します。

## BLS12-381曲線パラメータ
BLS12曲線と呼ばれる楕円曲線のファミリーは、
1. パラメータ $z$ をとり、
2. $r=z^4-z^2+1$,
3. $p=(z-1)^2 r / 3 + z$,

で定まる素数 $p$ 上の $y^2=x^3+4$で定まる楕円曲線（$a=0$, $b=4$）です。
$E(𝔽_p)$ の位数は素数 $r$, ペアリングの行き先が $𝔽_p$ の12次拡大体となります。

BLS12-381は `z= -0xd201000000010000` で
- `r=0x73eda753299d7d483339d80809a1d80553bda402fffe5bfeffffffff00000001`
- `p=0x1a0111ea397fe69a4b1ba7b6434bacd764774b84f38512bf6730d2a0f6b0f6241eabfffeb153ffffb9feffffffffaaab`

です。$p$ のビット長が381なのがBLS12-381の由来です。$r$ のビット長は255。

## 自己準同型写像
### $φ$ の定義
天下りですが、$w \in 𝔽_p$ を1の原始3乗根（$w^3=1$, $w \neq 1$）として、

$$
φ:E \ni (x, y) \mapsto (wx, y) \in E,\\
φ(0)=0
$$

という写像を考えます。$E$ の定義方程式が $y^2=x^3+4$ で $w^3=1$ なので $(wx)^3+4=w^3 x^3 + 4 = x^3+4=y^2$ が成り立ち、$φ(x,y) \in E$ です。

### $φ$ の準同型性
次に、楕円曲線の[加算公式](https://zenn.dev/herumi/articles/projective-coordinate#%E6%A5%95%E5%86%86%E6%9B%B2%E7%B7%9A%E3%81%AE%E5%AE%9A%E7%BE%A9)を思い出します。
$P=(x_1,y_1)$, $Q=(x_2,y_2)$ とします。$a=0$ なので
$x_1 \neq x_2$ のとき $L=(y_2-y_1)/(x_2-x_1)$,  $x_1 = x_2$ のとき $L=(3x_1^2)/(2 y_1)$ として
$x_3 = L^2-(x_1+x_2)$, $y_3=L(x_1-x_3)-y_1$.

$φ(P)=(w x_1, y_1)$, $φ(Q)=(w x_2,y_2)$ を使って $(x_3,y_3):=φ(P)+φ(Q)$ を考えます。
$x_1 \neq x_2$ のとき $L'=(y_2-y_1)/(w x_2-w x_1)=L/w$.
$x_1 = x_2$ のとき $L'=(3 (w x_1)^2)/(2 y_1) = w^2 L = L/w$.

したがって $x'_3=L'^2-(w x_1 + w x_2)=L^2/w^2- w(x_1 + x_2) = w (L^2/w^3 - (x_1+x_2)) = w x_3$.
$y'_3=L'(w x_1 - w x_3) - y_1=L(x_1-x_3)-y_1=y_3$.
よって $(x'_3, y'_3)=(w x_3, y_3) = φ(x_3, y_3)$.
これは $φ(P)+φ(Q)=φ(P+Q)$ を意味します。このような性質を準同型といい、$E$ から自分自身への写像なので自己準同型写像 (endomorphism) といいます。
注意 : 実は射（morphism） $φ : E \rightarrow E$ が $φ(0)=0$ を満たせば（isogenyといいます）自動的に準同型となることも知られています。

## 定数倍写像
前節で定義した $φ$ は面白い性質があります。それは $w \in 𝔽_p$ のとき、$φ$ は実は定数倍写像であるというものです。
つまり、ある定数 $L$ があって $φ(P) = L P$ と書けます（理由は今回はパス）。

定数 $L$ が具体的に何なのか求めてみましょう。
$φ(x,y)=(w x,y)$ なので $φ^2(x,y) = (w^2 x, y)$, $φ^3(x,y)=(w^3 x, y)=(x,y)$ です。つまり $φ^3$ は $E$ 上の恒等写像 $\text{id}_E$ です。
$φ(P)=L P$ だから $φ^3(P) = L^3 P = P$ でなければなりません。$P(\neq 0)$ は位数 $r$ の元なので $L^3 \equiv 1 \pmod{r}$ が成り立ちます。

ここで注意 : $w$ は $𝔽_p$ の元なので $w^3 \equiv 1 \pmod{p}$ であるのに対して $L$ は $\pmod{r}$ で考えてます。
$L^3-1=(L-1)(L^2+L+1)=0 \in 𝔽_p$ を解いて $L(\neq 1)$ を求めます（求め方はまたいずれ）。

- `w=0x1a0111ea397fe699ec02408663d4de85aa0d857d89759ad4897d29650fb85f9b409427eb4f49fffd8bfd00000000aaac`
- `L=0xac45a4010001a40200000000ffffffff`

となりました。実は $L$ は $z^2-1$ に等しく、$r=z^4-z^2+1 = z^2(z^2-1)+1=L(L+1)+1$ という関係が成り立っています。

注意 : $w$, $L$ は2次方程式の解なので、もう一つ解があり、それらは

- $w'=-1-w$=`0x5f19672fdf76ce51ba69c6076a0f77eaddb3a93be6f89688de17d813620a00022e01fffffffefffe`
- $L'=-L-1$=`0x73eda753299d7d483339d80809a1d804a7780001fffcb7fcfffffffe00000001`

です（今回はこちらは使わない）。

話をまとめると、$w^2+w+1=0 \in 𝔽_p$ の解の一つを使って $φ : E \ni (x, y)=(wx, y) \in E$ とすると、$φ(P) = L P$, ただし $L=z^2-1$, $r=L^2+L+1$ となるのです。

## mcl-wasmで確認する
[mcl-wasm](https://www.npmjs.com/package/mcl-wasm)を使って確認しましょう。
次の `test.js` というファイルを作ります。

```js
const mcl = require('mcl-wasm');

(async () => {
  await mcl.init(mcl.BLS12_381) // BLS12-381曲線で初期化
  const P = mcl.hashAndMapToG1("abc") // 適当な楕円曲線の点を作る
  P.normalize() // アフィン座標に変換
  console.log(`P.x=${P.getX().getStr(16)}`) // Pの(x, y)座標を表示
  console.log(`P.y=${P.getY().getStr(16)}`)
  const L = new mcl.Fr()
  L.setStr('0xac45a4010001a40200000000ffffffff') // L を設定
  console.log(`L=${L.getStr(16)}`)
  const Q = mcl.mul(P, L) // Q = P * L
  Q.normalize()
  console.log(`Q.x=${Q.getX().getStr(16)}`) // Qの(x, y)座標を表示
  console.log(`Q.y=${Q.getY().getStr(16)}`)
  const w = mcl.div(Q.getX(), P.getX()) // w = Q.x / P.x
  console.log(`w=${w.getStr(16)}`)
})()
```


```bash
mkdir work
cd work
npm install -D mcl-wasm
node test.js
```

で
```
P.x=16379044479e745e2a14731fee35274308addf04b9e2cd0ef838069689333c4460054b518cc1ec87fce8b9144262e206
P.y=168c083fc11839bd7ef7e655364f65000b6c3e0bf41c60b835a2dd9991aabeefc10b943c0469f211632c62ddf706ee54
L=ac45a4010001a40200000000ffffffff
Q.x=15dd3a9d916f38f45f05dfdce5a795782cffb502859edfd7414f24f474a378f86b8ab2da8becb4ba2e57a0a032b00046
Q.y=168c083fc11839bd7ef7e655364f65000b6c3e0bf41c60b835a2dd9991aabeefc10b943c0469f211632c62ddf706ee54
w=1a0111ea397fe699ec02408663d4de85aa0d857d89759ad4897d29650fb85f9b409427eb4f49fffd8bfd00000000aaac
```
という結果になりました。$P$ を $L$ 倍した $Q$ の $y$ 座標 $Q.y$ は $P.y$ と同じで $Q$ の $x$ 座標は $P.x$ の $w$ 倍されていることを確認できました。
他の $P$ でも同様の結果になります。
これでGLV法のコアアイデアのalgo1は解決しました。次にalgo2に移ります。

## $x$ の分解
$r$ は255ビット、$L$ は128ビットです。
$x \in [0, r-1]$ に対して $x$ を $L$ で割った余りを $a$ ($0 \le a < L$)、商を $b$ とすると

$$
x = a + b L
$$

これを `(a, b) = split(x)` と書くことにします。

$r = L^2+L+1=(L+1)L+1$ なので $0 \le b \le L+1$ です。
$L+1$ もまだ128ビットで表現できるので、最大約255ビットの整数 $x$ を最大128ビットの整数 $a$, $b$ を使って $x=a + b L$ と表せました。
これでalgo2も解決です。

## GLV法のまとめ
BLS12-381曲線について今までのパラメータを使ってPythonで書くと

```python
def mul(P, x):
  (a, b) = split(x)

  naf1 = make_NAF(a, w)
  naf2 = make_NAF(b, w)
  tbl1 = make_table(P, w)
  tbl2 = []
  for v in tbl1:
    tbl2.append(φ(v)) # L 倍する
  # a P + b (L P) をMSMする
  # このページの2個目のMSMと同じ
  return Q
```

`tbl2` は楕円曲線加算 `add` を使って `make_table` するよりも、作った `tbl1` の各要素に $φ$ を適用する方が軽い（$𝔽_p$ の乗算1回ずつ）です。
最初に述べたように、`dbl` の回数が256回から128回と半減しました。今まで紹介したウィンドウ法やその改善、NAFでは `dbl` の回数は減らせなかったのでこれは大きな改善です。
