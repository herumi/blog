---
title: "LLVMに対する32ビット定数除算の改善"
emoji: "📖"
type: "tech"
topics: ["整数除算", "逆数乗算", "LLVM", "最適化"]
published: true
---
## 初めに
[LLVM](https://github.com/llvm/llvm-project)はコンパイラ基盤で、抽象的なCPU命令を表す中間表現LLVM-IRに対して最適化を行ったり、ターゲットCPU用のアセンブリ言語コードを生成したりする機能を持ちます。

今回、符号無し32ビット変数xに対するx/7のような定数除算についてLLVMの最適化を改善するプルリクエスト[[SelectionDAG] Optimize 32-bit udiv with 33-bit magic constants on 64-bit targets](https://github.com/llvm/llvm-project/pull/181288)がllvm:mainにマージされました。LLVMはC/C++/Rust/Swiftなど様々な言語のコンパイラ実装に利用されているため、それらの言語の最適化に寄与します。

## 何がどう改善されたのか
たとえば入力値retを7, 19, 107で割りながら10億回更新するCのコードbench.cがclang-18 -O2でx64 (Xeon w9-3495X)で1.67倍、M4 (Apple M4 MacBook Pro)で1.98倍速くなりました。
初期値をargcに依存させることで過剰な定数畳み込み最適化を防いだり、排他的論理和をすることで命令順序の入れ替えを防いて除算の速度が測りやすいようにしています。

*bench.c*
```c
#include <stdio.h>
#include <stdint.h>
#include <time.h>

int test(uint32_t ret)
{
    for (int i = 0; i < 1000000000; i++) {
        ret ^= (i ^ ret) / 7;
        ret ^= (i ^ ret) / 19;
        ret ^= (i ^ ret) / 107;
    }
    return ret;
}

int main(int argc, char *argv[])
{
    time_t begin = clock();
    uint32_t ret = argc * 0x12345678;
    ret = test(ret);
    time_t end = clock();
    printf("ret=%08x time=%.2f sec\n", ret, (end - begin) / (double)CLOCKS_PER_SEC);
}
```

評価方法は次の通りです。まず、Clangを用いてbench.cからLLVM-IRコードであるbench.llを生成します。

```bash
clang-18 -O2 -S -emit-llvm bench.c -o bench.ll
```

次にLLVMのllcコマンドでターゲットCPU用のアセンブリ言語コードを生成します。
そのとき、LLVMオリジナルのllcとパッチを当てたllcの生成コードから実行バイナリを生成して実行時間を測定しました。

結果は次の通り

CPU|オリジナル|パッチあり|向上率
-|-|-|-
Xeon|6.40|3.83|1.67
M4|6.70|3.38|1.98

M4では2倍近く速くなっています。除算のような単純な処理がそれだけ速くできる余地があったのは驚きです。
生成コードがどう変化したのか確認しておきましょう。

まずはx64から。

```bash
llc -O2 -mattr=+bmi2 bench.ll -o 出力ファイル.s
```

でアセンブリ言語（以下asm）を出力しました。bmi2はmulxという乗算命令を使うためのオプションです。
test関数をみてみます。

```asm
test:
   movl    %edi, %eax
   xorl    %ecx, %ecx
   movl    $2938661835, %edx               # imm = 0xAF286BCB
   .p2align    4, 0x90
 .lp:
   movl    %ecx, %esi
   xorl    %eax, %esi
   # ↓ここから
   imulq   $613566757, %rsi, %rdi          # imm = 0x24924925
   shrq    $32, %rdi
   subl    %edi, %esi
   shrl    %esi
   addl    %edi, %esi
   shrl    $2, %esi
   # ↑ここまでがx/7相当のアセンブリ言語
   xorl    %eax, %esi
   movl    %esi, %edi
   xorl    %ecx, %edi
   movq    %rdi, %rax
   # ↓ここから
   imulq   %rdx, %rax
   shrq    $32, %rax
   subl    %eax, %edi
   shrl    %edi
   addl    %eax, %edi
   shrl    $4, %edi
   # ↑ここまでがx/19相当のアセンブリ言語
   xorl    %esi, %edi
   movl    %edi, %eax
   xorl    %ecx, %eax
   # ↓ここから
   imulq   $842937507, %rax, %rsi          # imm = 0x323E34A3
   shrq    $32, %rsi
   subl    %esi, %eax
   shrl    %eax
   addl    %esi, %eax
   shrl    $6, %eax
   # ↑ここまでがx/107相当のアセンブリ言語
   xorl    %edi, %eax
   incl    %ecx
   cmpl    $1000000000, %ecx               # imm = 0x3B9ACA00
   jne .lp
```

遅いdiv命令を避けて、乗算と加減算やビットシフトの組み合わせのなかなか複雑なコードが生成されています。次は改良版です。

```asm
test:
    movl    %edi, %eax
    xorl    %ecx, %ecx
    movabsq $2635249153617166336, %rsi      # imm = 0x24924924A0000000
    movabsq $970881267157434368, %rdi       # imm = 0xD79435E58000000
    movabsq $172399477334736896, %r8        # imm = 0x2647C6946000000
    .p2align    4
.lp:
    movl    %ecx, %edx
    xorl    %eax, %edx
    mulxq   %rsi, %r9, %r9 # x/7に対応
    xorl    %eax, %r9d
    movl    %r9d, %edx
    xorl    %ecx, %edx
    mulxq   %rdi, %r10, %r10 # x/19に対応
    xorl    %r9d, %r10d
    movl    %r10d, %edx
    xorl    %ecx, %edx
    mulxq   %r8, %rax, %rax # x/107に対応
    xorl    %r10d, %eax
    incl    %ecx
    cmpl    $1000000000, %ecx
    jne .lp
```
最初のコードで「↓ここから...↑ここまで」の部分がmulxq命令1個に置き換わり、ループ内で分岐処理以外が28命令だったものが12命令に減りました。

M4でもみてみましょう。

```sh
llc -mtriple=arm64-apple-macosx -mcpu=apple-m4 bench.ll -o 出力ファイル.s
```

オリジナルのメインループ

```asm
.lp:
    eor w13, w8, w0
    # ↓ここから
    umull   x14, w13, w9
    lsr x14, x14, #32
    sub w13, w13, w14
    add w13, w14, w13, lsr #1
    # ↑ここまでがx/7相当のアセンブリ言語
     eor w13, w0, w13, lsr #2
    eor w14, w13, w8
    # ↓ここから
    umull   x15, w14, w10
    lsr x15, x15, #32
    sub w14, w14, w15
    add w14, w15, w14, lsr #1
    # ↑ここまでがx/19相当のアセンブリ言語
    eor w13, w13, w14, lsr #4
    eor w14, w13, w8
    add w8, w8, #1
    # ↓ここから
    umull   x15, w14, w11
    cmp w8, w12
    lsr x15, x15, #32
    sub w14, w14, w15
    add w14, w15, w14, lsr #1
    # ↑ここまでがx/107相当のアセンブリ言語
    eor w0, w13, w14, lsr #6
    b.ne    .lp
```

改良後のメインループ

```asm
.lp:
    eor w13, w8, w0
    umulh   x13, x13, x9
    eor w13, w13, w0
    eor w14, w13, w8
    umulh   x14, x14, x10
    eor w13, w14, w13
    eor w14, w13, w8
    add w8, w8, #1
    umulh   x14, x14, x11
    cmp w8, w12
    eor w0, w14, w13
    b.ne    .lp
```

こちらも効果は歴然です。
x64/M4どちらも排他的論理和のxor/eorとループカウンタと条件分岐を除けば定数除算が乗算1命令mulx/umulhだけになっているのが分かります。すばらしい。リリース版に入るのが楽しみです。

## 背景
このPRを作ることになった背景を紹介します。
定数整数除算を乗算といくつかの加減算やビットシフトに置き換える最適化は昔から知られています。たとえばx/3を`x*0xAAAAAAAB >> 33`のように置き換える方法です。このときに現れるマジック定数は除数に依存して決まります。

現在のGCC/Clang/MSVCなどのコンパイラに組み込まれている最適化手法は["Division by Invariant Integers using Multiplication"(Granlund, Montgomery, SIGPLAN 1994)](https://gmplib.org/~tege/divcnst-pldi94.pdf)（以下GM法）や『ハッカーのたのしみ』の「整数定数による除算」の節の方法で、そこにはマジック定数の決め方も載っています。x/7ではマジック定数が33ビットになり、それを32ビットCPUでうまく扱う方法も載っています。

私は暗号ライブラリ[mcl](https://github.com/herumi/mcl)の研究開発をしていて、除算の高速化にも関心がありいろいろ調べていました。上記GM法なども20年以上前から知っていたのですが、今回最近のコンパイラで試したところCPUが64ビットになっているにもかかわらず実装方法が変わってないことに気がつきました。

それで、64ビットCPU向けに最適化できる方法をいくつか試し、それらが有効だったので[Kernel/VM探検隊@東京 No18](https://kernelvm.connpass.com/event/355100/)で[紹介](https://zenn.dev/herumi/articles/const-div-revised3)したり、それを更に改善してブログ「[定数除算最適化再考4 更なる高速化](https://zenn.dev/herumi/articles/const-div-revised4)」に書いたり、[2026年 暗号と情報セキュリティシンポジウム](https://www.iwsec.org/scis/2026/)で発表したりしました。

アルゴリズムの詳細は上記リンク先を参照していただくとして、簡単に説明するとx/7などは乗算後にいくつかの加減算とビットシフトが必要だったものを乗算1命令ですむような改善です。

その手法に基づき、x64/M4向けの実行時コード生成するライブラリ[constdiv](https://github.com/herumi/constdiv)も作ってみました。
しかし、やはり本丸は既存のコンパイラです。それを改善できればいいなと開発を始めました。

## 生成AIとの共同作業
LLVMの内部アーキテクチャは大きく分けて三つのフェーズで構成されています。冒頭でも触れましたがC/C++などを共通の中間表現LLVM-IRに変換するフロントエンド、CPUに依存しない論理的な最適化を行うミドルエンド、そして最適化されたIRをターゲットCPUの機械語に変換するバックエンドです。

今回手を入れるのは、バックエンドの初期段階で行われる「命令選択(Instruction Selection)」と呼ばれるフェーズです。ここでは抽象的な演算のグラフ(SelectionDAG)を、実際のCPUが持つ具体的な命令（今回ならmulxやumulhなど）に割り当てていくため、アーキテクチャ固有の効率的な命令を活用した最適化を組み込むのに適した場所です。

*LLVMのフェーズ概略*
```mermaid
flowchart LR
  src["C/C++/Rustなどの<br/>ソースファイル"] --> fe("フロントエンド<br/>（構文・意味解析）")
  fe --> ir1["LLVM-IR"]
  ir1 --> me("ミドルエンド<br/>（抽象的な最適化）")
  me --> ir2["LLVM-IR"]
  ir2 --> be("バックエンド<br/>（CPUに応じた最適化）")
  be --> asm["アセンブリ言語<br/>・オブジェクトコード"]
```


まずは[llvm-project](https://github.com/llvm/llvm-project)から改善すべき箇所を探します。LLVMのコードは膨大なので定数整数除算やHacker's Delightなどのキーワードで該当箇所の見当をつけます。その結果、[llvm/lib/CodeGen/SelectionDAG/TargetLowering.cpp](https://github.com/herumi/llvm-project/blob/0f89e3899502f7f7ddde21215a6ee13e036cae55/llvm/lib/CodeGen/SelectionDAG/TargetLowering.cpp#L6739)のBuildUDIVあたりをいじればよさそうだと分かりました。

*TargetLowering::BuildUDIVの冒頭*
```cpp
/// Given an ISD::UDIV node expressing a divide by constant,
/// return a DAG expression to select that will generate the same value by
/// multiplying by a magic number.
/// Ref: "Hacker's Delight" or "The PowerPC Compiler Writer's Guide".
SDValue TargetLowering::BuildUDIV(SDNode *N, SelectionDAG &DAG,
                                  bool IsAfterLegalization,
                                  bool IsAfterLegalTypes,
                                  SmallVectorImpl<SDNode *> &Created) const {
  SDLoc dl(N);
  EVT VT = N->getValueType(0);
  EVT SVT = VT.getScalarType();
  EVT ShVT = getShiftAmountTy(VT, DAG.getDataLayout());
  EVT ShSVT = ShVT.getScalarType();
  unsigned EltBits = VT.getScalarSizeInBits();
  EVT MulVT;
  ...
```

次は今回提案したい手法を埋め込みます。ただ、簡単ではないだろうなと思いました。何故ならLLVM独自の多数の型や構文木をいじるメソッドなど調べて使えるようになるのに多大な時間がかかるからです。しかし今は生成AIがあります。そこでconstdivに書いた[手法とその結果](https://github.com/herumi/constdiv#our-approach)をAIに読ませて、このような最適化をするよう変更せよと指示しました。

AIはすごいですね。何度かのやりとりのすえ、見事に私が欲しいasm出力をする修正パッチを作ってくれました。もちろん手直しや、細かい作業は必要でしたが想像していたよりもずっと短期間でできてしまいました。びっくりです。

*ログのほんの一部*

```md
## チャット開始

### User
const-udiv-readme.mdのOur Approachの方法をTargetLowering.cppの6808行目あたりで試したい

### Assistant
const-udiv-readme.mdを読んで、TargetLowering.cppの該当箇所を確認します。

[ファイル読み取り実行]

### Assistant
const-udiv-readme.mdの"Our Approach"セクション（152-161行目）に記載されている最適化手法:

**アプローチ**:
- 33ビットのマジック定数 `c = 2^32 + magic` を `(64-a)` ビット左シフト
- 64×64→128ビット乗算の上位64ビットを使用
- これにより、複数の命令を削減

TargetLowering.cppの6808行目付近の `BuildUDIVPattern` ラムダ関数内で、この最適化を実装します。
```

（いろいろ作業）

```md
[デバッグ分析]

問題点:
- `magics.Magic` のビット数チェックが不正確
- 実際は `magics.IsAdd` フラグを確認すべき

修正します。

[コード修正実行]

---

## IsAddフラグによる修正

### Assistant
`magics.IsAdd` フラグを使用するように修正しました。この修正により:
- IsAdd=trueの場合、内部でMagic+2^32が使用される
- より正確に33ビットマジック定数を検出

再ビルドしてテストしてください。
```

## 実際の修正コード
[llvm/lib/Support/DivisionByConstantInfo.cpp](https://github.com/herumi/llvm-project/blob/0f89e3899502f7f7ddde21215a6ee13e036cae55/llvm/lib/Support/DivisionByConstantInfo.cpp#L157-L170)の中で、既存のマジックナンバーの計算を今回の提案手法にあうように補正します。

オリジナルのMagicとShiftに対して`(u128(x) * (((1<<32)|Magic) << (64-Shift)))>>64`と式変形するので、Magicをそのように変更します。ifブロックが今回追加した内容です。

```cpp
  // IsAddとAllowWidenOptimizationがtrueのときに提案方式のための準備をする
  // マジックナンバーMagicに対して ((1<<32)|Magic)<<(64-OriginalShift)を求める
  // W=32
  if (Retval.IsAdd && AllowWidenOptimization) {
    unsigned W = D.getBitWidth();
    unsigned OriginalShift = Retval.PostShift + W + 1;
    // Since PostShift >= 1, shift amount is at most W-2, so W*2 bits suffice.
    Retval.Magic = (APInt(W * 2, 1).shl(W) + Retval.Magic.zext(W * 2))
                       .shl(W * 2 - OriginalShift);
    Retval.IsAdd = false;
    Retval.PostShift = 0;
    Retval.Widen = true;
  }
```

そして、コンパイラのターゲットCPUがu64×u64→u128の乗算命令（x64のmulxかAArch64のumulh相当）か、その上位64ビットを取得する命令（x64の従来のmul）が利用できる場合に応じて、今回の提案手法の最適化を実行します。

*[SDValue TargetLowering::BuildUDIV](https://github.com/herumi/llvm-project/blob/0f89e3899502f7f7ddde21215a6ee13e036cae55/llvm/lib/CodeGen/SelectionDAG/TargetLowering.cpp#L6880-L6895)の変更部分*

```cpp
  if (UseWiden) {
    // Compute: (i64(x) * MagicFactor) >> 64
    // 入力値を64ビットにゼロ拡張する
    SDValue X64 = DAG.getNode(ISD::ZERO_EXTEND, dl, MVT::i64, N0);

    SDValue High;
    if (HasWideVT64MULHU) {
      // X64とマジックナンバーを掛けて上位64ビットを取得する
      High = DAG.getNode(ISD::MULHU, dl, MVT::i64, X64, MagicFactor);
    } else {
      // X64とマジックナンバーを掛けて128ビットを計算し、その上位64ビットを取得する
      SDValue LoHi =
          DAG.getNode(ISD::UMUL_LOHI, dl, DAG.getVTList(MVT::i64, MVT::i64),
                      X64, MagicFactor);
      High = SDValue(LoHi.getNode(), 1);
    }

    // 結果を32ビットに切り詰めて返す
    Created.push_back(High.getNode());
    return DAG.getNode(ISD::TRUNCATE, dl, VT, High);
  }
```


## 水平展開
最初x64で生成コードを確認していましたが、今回の手法はApple M4でも有効なのは分かっているのでそちらでもテストするように依頼します。
LLVMの上位での最適化手法なので、M4でも一発で望みのコードが生成されました。たいしたものです。それから64ビットのRISC-Vでも同様にうまくいってることを確認しました。

余談ですが、LLVMのビルドとテストは結構重たいです。今回は112コアのXeon w9-3495Xを使っていたので大分助かりました。

## プルリクエストとコードレビュー
うまくできたのでフォーマットを整え、プルリクエストを出します。登録時に5000件以上のPRがあったのでいつになるかなと思ったのですが、意外と早く対応してくれました。それからは怒濤のコードレビューが始まり、冗長な部分や変更箇所の移動などに対応します。そしてなんとかmergeまでたどり着けました。

## GCCとちょっとしたクイズ
LLVMでうまくいったので今度はGCCです。実は手元では既に動いているパッチがあります。
正解のお手本（LLVMに対する修正すべき方法）があるので、生成AIに依頼するとLLVMよりもずっと素早くパッチを作れました。
ただ、LLVMに比べてちょっと最適化が甘いところがありました。その理由を説明するために、唐突ですがクイズです。

### クイズ
x64の乗算命令`mulx H, L, r`は64ビットのレジスタrとrdxの積を128ビットとして[H:L]に格納します（`(H<<64)|L`の意味）。
ここでHとLを同じレジスタにするとHとLには何が設定されるでしょうか。

1. 未定義
2. H = L = 結果の上位64ビット
3. H = L = 結果の下位64ビット

答えは2番の「上位64ビットが設定される」です。つまり`mulx H, H, r`はAArch64の`umulh H, r, rdx`に相当します。

IntelのマニュアルSDMにも

>If the first and second operand are identical, it will contain the high half of the multiplication result.
>（一つ目と二つ目のオペランドが同一なら、乗算結果の上位半分が格納される）

とあります。

さて、GCCはどうやらこの事実を考慮しないようで、mulqよりも効率がよくなるmulx（速度はほぼ変わらないがレジスタの使い回しがしやすい）を使うように指示しても冗長なmovが必要と判断してmulxを使いません。
そこで3万行以上あるx86の最適化ノウハウ集みたいなもの[gcc/config/i386/i386.md](https://gcc.gnu.org/git/?p=gcc.git;a=blob;f=gcc/config/i386/i386.md;h=8636b0168785fb2e73f10bc929ddd4f28cb88d3e;hb=HEAD)にumulh相当のパターンを追加するとLLVMと同等のコードが生成されるようになりました。

*i386.md修正前*

```asm
movabsq $2635249153617166336, %rcx
movl    %edi, %eax
mulq    %rcx
movl    %edx, %eax
```

*i386.md修正後*

```asm
movabsq $2635249153617166336, %rax
movl    %edi, %edx
mulx    %rax, %rdx, %rax
```

今後はこのあたりをまとめてプルリクエストを出す予定です。
