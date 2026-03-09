---
title: "LLVMに対する32ビット定数除算の改善"
emoji: "📖"
type: "tech"
topics: ["整数", "除算", "逆数乗算"]
published: true
---
## 初めに
[LLVM](https://github.com/llvm/llvm-project)に対する`uint32_t`なxに対するx/7のような定数除算の最適化を改善するプルリクエスト[[SelectionDAG] Optimize 32-bit udiv with 33-bit magic constants on 64-bit targets](https://github.com/llvm/llvm-project/pull/181288)がllvm:mainにマージされました。LLVMの様な広く使われているプロジェクトに貢献できて嬉しいです。コンパイラ基盤に対する改善なのでC++やRustなどの言語の最適化に寄与します。

## 何がどう改善されたのか
たとえば次のような適当な値を7, 19, 107で割りながら変数`ret`を10億回更新するコードbench.cがx64 (Xeon w9-3495X)やM4 (Apple M4 MacBook Pro)で1.67倍、1.98倍速くなりました。
初期値がargcに依存しているのは不用意な最適化を防ぐためです。

```c
// bench.c
#include <stdio.h>
#include <stdint.h>
#include <time.h>

uint32_t div7(uint32_t x) { return x / 7; }
uint32_t div19(uint32_t x) { return x / 19; }
uint32_t div107(uint32_t x) { return x / 107; }

int test(uint32_t ret)
{
    for (int i = 0; i < 1000000000; i++) {
        ret ^= div7(i ^ ret);
        ret ^= div19(i ^ ret);
        ret ^= div107(i ^ ret);
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

LLVMはコンパイラ基盤で、抽象的なCPU命令を表す中間表現IRに対して最適化を行ったり、ターゲットCPU用のアセンブリ言語コードを生成します。
LLVMはC/C++/Rust/Swiftなど様々な言語のコンパイラ実装に利用され、ClangはC/C++用フロントエンドの一つです。
今回はLLVMに対する改善なので、bench.cからLLVM-IRコードであるbench.llを生成し、ターゲットCPU用のアセンブリ言語コードを生成するところで、オリジナルのllcとパッチを当てたllcの生成コードを比較します。

次の方法で評価しました。まずClangでLLVM-IRコードを生成します。

```bash
clang-18 -O2 -S -emit-llvm bench.c -o bench.ll
```

次に生成されたbench.llをオリジナルのllcとパッチを当てたllcで実行バイナリを生成して実行時間を測定しました。
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
 .LBB3_1:
   movl    %ecx, %esi
   xorl    %eax, %esi
   imulq   $613566757, %rsi, %rdi          # imm = 0x24924925
   shrq    $32, %rdi
   subl    %edi, %esi
   shrl    %esi
   addl    %edi, %esi
   shrl    $2, %esi
   xorl    %eax, %esi
   movl    %esi, %edi
   xorl    %ecx, %edi
   movq    %rdi, %rax
   imulq   %rdx, %rax
   shrq    $32, %rax
   subl    %eax, %edi
   shrl    %edi
   addl    %eax, %edi
   shrl    $4, %edi
   xorl    %esi, %edi
   movl    %edi, %eax
   xorl    %ecx, %eax
   imulq   $842937507, %rax, %rsi          # imm = 0x323E34A3
   shrq    $32, %rsi
   subl    %esi, %eax
   shrl    %eax
   addl    %esi, %eax
   shrl    $6, %eax
   xorl    %edi, %eax
   incl    %ecx
   cmpl    $1000000000, %ecx               # imm = 0x3B9ACA00
   jne .LBB3_1
```

遅いdiv命令を避けて乗算と加減算やビットシフトの組み合わせのなかなか複雑なコードが生成されてますね。次は改良版です。

```asm
test:
    movl    %edi, %eax
    xorl    %ecx, %ecx
    movabsq $2635249153617166336, %rsi      # imm = 0x24924924A0000000
    movabsq $970881267157434368, %rdi       # imm = 0xD79435E58000000
    movabsq $172399477334736896, %r8        # imm = 0x2647C6946000000
    .p2align    4
.LBB3_1:
    movl    %ecx, %edx
    xorl    %eax, %edx
    mulxq   %rsi, %r9, %r9
    xorl    %eax, %r9d
    movl    %r9d, %edx
    xorl    %ecx, %edx
    mulxq   %rdi, %r10, %r10
    xorl    %r9d, %r10d
    movl    %r10d, %edx
    xorl    %ecx, %edx
    mulxq   %r8, %rax, %rax
    xorl    %r10d, %eax
    incl    %ecx
    cmpl    $1000000000, %ecx
    jne .LBB3_1
```
ループ内で分岐処理以外が28命令だったものが12命令に減りました。

M4でもみてみましょう。

```sh
llc -mtriple=arm64-apple-macosx -mcpu=apple-m4 bench.ll -o 出力ファイル.s
```

オリジナルのメインループ

```asm
LBB3_1:
    eor w13, w8, w0
    umull   x14, w13, w9
    lsr x14, x14, #32
    sub w13, w13, w14
    add w13, w14, w13, lsr #1
    eor w13, w0, w13, lsr #2
    eor w14, w13, w8
    umull   x15, w14, w10
    lsr x15, x15, #32
    sub w14, w14, w15
    add w14, w15, w14, lsr #1
    eor w13, w13, w14, lsr #4
    eor w14, w13, w8
    add w8, w8, #1
    umull   x15, w14, w11
    cmp w8, w12
    lsr x15, x15, #32
    sub w14, w14, w15
    add w14, w15, w14, lsr #1
    eor w0, w13, w14, lsr #6
    b.ne    LBB3_1
```

改良後のメインループ

```asm
LBB3_1:
    eor w13, w8, w0 // i ^ ret
    umulh   x13, x13, x9 // div7(i ^ ret);
    eor w13, w13, w0 // ret ^= div7(i ^ ret);
    eor w14, w13, w8 // i ^ ret
    umulh   x14, x14, x10 // div19(i ^ ret);
    eor w13, w14, w13 // ret ^= div19(i ^ ret);
    eor w14, w13, w8 // i ^ ret
    add w8, w8, #1 // i++
    umulh   x14, x14, x11 // div107(i ^ ret);
    cmp w8, w12
    eor w0, w14, w13 // ret ^= div107(i ^ ret);
    b.ne    LBB3_1
```

こちらも効果は歴然です。
x64/M4どちらも排他的論理和のxor/eorとループカウンタと条件分岐を除けば定数除算が乗算命令mulx/umulhだけになっているのが分かります。すばらしい。
リリース版に入るのが楽しみです。

## 背景
このPRを作ることになった背景を紹介します。
定数整数除算を乗算といくつかの加減算やビットシフトに置き換える最適化は昔から知られています。
たとえばx/3を`x*0xAAAAAAAB >> 33`のように置き換える方法が知られています。このときに現れるマジック定数は除数に依存して決まります。

コンパイラに組み込まれているのは["Division by Invariant Integers using Multiplication"(Granlund, Montgomery, SIGPLAN 1994)](https://gmplib.org/~tege/divcnst-pldi94.pdf)（以下GM法）や『ハッカーのたのしみ』の「整数定数による除算」の節の方法で、マジック定数の決め方も載っています。x/7ではマジック定数が33ビットになり、それを32ビットCPUでうまく扱う方法も載っています。

私は暗号ライブラリ[mcl](https://github.com/herumi/mcl)の研究開発をしていて、除算の高速化にも関心がありいろいろ調べていました。
上記GM法なども20年以上前から知っていたのですが、最近のコンパイラで試したところCPUが64ビットになっているにも係わらず実装方法が変わってないことに気がつきました。

それで、64ビットCPU向けに最適化できる方法をいくつか試し、それらが有効だったので[Kernel/VM探検隊@東京 No18](https://kernelvm.connpass.com/event/355100/)で[紹介](https://zenn.dev/herumi/articles/const-div-revised3)したり、それを更に改善してブログ「[定数除算最適化再考4 更なる高速化](https://zenn.dev/herumi/articles/const-div-revised4)」に書いたり、[2026年 暗号と情報セキュリティシンポジウム](https://www.iwsec.org/scis/2026/)で発表したりしました。

x64/M4向けの実行時コード生成するライブラリ[constdiv](https://github.com/herumi/constdiv)も作ってみました。
しかし、やはり本丸は既存のコンパイラです。それを改善できればいいなと開発を始めました。

## 生成AIとの共同作業
まずは[llvm-project](https://github.com/llvm/llvm-project)から改善すべき箇所を探します。定数整数除算やHacker's Delightなどのキーワードで該当個所の見当をつけます。その結果、[llvm/lib/CodeGen/SelectionDAG/TargetLowering.cpp](https://github.com/herumi/llvm-project/blob/0f89e3899502f7f7ddde21215a6ee13e036cae55/llvm/lib/CodeGen/SelectionDAG/TargetLowering.cpp#L6739)のBuildUDIVあたりをいじればよさそうだと分かりました。
次は今回提案したい手法を埋め込みます。

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

ただ、簡単ではないだろうなと思いました。何故なら独自の多数の型や構文木をいじるメソッドなど調べて使えるようになるのに多大な時間がかかるからです。
しかし今は生成AIがあります。そこでconstdivに書いた[手法とその結果](https://github.com/herumi/constdiv#our-approach)をAIに読ませて、このように変更せよと指示しました。

AIはすごいですね。何度かのやりとりのすえ、見事に私が欲しいasm出力をする修正パッチを作ってくれました。
もちろん手直しや、細かい作業は必要でしたが想像していたよりもずっと短期間でできてしまいました。びっくりです。

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

## 水平展開
最初x64で生成コードを確認していましたが、今回の手法はApple M4でも有効なのは分かっているのでそちらでもテストするように依頼します。
LLVMの上位での最適化手法なので、M4でも一発で望みのコードが生成されました。期待と驚きが混ざった感じです。それから64ビットのRISC-Vでも同様にうまくいってることを確認しました。

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

さて、GCCの最適化がLLVMに比べて甘かったという話ですが、GCCはどうやらこの事実を知らないようで、mulqよりも効率がよくなるはずのmulxを使うように指示しても冗長なmovが必要と判断してmulxを使いません。
そこで3万行以上あるx86の最適化ノウハウ集みたいなもの[gcc/config/i386/i386.md](https://gcc.gnu.org/git/?p=gcc.git;a=blob;f=gcc/config/i386/i386.md;h=8636b0168785fb2e73f10bc929ddd4f28cb88d3e;hb=HEAD)にumulh相当のパターンを追加すると（もちろん生成AIに指示したのですが）LLVMと同等のコードが生成されるようになりました。

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
mulx    %rax, %rdx, %rax      # rdx*rax → rdx(捨て), rax(結果)
```

今後はこのあたりをまとめてプルリクエストを出す予定です｡
