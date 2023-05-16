---
title: "x64用主要アセンブラの激ムズクイズ"
emoji: "🛠"
type: "tech"
topics: ["x64", "ASM", "Python", "Xbyak"]
published: false
---
## 初めに
これはx64用JITアセンブラ[Xbyak](https://github.com/herumi/xbyak)や静的アセンブラ[s_xbyak](https://github.com/herumi/s_xbyak)を開発するときに、各種アセンブラの差異についてはまったり調べたりしたことをまとめるにあたり、せっかくなのでクイズ形式にしたものです。
内容はかなりマニアックです。何を聞かれてるのか分からなくても殆どの場合、何の問題もありません。

## 前置き
ここで扱うアセンブラは
- GAS (GNU Assembler) 2.38
- [Netwide Assembler (NASM)](https://www.nasm.us/) 2.16
- [Microsoft Macro Assembler](https://learn.microsoft.com/vi-vn/cpp/assembler/masm/microsoft-macro-assembler-reference) 14.35.32217.1

です。MASMはWindows専用、GASは主にLinuxで扱われますが、Windowsでもmingwなどで使えます。NASMはwin64, elf64, macho64 (Intel macOS)用のオブジェクトコードを生成できます。
問題は概ねIntelの[SDM](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html)か[XED](https://intelxed.github.io/)の表記にしたがいます。GAS/NASM/MASMでどう書くかを答えてください。自分が慣れてるアセンブラのみの回答でもOKです。
s_xbyakでは、アドレス参照についてXbyakの`ptr[...]`が`ptr(...)`になる以外は概ね同じです。

## 初級編

### Q1
movでraxにrbxを代入する。

### Q2
raxの値に0x123をaddする。

### Q3
raxで指定されたアドレスの64bit整数をincする。

### Q3

## 中級編

### Q

### Q
raxにあるfloat値をm512としてブロードキャストしてzmm1にvaddpsしてzmm0に代入する。

## 上級編

### Q
raxにあるdouble値をm128として（つまり2個のdouble値）xmm0にcvtpd2dqする。

### Q
raxにあるdouble値をm256としてブロードキャストしてxmm0にcvtpd2dqする。

### Q
第2世代Xeon SPで使えるようになったAVX-512 VNNI命令vpdpbusdをAVX-512をサポートしていないAlder Lakeで使えるようにAVX-VNNIとしてエンコードする。

## 回答

### A1
- Xbyak : `mov(rax, rbx)`
- GAS : `mov %rbx, %rax`
- NASM : `mov rax, rbx`
- MASM : `mov rax, rbx`

GASは引数の順序がIntelのマニュアルと逆順になり、レジスタには`%`をつける必要があります。

### A2
- Xbyak : `add(rax, 0x123)`
- GAS : `add $0x123, %rax`
- NASM : `add rax, 0x123`
- MASM : `add rax, 123h`

GASでは数値の先頭に`$`をつける必要があります。MASMは`0x...`という16進数表記はエラーで、`...h`という表記でなければなりません。

### A3
- Xbyak : `inc(qword[rax])`
- GAS : `incq (%rax)`
- NASM : `inc qword[rax]`
- MASM : `inc qword ptr [rax]`

サイズを明示するときはGAS以外はメモリ側につけるのですがGASはニーモニックの後ろにつけます。

### Q
- Xbyak : `vaddps(zmm0, zmm1, ptr_b[rax])`
- GAS : `vaddps (%rax){1to16}, %zmm1, %zmm0`
- NASM : `vaddps zmm0, zmm1, [rax]{1to16}`
- MASM : `vaddps zmm0, zmm1, dword bcst [rax]`

1個の要素をレジスタ全体にブロードキャストキャストするには`{1toX}`を使います。
今回はfloat(32bit)をzmmレジスタ(512bit)にブロードキャストするので`X = 512/32=16`となります。GASとNASMは`{1to16}`という属性が付与されています。
しかし、本来ニーモニックとレジスタから`X`の値は一意に決まります。そこでXbyakでは`ptr_b`を採用し、自動的に設定されるようにしています。
MASMも同様に`bcst`を使います。ただしMASMは元のデータサイズ（今回は`dword`）を指定しなければならないので中途半端に思います。
s_xbyakでは`ptr_b`から`{1toX}`を求めるために、Intel SDMからニーモニックのパターンを全て抜き出してテーブルを作って対応しました。なかなか面倒でした。

### A
- Xbyak : `vcvtpd2dq(xmm0, ptr[rax])`
- GAS : `vcvtpd2dqx (%rax), %xmm0`
- NASM : `vcvtpd2dq xmm0, oword [rax]`
- MASM : `vcvtpd2dq xmm0, xmmword ptr [rax]`

Xbyakでは`ptr`の代わりに`xword`を使っても構いません。NASMは`xword`ではなく`oword`を指定しなければなりません。`yword`や`zword`はそのままなので`xword`にしてほしかったところです。GASはニーモニックに`x`をつけます（m256なら`y`、m512なら`z`）。
あまり使われないので以前objdumpのバグを見つけて[報告した](https://lists.gnu.org/archive/html/bug-binutils/2018-04/msg00002.html)ことがあります。

サイズ指定子一覧

asm|8|16|32|64|128|256|512
-|-|-|-|-|-|-|-
Xbyak|byte|word|dword|qword|xword|yword|zword
GAS|b|w|l|q|x|y|z
NASM|byte|word|dword|qword|oword|yword|zword
MASM|byte ptr|word ptr|dword ptr|qword ptr|xmmword ptr|ymmword ptr|zmmword ptr

GASの32bitが`d`ではなく`l`なのに注意。`l`だとlongで64bitかと勘違いしそうです。

### A
- Xbyak : `vcvtpd2dq(xmm0, yword_b[rax])`
- GAS : `vcvtpd2dq (%rax){1to4}, %xmm0` または`vcvtpd2dqy (%rax){1to4}, %xmm0`
- NASM : `vcvtpd2dq xmm0, [rax]{1to4}`
- MASM : 未対応

Xbyakではywordとしてブロードキャストするために`yword_b`を使います（ここでは`ptr_b`を使うと`xword_b`となります）。
GASやNASMでは`{1to4}`があるのでm256とわかるためにサイズを指定する必要はありません。しかし`{1toX}`の`X`はニーモニックやレジスタによって変わるので煩わしいです。
Xbyakの`ptr_b`やMASMの`bcst`が楽でよいと思います。
MASMは`vcvtpd2dq xmm0, qword bcst ymmword ptr [rax]`と書いてもm128のブロードキャスとして扱われてしまいました。
指定する方法が無さそうなので[バグ報告](https://developercommunity.visualstudio.com/t/ml64exe-cant-deal-with-vcvtpd2dq-xmm0/10352105)しました。そのうち改善されると思います。

### A
- Xbyak : `vpdpbusd(xmm0, xmm1, xmm2, VexEncoding)`
- GAS : `{vex} vpdpbusd %xmm2, %xmm1, %xmm0`
- NASM : 未対応?
- MASM : `vex vpdpbusd xmm0, xmm1, xmm2`

デフォルトでは`{evex}`や`EvexEncoding`を指定したのと同じです。
NASMでは`{evex}`は指定できてAVX-512としてエンコードされますが、`{vex}`はエラーになりました。未対応なのかもしれません。

## まとめ
ASM生成コードツール`s_xbyak`を紹介しました。既に自分のプロジェクト[mcl](https://github.com/herumi/mcl)や[fmath](https://github.com/herumi/fmath)などの既存のコードの一部を`s_xbyak`を使って書き直しています。
まだしばらく安定するまでは文法の破壊的変更があると思いますが、興味ある方は試して感想を頂けると幸いです。
