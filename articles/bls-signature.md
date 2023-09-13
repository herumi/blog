---
title: "BLS署名の基本"
emoji: "📖"
type: "tech"
topics: ["BLS署名", "ペアリング", "ハッシュ関数"]
published: false
---
## 初めに
今回はBLS署名を紹介します。BLS署名はBoneh, Lynn, Shachamが提案したペアリングを用いた署名で、[EthereumのPoSの暗号プロトコル](https://eth2book.info/capella/part2/building_blocks/signatures/)などで利用されています。

## 数学用語の復習
素数 $p$ 上で定義された楕円曲線の0でない点を $P$ を1個とり、素数 $r$ 倍すると0になるとします。$rP=0$.
すると集合 $G_1:=\Set{0, P, 2P, \dots, (r-1)P}$ は位数 $r$ の加法巡回群です。
同様に、別の楕円曲線の0でない点で $r$ 倍すると0になる点 $Q$ を1個とり、
$G_2:=\Set{0, Q, 2Q, \dots, (r-1)Q}$ とします。$G_2$ も位数 $r$ の加法巡回群です。

$e : G_1 \times G_2 \rightarrow G_T$ をペアリングとします。
すなわち、$G_T$ は $e(P, Q)$ を生成元とする位数 $r$ の乗法巡回群で、
任意の整数 $a$, $b$ に対して $e(aP, bQ) = e(P, Q)^{ab}$ が成り立ちます。

ここで紹介した用語を初めて見る方は過去の記事を参照してください。
- 群, 加法群, 楕円曲線 : [群と楕円曲線とECDH鍵共有](https://zenn.dev/herumi/articles/group-ec-ecdh)
- 巡回群 : [楕円ElGamal暗号](https://zenn.dev/herumi/articles/elgamal-encryption)
- ペアリング : [ペアリングと内積計算可能なL2準同型暗号](https://zenn.dev/herumi/articles/pairing-l2he)

また任意の文字列から $G_1$, $G_2$ への暗号学的ハッシュ関数を $H_1 : \Set{文字列} \rightarrow G_1$, $H_2 : \Set{文字列} \rightarrow G_2$ とします。

## BLS署名の基本形
署名の一般的な説明については[署名の概要](https://zenn.dev/herumi/articles/sd202203-ecc-2#%E7%BD%B2%E5%90%8D%E3%81%AE%E6%A6%82%E8%A6%81)を参照ください。

### 鍵生成
有限体 $F_r := \Set{ 0, 1, 2, \dots, r-1}$ からランダムに要素 $s$ を一つ選び、$s$ を署名鍵（秘密鍵）、$pk:=s P \in G_1$ を検証鍵（公開鍵）とします。
署名鍵と検証鍵の作り方は、（楕円曲線の種類は異なりますが）ECDSAなどと同じです。

### 署名
文字列 $m$ に対して署名鍵 $s$ を使って $σ:=Sign(s,m):= s H_2(m)$ とします。$H_2$ は楕円曲線 $G_2$ へのハッシュ関数なので $σ$ も $G_2$ の要素です。

### 検証
検証鍵 $pk$ を持っている人は文字列 $m$ と署名 $σ \in G_2$ に対して、2個のペアリング $e(pk, H_2(m))$ と $e(P, σ)$ を計算し、それらが一致していれば受理、そうでなれければ拒絶します。

$$
e(pk, H_2(m)) == e(P, σ)
$$

## 正当性の確認
正しく作った署名が検証に通ることを確認します。構成法から $pk = s P$, $σ=s H_2(m)$ なので

$$
e(pk, H_2(m)) = e(s P, H_2(m)) = e(P, s H_2(m)) = e(P, σ)
$$
が成り立ちます。2番目の等号はペアリングの性質 $e(aP, Q) = e(P, aQ) = e(P, Q)^a$ により導かれます。

## 安全性
$G_1$ の離散対数問題が困難（→[楕円曲線暗号の安全性 DLPとCDHとDDH](https://zenn.dev/herumi/articles/ecc-dlp-cdh-ddh)）なら、検証鍵（公開鍵） $sP$ から署名鍵（秘密鍵） $s$ を求められません。
署名の偽造ができないことの説明はここでは省略しますが、楕円曲線としてBLS12-381曲線を使い、$H_2$ が安全なハッシュ関数なら偽造困難であることが知られています。
ここでBLS12-381曲線のBLSはBarreto, Lynn, Scottの略でBLS署名のBLSとは異なります。

また、「$H_2$ が安全なハッシュ関数なら」と書いている点にも注意してください。
文字列から楕円曲線 $G_2$ へのハッシュ関数を、たとえば $H : \Set{文字列} \rightarrow \text{SHA256}(m) Q \in G_2$ としてしまいたくなるかもしれません。しかし、このようなハッシュ関数は安全ではありません。
Ethereumでは[RFC 9380](https://www.ietf.org/rfc/rfc9380.html)で定義された非常に複雑な方法が使われています。そこで使われるDSTというパラメータはブロックチェーンのプロジェクトや製品によって異なることがあるので注意してください（私のGitHubでもよくissueに上がる）。
