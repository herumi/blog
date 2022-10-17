---
title: "有限体の実装2（減算）"
emoji: "🧮"
type: "tech"
topics: ["有限体", "sub", "llvm", "x64"]
published: true
---
## 初めに

前回は有限体の加算を実装しました。今回は減算を実装します。
実装する関数は

```cpp
// Unitは64ビットならuint64_t, 32ビットならuint32_t
// pz[] = (px[] - py[]) % pp[]
void fp_subN(Unit *pz, const Unit *px, const Unit *py, const Unit *py, const Unit *pp);
```
です。

## LLVM DSLによる実装

前回[加算の「最適化への準備」](https://zenn.dev/herumi/articles/finite-field-01-add#%E6%9C%80%E9%81%A9%E5%8C%96%E3%81%B8%E3%81%AE%E6%BA%96%E5%82%99)で紹介したPythonのコードを少し変形します。

```python
def select(cond, x, y):
  return x if cond else y

def fp_sub(x, y):
  x -= y
  return x + select(x < 0, p, 0)
```

これを[多倍長整数の実装7（XbyakライクなPython DSLによるasmコード生成）](http://localhost:8000/articles/bitint-07-gen-asm)で紹介したDSLで実装します。

```cpp
Operand pz(IntPtr, unit);
Operand px(IntPtr, unit);
Operand py(IntPtr, unit);
Operand pp(IntPtr, unit);
// pz[] = (px[] - py[]) % pp[]
// void fp_sub(Unit *pz, const Unit *px, const Unit *py, const Unit *py, const Unit *pp);
auto f = Function("fp_sub", Void, pz, px, py, pp);
beginFunc(f);
Operand x = loadN(px, N);
Operand y = loadN(py, N);
x = zext(x, bit + 1);
y = zext(y, bit + 1);
Operand v = sub(x, y);
Operand c = trunc(lshr(v, bit), 1);
Operand p = loadN(pp, N);
c = select(c, p, makeImm(bit, 0));
v = add(trunc(v, bit), c);
storeN(v, pz);
ret(Void);
endFunc();
```

```cpp
Operand pz(IntPtr, unit);
Operand px(IntPtr, unit);
Operand py(IntPtr, unit);
Operand pp(IntPtr, unit);
```
ポインタ変数pz, px, py, ppを定義します。

```cpp
auto f = Function("fp_sub", Void, pz, px, py, pp);
beginFunc(f);
...
endFunc();
```
関数fを定義します。

```cpp
Operand x = loadN(px, N);
Operand y = loadN(py, N);
x = zext(x, bit + 1);
y = zext(y, bit + 1);
Operand v = sub(x, y);
```

px, pyからN個分の配列の値を読み込み、x, yとして`v = x - y`とします。`x = zext(x, bit + 1);`とx, yを1ビット増やしているのは引き算の繰り下がりを表現するためです。v = x - yの最上位ビットが立っていればx < yだったということです。

```cppp
Operand c = trunc(lshr(v, bit), 1); // (v >> bit) & 1
```
でその1ビットを取り出し、

```cpp
Operand p = loadN(pp, N);
c = select(c, p, makeImm(bit, 0)); // c = c ? p : 0
```
でc = pか0とします。

```cpp
v = add(trunc(v, bit), c);
```
で1bit増やしたvを元の長さに戻してcを加算します。あとは`storeN`でメモリに書き戻します。
後はこのコードを実行してLLVM-IRを生成し、各CPUアーキテクチャごとに最適なasmコードを生成すればOKです。ここまで準備しておけば簡単ですね。

## x64向け最適化と細かい話

x64ではCFを使えばよいので1bit増やす必要はありません。LLVMにおけるselect(x < 0, p, 0)を実現するにはいくつか方法があります。
`x -= y`の実行直後はCFが設定されています（x < 0ならCF=1、そうでなければ0）。予めT（N個のレジスタの組）にpをloadしておき、cmovncで0を入れます。cmov命令は即値を取れないので補助レジスタ（たとえばrax）を使います。
コード中の`_pm`の`p`はレジスタの配列(packed)、`m`はメモリ、`_pr`の`r`は1個のレジスタを表します（私のアセンブラのDSLでの記号）。

```cpp
// X=[]にx -= yの実行結果が入っている(CF = x < 0 ? 1 : 0)
mov(eax, 0);
load_pm(T, pp); // T = []にpの値を入れる
cmovnc_pr(T, rax); // T = CF ? p : 0
add_pp(X, T);   // X += T
```

eaxを0クリアするのに`mov(eax, 0)`を使っていることに注意してください。通常よりバイト長の短い`xor_(eax, eax)`を使いますが（movだと5バイト、xorだと2バイト）、ここでは直前に設定されたCFを変更したくないのでmovを使います。
なおxorを使って0クリアする場合は`xor_(rax, rax)`よりも`xor_(eax, eax)`の方がよいです。64bit環境で32bitの演算をすると上位が0クリアされるため両者は同じ挙動だけれども、eaxを使う方が1バイト短いからです。

次に、Tを0クリアするならcmovよりもandを使う方がよいです。上記と同等なコード

```cpp
// X=[]にx -= yの実行結果が入っている(CF = x < 0 ? 1 : 0)
sbb(rax, rax);  // rax = CF ? -1 : 0
load_pm(T, pp); // T = []にpの値を入れる
and_pr(T, rax);
add_pp(X, T);   // X += T
```

`sbb(rax, rax)`は`rax -= rax + CF`の意味なので`rax = CF ? -1 : 0`となります（-1は全てのビットが立っているという意味）。

SkylakeXの実行時間（Agner氏の[Instruction tables](https://www.agner.org/optimize/instruction_tables.pdf)）を見るとcmov系命令よりもand命令の方が同時実行できる可能性が高いからです。

命令|オペランド|μops fused|μops unfused|ポート|レイテンシ|スループットの逆数
-|-|-|-|-|-|-
CMOVcc|r,r|1|1|p06|1|0.5
AND|r,r|1|1|p0156|1|0.25

別の方法もあります。素数pの値の配列だけでなく、0の値の配列も用意しておき、CFの値に応じてTに読み込むアドレスを切り換えます。こうすると必要な一次レジスタのセットTが不要になります。Nが大きいときは効果的です。残念ながらfp_addについてはこの手法は使えません。

```cpp
// X=[]にx -= yの実行結果が入っている(CF = x < 0 ? 1 : 0)
lea(rax, ptr[rip + zeroL]); // zeroへのアドレス
cmovc(rax, pp);   // X < 0 ? pp : 0
add_pm(X, rax);
store_mp(pz, X);
```

## pがフルビットでない場合の最適化

ここで「pがフルビットである」とは素数pのビット長がUnitのビットサイズの倍数（64ビットなら64）という意味とします（私のここだけの造語）。
LLVM DSLの節で紹介したように任意のビット長の素数を考慮するなら、1ビット増やす必要があります。しかし、たとえばN = 4でpが255ビットの素数なら、最上位ビットが空いているので増やす必要がありません。フルビットか否かを表すフラグisFullBitを使うとfp_subは次のように最適化できます。

```cpp
Operand x = loadN(px, N);
Operand y = loadN(py, N);
if (isFullBit) {
  x = zext(x, bit + 1);
  y = zext(y, bit + 1);
}
Operand v = sub(x, y);
Operand c;
if (isFullBit) {
  c = trunc(lshr(v, bit), 1);
  v = trunc(v, bit);
} else {
  c = trunc(lshr(v, bit - 1), 1);
}
Operand p = loadN(pp, N);
```

この最適化はfp_addやfp_subだけでなく様々な場面に適用できます。
昔はpを256ビットなどのフルビットでパラメータ探索していたのですが、私たちの実装論文[High-Speed Software Implementation of the Optimal Ate Pairing over Barreto-Naehrig Curves](https://eprint.iacr.org/2010/354)以降はフルビットでない素数が使われる場面が増えたように思います。
1ビット減ったところで攻撃時間は精々半分にしか減らないけれども現在の実行速度は割と改善されるからです。ペアリングでよく使われるBLS12-381曲線や電子署名で使われるed25519もフルビットではありません。
