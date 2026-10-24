---
title: "eMudhraの中間CA証明書の秘密鍵を求めてみる"
emoji: "📖"
type: "tech"
topics: ["楕円曲線暗号", "CA", "eMudhra", "離散対数問題"]
published: true
---
## はじめに
2026年10月に認証局eMudhraの中間CA証明書の署名鍵（秘密鍵）が脆弱だったという報告がありました。
ここではそのCA証明書に記載された検証鍵（公開鍵）から署名鍵を実際に求めてみます。

## Pythonを使って求める
脆弱だった秘密鍵は8ビット以下だったということで、洗練された高速なライブラリを使わずナイーブな方法を使います。
『Software Design 2022年3月号』で紹介した記事、[楕円曲線暗号のPythonによる実装その1（有限体とECDH鍵共有）](https://zenn.dev/herumi/articles/sd202203-ecc-1)で使った楕円曲線の点の演算コードを利用しましょう。
ソースコードは[src/ecc](https://github.com/herumi/blog/tree/main/src/ecc)に置きました。
これらは有限体や楕円曲線の点のクラス `Ec` を含みます。
[search_key.py](https://github.com/herumi/blog/blob/main/src/ecc/search_key.py)が実際に求めるコードです。
順番に説明しましょう。

## 公開鍵を取得する
[eMudhra Intermediate CA Compromise](https://emudhra.we-have-a-problem.com/#check)によると、
`dv-g3b-emudhra.we-have-a-problem.com`が脆弱な鍵があるとのことです。

```
openssl s_client -connect dv-g3b-emudhra.we-have-a-problem.com:443 -servername dv-g3b-emudhra.we-have-a-problem.com -showcerts > emudhra.pem
```

で[emudhra.pem](https://github.com/herumi/blog/blob/main/src/ecc/emudhra.pem)を取得します。このファイルの中身は

https://github.com/herumi/blog/blob/103aeaa7741f7da77cf38ce47e0c03a33b6f070c/src/ecc/emudhra.pem#L22-L46

となっているのですが、今回対象となっているのは `O=eMudhra BV`, `CN=EM DV TLS CA - G3B` という2番目の公開鍵です。
その部分だけ取り出して[bad.pem](https://github.com/herumi/blog/blob/main/src/ecc/bad.pem)に保存しました。
このファイルから実際の楕円曲線の点を取り出します。

opensslコマンドを使って

```
>openssl x509 -in bad.pem -noout -pubkey | openssl ec -pubin -text
```

とすると中身が読める形にしてくれます。

https://github.com/herumi/blog/blob/103aeaa7741f7da77cf38ce47e0c03a33b6f070c/src/ecc/search_key.py#L7-L26

この中の

```
pub:
    04:e7:9e:d9:c0:4a:97:73:c4:73:5e:51:11:1d:a8:
    5a:c2:bd:5e:64:ab:37:4b:80:e1:2c:bc:20:38:ec:
    1e:4f:0e:3b:81:2f:c9:85:9e:1e:67:af:b0:4e:2f:
    8b:c9:7f:74:85:ba:65:aa:91:1e:e8:a4:e4:5b:62:
    d5:cc:03:d5:6b:e6:78:b3:c3:8c:b4:dc:93:7c:a2:
    46:0e:23:84:53:c8:92:93:5d:29:d4:4f:91:e1:56:
    06:b6:3c:ce:da:b4:68
```

が今回欲しい情報です。この文字列を

```python
h = ''.join(pubKeyStr.replace('\n','').replace(' ', '').split(':'))
```

として16進数文字列に変換します。このフォーマットは先頭 `04` に続いて楕円曲線のx座標とy座標が連結されています。


https://github.com/herumi/blog/blob/103aeaa7741f7da77cf38ce47e0c03a33b6f070c/src/ecc/search_key.py#L38-L46

によりその `(x, y)` を取り出し、 `Ec` クラスのコンストラクタに渡して楕円曲線の点 `Q = Ec(x, y)` を作ります。

## 楕円曲線と解くべき離散対数問題を設定する
前節のbad.pemをテキスト化した中に、

```
ASN1 OID: secp384r1
NIST CURVE: P-384
```

とあるので規格書にしたがってその楕円曲線で `Ec` クラスを初期化します。

```python
  P = init('NIST_P384')
```

有限体 $𝔽_p$ 上 $y^2=x^3+ax+b$ で定義された曲線のパラメータ $p$, $a$, $b$ は

https://github.com/herumi/blog/blob/103aeaa7741f7da77cf38ce47e0c03a33b6f070c/src/ecc/curve.py#L30-L37

で与えています。

Pが規格で定められた楕円曲線の生成元（ベースポイント）です。今からやるべきは P を何倍すると Q になるかという離散対数問題を解くことです。

```
Q = x P となる x を求める。
```

この x がこの証明書の署名鍵（秘密鍵）です。
もちろん通常この離散対数問題を解くのは現在のコンピュータでは不可能とされています（遠い将来、実用的な量子コンピュータが登場すると話は変わりますが）。

## 総当たりで解いてみる
accを O から出発して P を足していき P, 2 P, 3 P, 4 P, ... と求めます。acc == Q となれば足した回数が x となります。

https://github.com/herumi/blog/blob/103aeaa7741f7da77cf38ce47e0c03a33b6f070c/src/ecc/search_key.py#L53-L58

通常であれば x は384ビット近い値なのでこのループは $2^{383}$ 回ぐらいしないとヒットせず絶対に終わりません。
しかし、手元で動かすと0.4秒ほどで x = 156 と求まりました。
とっても脆弱だったということです。

## まとめ
認証局eMudhraの中間CA証明書の秘密鍵を公開鍵からナイーブなPythonコードで求めました。
実際に手を動かして理解を深めるきっかけになれば幸いです。
