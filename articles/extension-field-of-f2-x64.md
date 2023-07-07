---
title: "標数2の体のためのx64用SIMD命令"
emoji: "🧮"
type: "tech"
topics: ["F2", "拡大体", "AES", "PCLMULQDQ", "GFNI"]
published: true
---
## 初めに

今回は標数2の体の実装に利用でいるx64用SIMD命令を紹介します。この記事は
- [暗号で使われる標数2の体](https://zenn.dev/herumi/articles/extension-field-of-f2)
- [暗号で使われる標数2の体の実装](https://zenn.dev/herumi/articles/extension-field-of-f2-impl)
の続きです。AES専用命令AES-NIは今回ターゲットから外れるためまたの機会とします。

## PCLMULQDQ
Packed Carry-Less MULtiplication Quadwordという命令です。"packed"は整数系SIMD, "carry-less multiplication"はcarryの無い乗算という意味です。SSE版だけでなくAVXやAVX-512版のvpclmulqdqもあります。
普通の整数乗算は計算途中で繰り上がりが発生します（cf. [多倍長整数の実装4（乗算の基礎）](https://zenn.dev/herumi/articles/bitint-04-mul)）。
しかし、標数2の体では加法は排他的論理和、乗法は論理積なので繰り上がりがありません。そのため"carry-less"とついています。具体的には[多項式の乗算](https://zenn.dev/herumi/articles/extension-field-of-f2-impl#%E5%A4%9A%E9%A0%85%E5%BC%8F%E3%81%AE%E4%B9%97%E7%AE%97)に現れた式を計算します。
すなわち、 $a=\sum_{i=0}^{n-1} a_i X^i$, $b=\sum_{i=0}^{n-1} b_i X^i$ に対して $c=ab$ としたとき

$$
c_i = \begin{cases}
  \sum_{j=0}^i a_j b_{i-j}  & (i=0, \dots, n-1),\\
  \sum_{j=i-n+1}^{n-1} a_j b_{i-j} & (i=n, \dots, 2n-2).
\end{cases}
$$

命令の最後の"qdq"はqword（64ビット）の乗算でdouble qword（128ビット）の結果を得るという意味で、$n=64$ 固定です。

前回主に8次拡大体の実装方法を紹介しましたが、[$𝔽_2$​ の8次拡大体と128次拡大体](https://zenn.dev/herumi/articles/extension-field-of-f2#%E3%81%AE8%E6%AC%A1%E6%8B%A1%E5%A4%A7%E4%BD%93%E3%81%A8128%E6%AC%A1%E6%8B%A1%E5%A4%A7%E4%BD%93)で紹介したように、AES-GCMやXTS-AESでは128次拡大体が使われます。その乗算処理を高速化するために導入されました。
その詳細は[Intel Carry-Less Multiplication Instruction and its Usage for Computing the GCM Mode](https://www.intel.com/content/dam/develop/external/us/en/documents/clmul-wp-rev-2-02-2014-04-20.pdf)にあるので、ここでは簡単に紹介します。

### 128次多項式の乗算
(v)pclmulqdqは正確には vpclmulqdq(z, x, y, imm) という4個の引数をとります。
x, yは128ビット単位のSIMDレジスタxL, yL, xH, yHと書くとそれぞれx, yの下位64ビット、上位64ビットを表すことにします。immの値に応じて{xH,xL}×{yH,yL}の4通り選択でき、名前のエリアスがつけられています。
imm|エリアス名|x|y
-|-|-|-
0x00|vpclmullqlqdq|xL|yL
0x10|vpclmulhqlqdq|xH|yL
0x01|vpclmullqhqdq|xL|yH
0x11|vpclmulhqhqdq|xH|yH

128次同士の多項式の乗算はこれらの64次多項式乗算を使って実現します。繰り上がりが無いので難しくはありません。
xL yL, xL yH, xH yL, xH yHの乗算を計算し、それぞれ重なっている部分で排他的論理和をとればよいです。
```
xy = ([xH yH] << 128) ^ ([xH yL] << 64) ^ ([xL yH] << 64) ^ [xL yL]
```
![pclmulqdq](/images/mulqdq.drawio.png)

この後の $K=𝔽_{2^{128}}$​ におけるmod演算は簡単なビットシフトと排他的論理和をとります。詳細は前述のIntelのPDFをご覧ください。

## GFNI命令
8次拡大体 $K=𝔽_{2^8}$​ について、乗算はそのものずばりのvgf2p8mulbという命令があります。"Galios Field of 2 to the Power 8"の略でしょうか。
cpuidでGFNIが有効だと使えます。AVX-512として使う場合にはGFNIとAVX512Fの両方が有効でないと使えないことに注意してください。
この命令はSIMDの8ビット要素ごとに $K$ での乗算をします。つまり[暗号で使われる標数2の体の実装](https://zenn.dev/herumi/articles/extension-field-of-f2-impl)の計算をAVX-512なら512/8 = 64個同時にします。IceLakeによるとレイテンシ3のスループットが0.5~1と、単純な実装より1, 2桁速いです。
逆数を求める命令gf2p8affineinvqbもあります。やたら長い名前ですね。調べた限りではx64命令の中で一番長かったです。単に逆元(inverse)を求めるだけでなくAffine変換も入ってます。

```python
gf2p8affineinvqb(z, x, y, imm8) # z ← inv(x) * y + im8
```
xの逆数inv(x)に行列yを掛けてimm8を足します。「行列を掛けて定数を足す」部分がAffine変換です。
より正確にはyは64ビット単位のSIMDで64ビットを8x8のビット行列Yとみなし、8ビット単位のxをサイズ8の行列とみなして「Yx+imm8」を計算するのです。計算における積や和は、やはり論理積や排他的論理和であることに注意してください。
単に逆数を求めたいならYは単位行列を設定し、imm8=0として呼び出せばよいです。
単位行列は64ビット整数で表すと0x0102040810204080です。

```
10000000 // 0x80
01000000 // 0x40
00100000 // 0x20
00010000 // 0x10
00001000 // 0x08
00000100 // 0x04
00000010 // 0x02
00000001 // 0x01
```
呼び出し例（あくまでサンプル）
```python
  makeLabel('matrixI')
  dq_(0x0102040810204080)

  # 256/8=32個のbyte配列x[32]の逆元を求めてy[32]に保存する
  # gf256_inv_gfni(uint8_t y[32], const uint8_t x[32]);
  with FuncProc('gf256_inv_gfni'):
    with StackFrame(2, vNum=2, vType=T_YMM) as sf:
      py = sf.p[0]
      px = sf.p[1]
      vmovups(ymm0, ptr(px))
      vbroadcastsd(ymm1, ptr(rip+'matrixI'))
      # ymm0 * matrixI + 0
      vgf2p8affineinvqb(ymm0, ymm0, ymm1, 0)
      vmovups(ptr(py), ymm0)
```

逆数のAffine変換ではなくxのAffine変換のみを求める命令gf2p8affineqbもあります。

```python
gf2p8affineqb(z, x, y, imm8) # z ← x * y + imm8
```
こちらの面白い使い方についてはまたいずれの機会に。

## まとめ
標数2の拡大体の演算を高速化するためのPCLMULQDQとGFNI命令を紹介しました。暗号用途に追加された命令群ではありますが、それ以外の使い道が無いか考えてみると面白いかもしれません。
