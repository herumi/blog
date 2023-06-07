---
title: "Granite Rapids/Sierra ForestのAMX/AVXの新命令"
emoji: "🖥"
type: "tech"
topics: ["amx","avx", "asm"]
published: true
---
## 初めに

先月2022年9月に公開された「[Intel Architecture Instruction Set Extensions and Future Features](https://www.intel.com/content/www/us/en/develop/download/intel-architecture-instruction-set-extensions-programming-reference.html)」 (PDF) を眺めつつ、Intelで開発中のGranite Rapids/Sierra Forestで追加される予定のAMX/AVX関係の命令を紹介します。[Xbyak](https://github.com/herumi/xbyak) v6.63はIntelからのパッチを取り込んでこれらの命令に対応しています。

## AMX/AVX追加命令概要

AMX/AVX関連で追加された命令群を簡単に分類してコメントしました。以下ざっくりと紹介します。

- Granite Rapids向け
  - AMX-FP16命令群
    - tdpfp16ps ; FP16の積和加算
- Sierra Forest向け
  - AVX-IFMA命令群
    - vpmadd52{h,l}uq ; 52ビット×52ビット整数→104ビット整数（AVX-512版がAVX対応）
  - AVX-VNNI-INT8命令群
    - vpdpb{su,uu,ss}d ; 1バイト（符号あり・なし）整数の積和加算
    - vpdpb{su,uu,ss}ds ; 1バイト（符号あり・なし）整数の積和飽和加算
  - AVX-NE-CONVERT命令群
    - vbcstnebf162ps ; BF16→float変換+broadcast
    - vbcstnesh2ps ; FP16→float変換+broadcast
    - vcvtneebf162ps ; 偶数要素BF16→float変換
    - vcvtneeph2ps ; 偶数要素FP16→float変換
    - vcvtneobf162ps ; 奇数要素BF16→float変換
    - vcvtneoph2ps ; 奇数要素FP16→float変換
    - vcvtneps2bf16 ; float→BF16変換（AVX-512版がAVX対応）

## AMX概略

「[Intel新ロードマップを発表。Meteor Lake、Arrow Lake、Lunar Lakeへと進化](https://pc.watch.impress.co.jp/docs/news/1389338.html)」などを見ると、Granite Rapidsはなかなか出てこないSapphire Rapidsの次々CPUです。2024年投入計画があるとのこと。Sapphire Rapidsで初めて追加されたAMXに、新しい命令が1個追加されています。

AMX (Advanced Matrix EXtensions) とは行列計算をするための枠組みです。AI（深層学習）では行列演算が多用されるのでそれ専用の命令が追加されました。AVX/AVX-512とは異なる命令体系で1個1KiBのタイルと呼ばれるレジスタが8個tmm0, ..., tmm7追加されています。

タイルは大きな2次元行列の部分行列を表現でき、タイルを操作するアクセラレータTMUL (tile matrix multiply unit) があります。

AMXアーキテクチャは概ね次の操作で利用します。
- ホストCPUがメモリブロックやループの回数、ポインタ操作などを設定する。
- load/store/アクセラレータコマンドをTMULに送る。
- TMULがメモリ一貫性をもちながらホストのメモリとやりとりをして結果を返す。

![冒頭の資料の図3-3](https://raw.githubusercontent.com/herumi/blog/main/x64/img/matrix-multiply.png)
*行列演算(C += AB) 冒頭の資料の図3-3より引用*

*行列のタイル計算の内側部分の例（概要）*
```cpp
  // Cの読み込み
  // rsi : Cの先頭ポインタ
  // rdi  : Cの次のラインへのオフセット
  tileloadd(tmm0, ptr [rsi+rdi]);

  // Cの隣(rsi + n)の読み込み
  tileloadd(tmm1, ptr [rsi+rdi+n]);
  mov(r14, 0);
L(lp);
  // Aの読み込み
  // r8 : Aの先頭ポインタ
  // r9 : Aの次のラインへのオフセット
  tileloadd(tmm2, ptr [r8+r9]);

  // Bの読み込み
  // r10 : Bの先頭ポインタ
  // r11 : Bの次のラインへのオフセット
  tileloadd(tmm3, ptr [r10+r11]);

  // u8成分のAとs8成分のBの行列計算してtmm0に足し込む
  tdpbusd(tmm0, tmm2, tmm3);

  // 隣のBの読み込み
  tileloadd tmm3, [r10+r11+n]);

  // Cの隣を更新
  tdpbusd(tmm1, tmm2, tmm3);

  // 次のA
  add(r8, k);
  add(r10, k*r11);
  add(r14, k);
  cmp(r14, limit);
  jne(lp);
  tilestored(ptr [rsi+rdi], tmm0);   // Cの更新
  tilestored(ptr [rsi+rdi+m], tmm1); // 隣のCの更新
```

大きな行列をレジスタに読み込むにはCPUキャッシュに乗せるために部分行列に分割して処理するのが定石です。
部分行列を表すメモリとタイル（tmmレジスタ）でデータを読み書きするには一部非連続なアドレスを処理しなければなりません。tileload/tilestoredを使うとそれが簡単にできます。
そしてタイルに対して積和演算専用命令（tdpbusdなど）を実行して行列の積を実現します。行列の要素としてはBF16やint8をサポートしていました。

## BF16 (bfloat16) とFP16 (binary16)

BF16は機械学習の計算で従来のfloat (32bit) ほどの精度が必要ないところで利用される16bit浮動小数点数です。
floatが1bitの符号、8bitの指数部、23bitの仮数部なのに対してbfloat16は符号と指数部が同じで仮数部を上位7bitに切り詰めたフォーマットで表現されます。

floatとBF16とFP16のフォーマット表
型|符号ビット(s)|指数部(e)|仮数部(f)|値|
-|-|-|-|-|
float|1|8|23|(-1)^s 2^(e-127)×(1 + f/2^24)|
BF16 (bfloat16)|1|8|7|(-1)^s 2^(e-127)×(1 + f/2^8)|
FP16 (binary16)|1|5|10|(-1)^s 2^(e-15)×(1 + f/2^11)

Sapphire RapidsはBF16をサポートしていたのですが、Granite RapidsではFP16用の積和加算命令 (tdpfp16ps) も追加されました。
FP16は半精度浮動小数点数と呼ばれ、BF16と同じく16bitで浮動小数点数を表現します。BF16よりは表現できる値の範囲が狭い（指数部が小さい）のですが、その分精度はあります。どちらがよいかはアプリケーションによります。両方サポートするのがよいということで対応したのでしょう。
Intel系CPUでFP16をサポートする命令は今回が初めてだと思います。AVX-NE-CONVERTはfloatとそれらとの相互変換をサポートします。

## Sierra ForestのAVX拡張命令

Sierra ForestはEコアのみからなるCPUの予定のとこと。そのため（恐らく）AVX-512は持っていません。Intelの戦略はよく分かりませんが、AVX-512のうちAI系で便利な命令群をAVX2に逆移植するパターンが増えています。
Alder LakeやSapphire Rapidsで追加されたAVX-VNNIがその始まりです。Sierra ForestではAVX-IFMA, AVX-VNNI-INT8が追加されています。

AVX512_VNNIやAVX-VNNIでint8_t×uint8_tの積和をintに足し混む命令はvpdpbusdですが、今回追加されたAVX-VNNI-INT8の同じことをする命令はvpdpbsud（usとsuで逆!）です。
説明を見る限りでは同じ操作でプレフィクスが違うだけです。名前が違うのは紛らわしいですね。

![vpdpbsud](https://user-images.githubusercontent.com/245118/194227473-2232eab9-8996-4360-915b-cedf96675ef5.png)
*vpdpbsud （冒頭の資料Ref. # 319433-046 p.2-40）*

![vpdpbusd](https://user-images.githubusercontent.com/245118/194227525-779cb523-d464-4e45-847f-f77068538805.png)
*vpdpbusd （Intel64 and IA-32 Architectures Software Developer's Manual p.5-344）*


[Intelの人によると](https://github.com/herumi/xbyak/pull/162#issuecomment-1270698771)AVX-VNNI-INT8はint8_t×uint8_t以外にint8_t×int8_tやuint8_t×uint8_tも追加されたので違う名前だそうですが、これらもそのうちAVX-512に入るのでしょうか。
いずれにせよ目茶苦茶コードが書きにくそうです。もうちょっと一貫性を持って設計して欲しいものです。実質、oneAPI (oneDNN) でしか使われないからよいだろうということなのかな。

## encodingの選択

元々AVX-512であったvpdpbusdがAVX-VNNIとしてAVX2マシンでも使えるようになった場合、同じ命令でもEVEXとVEXの異なるエンコーディングをしなければなりません。
たとえば
```
vpdpbusd(xm0, xm1, xm2);
```
はAVX512_VNNI用のEVEXエンコーディングだと`62 F2 75 08 50 C2`ですがAVX_VNNIだと`C4 E2 71 50 C2`です。Xbyakでは後方互換性のためにデフォルトはAVX512_VNNIとして扱い、
```
vpdpbusd(xm0, xm1, xm2, VexEncoding);
```
とVexEncodingを指定するとAVX_VNNIとして扱うようにしました。しかし、今回のように今後そのような命令が増えてくるといちいちVexEncodingを指定するのは面倒です。そこで、途中でデフォルト値を変更できるように`setDefaultEncoding`という関数を追加しました。

```cpp
vpdpbusd(xm0, xm1, xm2); // EVEX
setDefaultEncoding(VexEncoding);
vpdpbusd(xm0, xm1, xm2); // VEX
setDefaultEncoding(EvexEncoding);
vpdpbusd(xm0, xm1, xm2); // EVEX
```
このように切り換えられます。setDefaultEncodingはvpdpbusdやvcvtneps2bf16などのencodingを指定できる命令にしか影響しません。

## 雑感
命令の拡張のされ方を見ると、Intelはサーバ系はAVX-512/AMX、コンシューマ系はAVX2にAVX-512の中から便利な命令を一部持ってくる方針のようです。
AMDのZen 4は元のAVX-512に対応しているのでしばらくは個人でAVX-512プログラミングするならAMDを選ぶことになりそうです。個人的にはAVX-512のマスク系命令はとても便利なので、レジスタ幅は256bitのままでよいからそれらが使えるようになって欲しいものです。
