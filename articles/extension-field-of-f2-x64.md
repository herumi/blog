---
title: "標数2の体のためのx64用SIMD命令"
emoji: "🧮"
type: "tech"
topics: ["F2", "拡大体", "AES", "AVX", "pclmulqdq"]
published: false
---
## 初めに

今回は標数2の体の実装に利用でいるx64用SIMD命令を紹介します。この記事は
- [暗号で使われる標数2の体](https://zenn.dev/herumi/articles/extension-field-of-f2)
- [暗号で使われる標数2の体の実装](https://zenn.dev/herumi/articles/extension-field-of-f2-impl)
の続きです。

## pclmulqdq
Packed Carry-Less MULtiplication Quadwordという命令です。"packed"は整数系SIMD, "carry-less multiplication"はcarryの無い乗算という意味です。SSE版だけでなくAVXやAVX-512版のvpclmulqdqもあります。
普通の整数乗算は計算途中で繰り上がりが発生します（cf. [多倍長整数の実装4（乗算の基礎）](https://zenn.dev/herumi/articles/bitint-04-mul)）。
しかし、標数2の体では加法は排他的論理和、乗法は論理積なので繰り上がりがありません。そのため"carry-less"とついています。具体的には[多項式の乗算](https://zenn.dev/herumi/articles/extension-field-of-f2-impl#%E5%A4%9A%E9%A0%85%E5%BC%8F%E3%81%AE%E4%B9%97%E7%AE%97)に現れた式を計算します。
すなわち、$a=\sum_{i=0}^{n-1} a_i X^i$, $b=\sum_{i=0}^{n-1} b_i X^i$ に対して $c=ab$ としたとき

$$
c_i = \begin{cases}
  \sum_{j=0}^i a_j b_{i-j}  & (i=0, \dots, n-1),\\
  \sum_{j=i-n+1}^{n-1} a_j b_{i-j} & (i=n, \dots, 2n-2).
\end{cases}
$$

命令の最後の"qdq"はqword（64ビット）の乗算でdouble qword（128ビット）の結果を得るという意味で、$n=64$ 固定です。

前回主に8次拡大体の実装方法を紹介しましたが、[$𝔽_2$​ の8次拡大体と128次拡大体](https://zenn.dev/herumi/articles/extension-field-of-f2#%E3%81%AE8%E6%AC%A1%E6%8B%A1%E5%A4%A7%E4%BD%93%E3%81%A8128%E6%AC%A1%E6%8B%A1%E5%A4%A7%E4%BD%93)で紹介したように、AES-GCMやXTS-AESでは128次拡大体が使われます。その乗算処理を高速化するために導入されました。
その詳細は[Intel Carry-Less Multiplication Instruction and its Usage for Computing the GCM Mode](https://www.intel.com/content/dam/develop/external/us/en/documents/clmul-wp-rev-2-02-2014-04-20.pdf)にあるので、ここでは簡単に紹介します。

### エリアス

## まとめ
