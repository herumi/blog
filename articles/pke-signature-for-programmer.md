---
title: "プログラマのための公開鍵による暗号化と署名の話"
emoji: "📖"
type: "tech"
topics: ["公開鍵暗号", "PKE", "署名"]
published: true
---
## 初めに
公開鍵による暗号化と署名をプログラマ向け(?)に書いてみました。ちまたによくある暗号化と署名の話はインタフェースと実装がごちゃまぜになっていることが分かり、暗号化と署名の理解が進めば幸いです（と思って書いたけど、余計分からんといわれたらすんません）。登場する言語は架空ですが、多分容易に理解できると思います。

## 公開鍵による暗号化PKE
早速、公開鍵による暗号化(PKE : Public Key Encryption)を紹介します。登場するのは暗号化したいデータのクラス`PlainText`, 暗号文クラス`CipherText`, 秘密鍵クラス`PrivateKey`と公開鍵クラス`PublicKey`です。PKEは次の3個のインタフェースを提供しています。

```js
abstract class PKE {
  abstract keyGenerator(): [PrivateKey, PublicKey];
  abstract encrypt(p : PublicKey, m : PlainText): CipherText;
  abstract decrypt(s : PrivteKey, c : CipherText): PlainText;
}
```

### 鍵生成
関数`keyGenerator()`は秘密鍵`s`と公開鍵`p`のペアを生成します。入力としてセキュリティパラメータなどのパラメータをとることはありますが、ここでは省略します。
`keyGenerator()`は乱数を使って鍵のペアを生成するので毎回異なる鍵を生成します。秘密鍵`s`は他人に見せてはいけませんが、公開鍵`p`は、`p`から`s`の情報は得られないため誰に見せても構いません。

### 暗号化
関数`encrypt(p, m)`は公開鍵`p`と平文`m`をとり、暗号文`c`を返します。公開鍵`p`は暗号化に使う鍵なので暗号（化）鍵ともいいます。安全な`encrypt`は乱数を使って毎回異なる暗号文を生成し、秘密鍵`s`を知らない人は暗号文`c`から元の平文`m`の情報は何も得られません。

### 復号
関数`decrypt(s, c)`は秘密鍵`s`と暗号文`c`をとり、平文`m`を返します。`decrypt`は復号失敗を返すことがある方式もありますが、ここでは簡単にするためいつでも`PlainText`を返すことにします。

### 正当性
`keyGenerator()`で作られた公開鍵のペア(`s`, `p`)を使って平文`m`を暗号化して復号すると元の平文に戻ります。つまり

```js
const [s, p] = PKE.keyGenerator();
assert(PKE.decrypt(s, PKE.encrypt(p, m)) === m);
```
assertの中は常に`true`です。

### 最低限の理解
公開鍵による暗号化PKEで理解すべきは上で紹介した3個のAPIとその性質だけです。どんな数学的背景を使ってこれらのAPIを実現しているのか、暗号文クラス`CipherText`の形、APIの実装詳細などを知る必要はありません（利用者の視点で理解する）。理解したくなったら、それぞれの暗号化方式のドキュメントに挑戦すればよいのです（実装者の視点）。

### インタフェースを越える説明に注意
`PrivateKey`クラスと`PublicKey`クラスは一般に同じとは限りません。また、もちろん平文クラス`PlainText`と暗号文クラス`CipherText`クラスも一般には異なります。「秘密鍵で暗号化」しようとしても`encrypt`には`PublicKey`と`PlainText`のインスタンスしか渡せないので型エラーになります。「公開鍵で復号」も同様にエラー。
したがって、一般的なPKEの解説で「秘密鍵で暗号化」という文章が出てきたら、それは間違いか、意図せず何か個別の公開鍵暗号方式（恐らくRSA暗号）の話をしていることになります。標準的なブラウザアプリケーションを設計・実装しようとしているときに、断り無く一部の特別なブラウザ拡張の関数を使った実装をされたら困りますよね。
一般論の話をしているのか、個別の話をしているのか明確な切り分けとその理解が必要です。

## 署名
署名もPKEと同様3個のインタフェースを提供しています。登場人物は署名鍵クラス`PrivateKey`、検証鍵クラス`PublicKey`、署名`Signature`、任意のバイト列クラス`Uint8Array`、trueかfalseを保持する二値クラス`boolean`です。

```js
abstract class SIGN {
  abstract keyGenerator(): [PrivateKey, PublicKey];
  abstract sign(s: PrivateKey, m : Uint8Array): Signature;
  abstract verify(p : PublicKey, m : Uint8Array, σ : Signature): boolean;
}
```

### 鍵生成
関数`keyGenerator()`は秘密鍵`s`と公開鍵`p`のペアを生成します。秘密鍵`s`は他人に見せてはいけません。署名するときに使うので署名鍵ともいいます。公開鍵`p`は署名の検証に使うので検証鍵ともいいます。PKEと同様`p`は誰に見せても構いません。
`keyGenerator()`, `PrivateKey`, `PublicKey`はPKEと同じ名前ですが、こちらは署名用なので同じ型とは限りません。

### 署名と3種類の意味
関数`sign(s, m)`は署名鍵`s`と任意のバイト列`m`をとり、署名クラス`Signature`のインスタンス`σ`を返します。
インタフェースを与える抽象クラスSIGNとしての「署名(1)」と、署名するというメソッド名（sign : 動詞）の「署名(2)」、戻り値の型名（signature : 名詞）である「署名(3)」と異なる用途に全て「署名」という用語がついているため大変混乱しやすいです。ここは、そうなってしまってるので慣れるしかありません。

