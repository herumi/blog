# 呼び出し規約

[Procedure Call Standard for the Arm 64-bit Architecture (AArch64)](https://github.com/ARM-software/abi-aa/blob/main/aapcs64/aapcs64.rst)

## 汎用レジスタ

- caller ; 関数を呼び出した側
- callee ; 呼び出された関数の中

- x ; 64-bit
- w ; 32-bit

- x0, ..., x7 ; 関数の引数
- x8 ; indirect result location register
- x9, ..., x15 ; 一時レジスタ
- x16, x17, x18 ; 特別なレジスタ
- x19, ..., x28 ; calee saved (関数の中で保存)
- x29 ; fp ; フレームポインタ
- x30 ; lr ; リンクレジスタ
- x31 ; sp ; スタックポインタ(常に16の倍数)

とりあえず関数f(x0, x1, x2, ..., x7)の中で自由に使ってよいのはx9からx15まで。
それより使いたい場合はスタックを確保してx19からx28までを保存して利用。
関数から抜けるときにx19からx28を復元してスタックを戻してretとする。
関数の戻り値はx0。

- x8 ; 大きなサイズの構造体を返す関数のときに利用(その構造体の先頭アドレスを指す)
- x16, x17 ; intra-procedure-callスクラッチレジスタ
  - リンカが挿入する小さいコード(veneer)や共有ライブラリのシンボル解決に利用するPLT(procedure linkage table)コードなどで利用
- x18 ; プラットフォームレジスタ
  - プラットフォーム依存ABI用のレジスタ
  - ABIで使われないなら一時レジスタとして利用できる(が使わない方が無難)
```
┌-----┐
│lr'' │
├-----┤
│fp'' │
├-----┤fp'
│arg  │
├-----┤sp' ; 関数に入った直後
│loc  │
├-----┤
│save │
├-----┤
│lr'  │
├-----┤
│fp'  │
├-----┤fp ; mov fp, sp
│arg  │
└-----┘sp ; stp fp, lr, [sp, #-num] ; ここにくる
```

## SVEレジスタ
z0からz31まで。A64FXは最大512bit。
下位64-bitは従来の浮動小数点数レジスタvと共有。

- z0からz7まで自由
- z8からz23までcallee保存
- z24からz31までcaller保存

関数の中ではz0からz7までとz24からz31まで自由に使える。

## 述語(predicate)レジスタ
p0からp15まで。各bitが1ならactive。0ならinactive。

- p0からp3まで自由
- p4からp15までcallee保存
