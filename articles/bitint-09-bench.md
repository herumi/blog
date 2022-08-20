---
title: "多倍長整数の実装9（手書きasmとLLVMのベンチマーク）"
emoji: "🧮"
type: "tech"
topics: ["llvm", "x64", "aarch64"]
published: true
---
## 初めに

今まで長らく多倍長整数の加算と乗算について標準C/C++の範囲で出来ること、intrinsicを使った場合とその限界、XbaykやLLVMによる実装方法の紹介をしました。
今回は一区切りのまとめとしてN桁xN桁の乗算（mulPre）のベンチマークを取って考察します。
記事全体の一覧は[多倍長整数の実装1（C/C++）](https://zenn.dev/herumi/articles/bitint-01-cpp)参照。

## LLVMの生成コード

まず前回紹介したLLVMの生成コードを詳しく見てみましょう。
[opti/llvm/mulPre](https://github.com/herumi/opti/tree/master/llvm/mulPre)にサンプルコードを置きました。

```
make asm
```
で生成された`mulpre.s`の`mcl_fpDbl_mulPre4L`という関数がN=4のときのmulPreです（コンパイルオプションは`-S -masm=intel -O2 -mbmi2 mulpre.ll`）。
push/pop/retを除いた命令数を数えると

mulx|add系(+setb)|mov|その他
-|-|-|-
16|31|36|0

でした。N桁の乗算の命令数はNxN=16、加算の命令数はNxN+(N+1)(N-1)=31と最小ですがmovが多いです。実際、コードを確認すると「8-byte spill」という文字列が現れています。一時変数をレジスタに退避できずにスタック上のメモリと読み書きが多発しています。Nが大きくなるとこの傾向は強くなります。

コードの一部を抜粋

```nasm
mov qword ptr [rsp - 56], rsi       # 8-byte Spill
adc r12, r9
adc rcx, r8
adc rbp, r11
adc rbx, 0
mov rdx, qword ptr [rsp - 40]       # 8-byte Reload
mov rdx, qword ptr [rdx + 24]
mulx    r8, r10, qword ptr [rsp - 24]   # 8-byte Folded Reload
mulx    r9, r14, qword ptr [rsp - 48]   # 8-byte Folded Reload
mulx    r11, r15, r13
add r14, r8
adc r15, r9
mov rsi, qword ptr [rsp - 8]        # 8-byte Reload
mov qword ptr [rdi], rsi
mulx    rsi, rdx, rax
adc rdx, r11
adc rsi, 0
mov rax, qword ptr [rsp - 16]       # 8-byte Reload
```

これは[intrinsic関数とその限界](https://zenn.dev/herumi/articles/bitint-06-muladd#intrinsic%E9%96%A2%E6%95%B0%E3%81%A8%E3%81%9D%E3%81%AE%E9%99%90%E7%95%8C)で書いたadox/adcxを使わない現時点でのコンパイラの制約上しかたがないのですが、その分どうしても遅くなります。

## x64向け高速化

[mclでの利用方法](https://zenn.dev/herumi/articles/bitint-07-gen-asm#mcl%E3%81%A7%E3%81%AE%E5%88%A9%E7%94%A8%E6%96%B9%E6%B3%95)ではN桁固定長整数x, yの乗算を`mulUnit`と`mulUnitAdd`を用いて実装しました。これでもGMPより速いのですが、今回手書きasmでLLVMより速いコードを目指します。

対象のコード
```cpp
// z[2N] = x[N] * y[N]
template<size_t N>
void mulT(Unit *pz, const Unit *px, const Unit *py)
{
  pz[N] = mulUnitT<N>(pz, px, py[0]);
  for (size_t i = 1; i < N; i++) {
    pz[N + i] = mulUnitAddT<N>(&pz[i], px, py[i]);
  }
}
```

`mulUnitAdd`を繰り返し呼び出していますが、Nはせいぜい9回で固定なので展開します。そして、毎回メモリに退避・復元するところを全てレジスタ上で処理します。
必要なレジスタ数を見積もりましょう。まずN桁と1桁の分でN+1個、px, py, pzの3個、mulxを使うためのrdx, 一時変数として最低1個の合計N+6個。N = 9でも15個でx64の汎用レジスタ全部を使うとなんとかなりそうです。

mulUnitとmulUnitAddTに相当するコードをレジスタ上だけで演算するmulPackとmulPackAddを作ります。

```python
# rdx : px[0]
# pd : N桁のレジスタ配列
# py : ポインタ
# pz : 出力ポインタ
# offset : pzのどこに書き込むか

# t = y[] * rdxとしたとき最下位64bitをpz + offsetに保存する
# pd[] = t >>= 64により上位N桁が入る
def mulPack(pz, offset, py, pd):
  a = rax
  mulx(pd[0], a, ptr(py + 8 * 0))
  mov(ptr(pz + offset), a)
  xor_(a, a)
  n = len(pd)
  for i in range(1, n):
    mulx(pd[i], a, ptr(py + 8 * i))
    adcx(pd[i - 1], a)
  adc(pd[n - 1], 0)
```

```python
# rdx : px[i]
# pd : N桁のレジスタ配列
# py : ポインタ
# pz : 出力ポインタ
# offset : pzのどこに書き込むか

# [hi]:t = pd + y[] * rdxとしたとき最下位64bitをpz + offsetに保存する
# pd[] = t >>= 64により上位N桁が入る
def mulPackAdd(pz, offset, py, hi, pd):
  a = rax
  xor_(a, a)
  n = len(pd)
  for i in range(0, n):
    mulx(hi, a, ptr(py + i * 8))
    adox(pd[i], a)
    if i == 0:
      mov(ptr(pz + offset), pd[0])
    if i == n - 1:
      break
    adcx(pd[i + 1], hi)
  mov(a, 0)
  adox(hi, a)
  adc(hi, a)
```

そしてこれらを用いてmulPreを実装します。さくっと書いていますが、自分の頭の中に思い描くコードを適切に生成するコードを作るのはなかなか難しいです。今回は長らくXbyak上で開発していたのを（静的版として）移植しました。

```python
def gen_mulPreN(pz, px, py, pk, t, N):
  mov(rdx, ptr(px + 8 * 0))   # rdx = px[0]
  mulPack(pz, 8 * 0, py, pk)  # pk = py[] * rdx, pz[0] = pk[0], pk >>= 1
  for i in range(1, N):
    mov(rdx, ptr(px + 8 * i)) # rdx = px[1]
    mulPackAdd(pz, 8 * i, py, t, pk) # pk += py[] * rdx, pz[i] = pk[0], pk >>= 1
    s = pk[0]
    pk = pk[1:]
    pk.append(t)
    t = s
  store_mr(pz + 8 * N, pk)
```

このコードをN = 4で呼び出すと[mclb_mul_fast4](https://github.com/herumi/mcl/blob/master/src/asm/bint-x64-win.asm#L3382-L3457)が生成されます。
命令を数えると、

-|mulx|add系|mov|xor
-|-|-|-|-
LLVM|16|31|36|0
asm|16|31|16|4

でした（push/pop除く）。乗算はNxN=16, add系も4x4+5x3=31, movも16個でスタックへの退避はありません。これはN <= 9まで同じです。

## x64でのベンチマーク

Xeon 8280@2.7GHz (turbo boost on)でベンチマークをとりました。単位はnsecです。

N=|3|4|5|6|7|8
-|-|-|-|-|-|-
GMP|28.59|12.93|18.48|23.43|30.91|38.42
LLVM w/o mulx|7.78|10.58|15.32|22.42|30.57|58.64
LLVM w/ mulx|6.04|8.08|11.88|17.21|25.59|50.25
asm|4.48|6.19|9.66|12.31|17.77|21.97

LLVM w/ mulxはasm版と演算回数はほぼ同じですが、前節の通りmovの数の違いで手書き(?)asmが1.4倍ほど速くなっています。
楕円曲線暗号でよく使うN = 4や6のときはGMPの1.7～2倍の速さを達成しています。

## M1でのベンチマーク

次にM1 Mac (MacBook Pro 2020)でベンチマークをとりました。こちらはx64と違って手動最適化はしていないのでGMPとLLVMとの比較です。

N=|3|4|5|6|7|8
-|-|-|-|-|-|-
GMP|23.16|12.21|15.49|18.39|23.69|36.38
LLVM|6.34|5.23|8.13|11.60|15.80|20.81

LLVM版がx64の手書きasmに近い傾向です。生成されたコードを見るとほとんどレジスタスピルが発生していません。これはM1(AArch64)の汎用レジスタがx64の2倍の30個あるため今回のケースではレジスタの使い回しがうまくいったからと思われます。x64のLLVM版はN>=7がかなりぐだぐだな感じの速度になっているのとは対照的です。AArch64のアセンブラを1行も書かずにLLVMの力をうまく利用できたようです。

## まとめ

前回紹介した補助ツールによってコード生成されたLLVMの性能評価を行いました。今回のケースではAArch64のような汎用レジスタの多い環境では十分な性能を得られそうです。x64ではまだしばらくは手書きasmの方がよさそうですね。