1. 署名(1) SIGN : 3個のインタフェースを持つ抽象クラス。
2. 署名(2) `sign()` : 署名(1)のメソッドの一つ。署名を生成する動作を表す。
3. 署名(3) `Signature` : 署名(2)の結果得られるデータ。

### 検証
関数`verify(p, m, σ)`は検証鍵`p`とバイト列`m`と署名`σ`をとり、検証にパスすればtrue、そうでなければfalseを返します。

### 正当性
署名鍵を持つ人が作った署名はいつでも検証を通ります。つまり

```js
const [s, p] = SIGN.keyGenerator();
assert(SIGN.verify(p, m, SIGN.sign(s, m)));
```
assertの中は常に`true`です。

### 偽造不可能性
攻撃者が、過去に署名者が署名したデータとその署名のペア($m_i$,  $σ_i$)をいくら入手しても、そこから署名者が署名したことの無いデータ`m`（$\neq m_i$ for all $i$）に対する検証を通るような署名`σ`は作れないことが求められます。
逆に言えば、検証にパスした`σ`は署名者が作ったものと保証され、否認防止に使えます（署名鍵が漏洩しない限りは）。

### 最低限の理解とインタフェースを越える説明に注意その2
公開鍵による暗号化PKEと同様、署名は上記3個のメソッドと偽造不可能性などの性質の理解が重要です。中身がどうなっているかはブラックボックスで構わないのでPKEもハッシュ関数も不要です。個別の署名アルゴリズムの詳細を知りたくなったときに学べばよいのです。
一般的な署名(1)の解説をしているときに、PKEの、しかも非標準なメソッドを使って説明(e.g., 「ハッシュ値を秘密鍵で暗号化したもの」)していたら、それはやはりおかしいわけです。

## 署名の具体的なアルゴリズムとRSA署名の特殊性
ここまで抽象的な署名の話をしてきました。じゃあ、具体的にはどうなってるの?と思われた方は、たとえば
- [楕円曲線暗号のPythonによる実装その2（楕円曲線とECDSA）](https://zenn.dev/herumi/articles/sd202203-ecc-2)
- [RSA署名を正しく理解する](https://zenn.dev/herumi/articles/rsa-signature)
- [BLS署名の基本](https://zenn.dev/herumi/articles/bls-signature)

などをごらんください。

なお最後に、この記事で何度か出てきた「秘密鍵で暗号化」について。

RSA暗号のコアでは、落とし戸つき一方向性関数$f(x, y) :=y^x \bmod{n}$ （記号の詳細は上記[RSA署名を正しく理解する](https://zenn.dev/herumi/articles/rsa-signature)参照）を使って

- $\text{RSA-Enc}(e, m):=f(e, m)$ : $e$は公開鍵, $m$は平文
- $\text{RSA-Dec}(d, c):=f(d, c)$ : $d$は秘密鍵, $c$は暗号文
- $\text{RSA-Dec}(d, \text{RSA-Enc}(e, m)) = m$.

が使われます。そしてRSA暗号の極めて特殊な性質なのですが、RSA-EncとRSA-Decが同じ関数$f(x, y)$で表され、公開鍵と秘密鍵はどちらも1以上$n$未満の整数で同じ、平文クラス`CipherText`、暗号文クラス`CipherText`、署名対象の空間、署名値はどちらも0以上$n$未満の整数で同じなのです。署名の場合は$e$と$d$を入れ換えて([https://www.rfc-editor.org/rfc/rfc8017#section-5.2](https://www.rfc-editor.org/rfc/rfc8017#section-5.2))

- $\text{RSA-SP}(d, m):=f(d, m)$ : SP = Signature Primitive(署名プリミティブ)
- $\text{RSA-VP}(e, c):=f(e, c)$ : VP = Verification Primitive(検証プリミティブ)
- $\text{RSA-VP}(e, \text{RSA-SP}(d, m)) = m$.

と書かれています。つまり形に着目すると次の2パターンに分類できます。

- 秘密鍵$d$を使う : RSA-SP($m^d \bmod{n}$), RSA-Dec($c^d \bmod{n}$)
- 公開鍵$e$を使う : RSA-VP($c^e \bmod{n}$), RSA-Enc($m^e \bmod{n}$)

あるいは

- $m$に対する操作 : RSA-SP($m^d \bmod{n}$), RSA-Enc($m^e \bmod{n}$)
- $c$に対する操作 : RSA-VP($c^e \bmod{n}$), RSA-Dec($c^d \bmod{n}$)

式の形は同じなので「RSA-SP」のことを秘密鍵を使うことを意識して「秘密鍵で$m$を復号」、あるいは$m$に対する操作であることを意識して 「$d$で$m$を暗号化」というのはありだと思います。（厳密には秘密鍵を使うバージョンは実装上、公開鍵のみのバージョンより最適な手法がとれるので違うといえば違うのですが、ここでは省略します）。「暗号化」や「復号」が嫌なら「操作」「変換」でも構いませんが、個人的には大差ないと思われ。
いずれにせよRSAの落とし戸つき一方向性関数の特殊な性質であることは注意してください。

## まとめ
- 公開鍵による暗号化PKEと署名は3個の抽象的なメソッド（インタフェース）からなる。
- 一般論はインタフェースの言葉のみを使って説明し、個別の方式の特殊性（実装詳細）と切り分ける。
