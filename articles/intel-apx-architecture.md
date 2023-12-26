---
title: "Intel APXの解説"
emoji: "📖"
type: "tech"
topics: ["APX", "x64", "EVEX拡張"]
published: false
---
## 初めに
Intel APXは既存命令の拡張と新しい命令群のセットです。
今月始め(2023/12/1)、拙作の[Xbyak](https://github.com/herumi/xbyak)をAPX対応させて、とっても詳しくなった（自称）ので、改めて解説します。
APXは主に次の特徴を持ちます。
- 16個の汎用レジスタGPR r16, ..., r31が追加された。
- 多くの命令に新しいデータ宛て先NDDが追加されて3オペランド対応した。
- 新しい条件命令が追加された。
- 雑多な機能拡張
  - フラグ更新抑制フラグ
  - setCC/imulのゼロ拡張

## 新しい汎用レジスタ
従来x64ではrax, rbx, rcx, rdx, rsi, rdi, rbp, rsp(スタックポインタ)とr8, ..., r15の16個の汎用レジスタがありました。
APXではそれに加えてr16, ..., r31の64ビットレジスタが追加されて倍増しました。これでARM64やRISC-Vなみの汎用レジスタ数となります。
従来と同様、64ビットレジスタの下位32ビット, 16ビット, 8ビットをそれぞれ

- r16d, ..., r31d
- r16w, ..., r31w
- r16b, ..., r31b

でアクセスできます。`add(r30, r31)`や`lea(r20, ptr[r16+r8])`のように使えます。
レジスタが増えて複雑な計算中のメモリ退避回数を減らせるため、楕円曲線暗号の実装などで高速化が見込まれます。

## 3オペランド対応
`add`や`xor`などの基本的な算術・論理演算命令やシフト命令が3オペランド対応しました。
`add(dst, src1, rsc2)`で`dst = src1 + src2`を意味します。
もちろん追加された汎用レジスタも使えるので`add(r20, r21, r22)`と書けます。これだけ見るとRISC系CPUと区別がつかないですね。

## 新しい条件命令ccmpSCCとctestSCC
従来の比較命令cmpやテスト命令testの条件つき命令が追加されました。条件にしたがって`eflags`（のOF, SF, ZF, CF）を更新します。
`SCC`の部分は`b` (blow), `lt` (less than)などの分岐命令の条件コードCC (Condition Code)が入ります。SCCはSource CCの略です。
ccmpSCCは`ccmpSCC(op1, op2, dfv)`という形で使い、op1, op2はレジスタ, メモリ, 即値のいずれかで`cmp(op1, op2)`が有効な組合せ, dfv (default flags value)は0から15までの定数値（後述）です。
ctestSCCも同様に`ctestSCC(op1, op2, dfv)`という形をとります。

```python
cmovCC(op1, op2): # 従来のcmov（比較のため）
  if eflags == CC:
    mov(op1, op2)
```

```python
ccmpSCC(op1, op2, dfv = 0):
  eflags = cmp(op1, op2) if eflags == SCC else dfv
```

```python
ctestSCC(op1, op2, dfv = 0):
  eflags = test(op1, op2) if eflags == SCC else dfv
```

これらの命令は複雑な条件チェックにおける分岐命令を削減します。
たとえばレジスタr1, r2に対して`rax = (r1 > 3) && ((r2 & 1) == 0);`としたいとしましょう。
従来ならば次のように書く必要がありました。

```cpp
   xor_(eax, eax); # rax = 0
   cmp(r1, 3);
   jbe(endL);
   and(r2, 1);
   setz(al); // al = 1 if (r2 & 1) == 0
L(endL);
```

setCCを使って分岐命令を一つ減らしましたが、`a == 1 && b == 2 && c == 3`みたいな条件なら、2回は分岐する必要があります（凝ったテクニックを使わなければ）。
これに対して新しい条件命令を使うと次のように書けます。

```cpp
xor_(eax, eax);
cmp(r1, 3);
ctesta(r2, 1); // eflags = (r1 > 3) ? (r2 & 1) : dfv(=0);
setz(al); // rax = 1 if elfags == ZF else 0
```

まずr1と3を比較してeflagsを更新し、それが`a` (above)のときに`eflags = r2 & 1`をセットします。
そうでないときは`eflags = dfv = 0`です。更新後のeflags対して、`ZF=1`なら`rax = 1`とします。
Xbyakではdfvのデフォルト値は0としました。一般には`T_of` (=8), `T_sf` (=4), `T_zf` (=2), `T_cf` (=1)のor値を設定します。

先程の`a == 1 && b == 2 && c == 3`も

```cpp
xor_(eax, eax);
cmp(p1, 1);
ccmpe(p2, 2);
ccmpe(p3, 3);
sete(al);
```

と簡潔に書けます。すばらしい。

## メモリ例外を起こさない条件mov命令cfcmovCC
従来の`cmovCC`は、たとえば`cmova r, mem`のとき、条件`a`が成り立たないときはレジスタrは変更されませんが`mem`はメモリread可能でなければなりませんでした。
cfcmovはその制約を回避し、条件が成立しないときはメモリアクセスしません。`cf`はconditional faultingの略。

```cpp
cfcmovCC(r, ptr[rax]); // if (CC) { r = [rax]; } else { r = 0; }
```
3オペランドにも対応し、

```cpp
cfcmovCC(dst, r, ptr[rax]); // if (CC) { dst = [rax]; } else { dst = r; }
```

となります。`r`と`ptr[rax]`の順序に注意。条件が成立したときは3番目の引数の値、不成立なら2番目の引数の値です。
従来のcmovCCも3オペランド拡張されていますが、条件に関わらず常にメモリアクセスは発生します。

```cpp
cmovCC(dst, r, ptr[rax]); // tmp = [rax]; if (CC) { dst = tmp; } else { dst = r; }
```

これは、条件に限らず同じ時間で実行することでタイミング攻撃などのサイドチャネル攻撃を防ぐ暗号用のコードを書くときに利用します。

## フラグ更新抑制
x86は基本的にほぼ全ての演算命令で、演算後の値に応じてeflagsが更新されていました。
特に多倍長演算の実装では、その仕様が足かせになることがあり、`mulx`や`shlx`などのフラグを更新しない命令が追加されてきました。
APXではadd, sub, mul, xorなどの命令が3オペランド対応になると同時にフラグ更新を抑制するフラグNF(no flags = status flags update suppression)を指定できるようになりました。
アセンブリ言語レベルで、どういう文法になるかは現状では決まっていない（2023/12/1?時点）のでXbyakではAVX-512ライクに`T_nf`をつける文法としました。
`add(r10, r11, r12);`と異なり`add(r10|T_nf, r11, r12);`はeflagsを更新しません。

## setCCのゼロ拡張
条件CCに従ってレジスタを1か0に設定するsetCC命令は上位ビットを変更しないため事前にゼロクリアする必要があり、非常に使い勝手が悪いものでした。
それが上位ビットをゼロクリアするフラグZU(zero upper)を指定できるようになりました。こちらも指定文法が不明瞭なため、Xbyakでは`T_zu`をつけることにしました。
`setz(al|T_zu)`は`rax = (ZF == 1)`という意味になります。`setCC`の他に何故か`imul`だけが`T_zf`を指定できるようになっています。

ccmpSCCの説明で紹介した

```cpp
xor(rax, rax);
cmp(p1, 1);
ccmpe(p2, 2);
ccmpe(p3, 3);
sete(al);
```

も`T_zu`を使うと

```cpp
cmp(p1, 1);
ccmpe(p2, 2);
ccmpe(p3, 3);
sete(al|T_zu);
```

と書けます。

## まとめ
Intel APXの拡張された機能を紹介しました。汎用レジスタ倍増、3オペランド対応の他にフラグ回りの大きい拡張、細かい改良があります。
CPUとコンパイラが対応したら再コンパイルするだけで性能向上が見込まれるというのも納得できそうです。
