---
title: "多倍長整数の実装7（XbyakライクなPython DSLによるasmコード生成）"
emoji: "🧮"
type: "tech"
topics: ["mulx", "adox", "xbyak", "Python", "DSL"]
published: true
---
## 初めに

前回[多倍長整数の実装6（乗算後加算とintrinsicの限界）](https://zenn.dev/herumi/articles/bitint-06-muladd)では、コンパイラのintrinsic関数の限界を紹介しました。
仕方がないのでアセンブリ言語（以下asmと表記）で実装しなければなりません。
今回はその作業を手助けするPythonでアセンブリコードを生成する簡単なDSLを作ったのでそれを紹介します。
記事全体の一覧は[多倍長整数の実装1（C/C++）](https://zenn.dev/herumi/articles/bitint-01-cpp)参照。

## 動機

私はC++でアセンブリ言語(以下asm)レベルでの実行時コード生成をやりたくて[Xbyak](https://github.com/herumi/xbyak)を開発しています。
これはもちろんJITコード生成がメインの機能なのですが、自分の慣れているC++の文法でasmコードを記述できるのは想像以上に便利でした。
開発&自分で使い始めて15年、もう通常のnasmやgasでasmを記述する気にはなれません。
それぞれ個別の擬似命令やマクロのやりかたを調べるのが面倒だからです。

ただし、Xbyakは実行時コード生成なので、今回のような静的なコードを書くときにはオーバースペックです。
しかし、静的なasmを書くときも個別のアセンブラの文法ではなく、よく知っている(C++やPython)言語の文法で書きたいのです。
そこで、多倍長整数の実装に必要な最低限の機能だけを使えるようにしたPythonのDSLを作りました。
それが[gen_x86asm.py](https://github.com/herumi/mcl/blob/master/src/gen_x86asm.py)です。

## gen_x86asm.pyの紹介

前述のようにまだ本当に最小限の機能しかありませんが、次のことができます。

- LinuxのAMD64とWin64の呼び出し規約に対応
- gas, NASM, MASM(ml64.exe)対応コード生成

最初はNASMだけでよいだろうと思ったのですが、標準で使えるgasやMASMをサポートしているとbuild時に何かと便利だったのでサポートしました（違いを吸収するのは結構面倒なのですが）。

## addの例

とりあえずサンプルを見ましょう。

```python
from gen_x86asm import *
import argparse

def gen_add(N):
  align(16)
  with FuncProc(f'mclb_add{N}'):
    with StackFrame(3) as sf:
      z = sf.p[0]
      x = sf.p[1]
      y = sf.p[2]
      for i in range(N):
        mov(rax, ptr(x + 8 * i))
        if i == 0:
          add(rax, ptr(y + 8 * i))
        else:
          adc(rax, ptr(y + 8 * i))
        mov(ptr(z + 8 * i), rax)
      setc(al)
      movzx(eax, al)

parser = argparse.ArgumentParser()
parser.add_argument('-win', '--win', help='output win64 abi', action='store_true')
parser.add_argument('-m', '--mode', help='output asm syntax', default='nasm')
param = parser.parse_args()

setWin64ABI(param.win)
init(param.mode)

segment('text')
gen_add(3)
term()
```

これはN桁同士の足し算関数`Unit add(Unit *z, const Unit *x, const Unit *y);`を生成するコードです。

```python
setWin64ABI(true or false)
```
Win64 ABIを使うかLinuxなどのAMD64のABIを使うかを設定します。

```python
init(mode) # mode in ['gas', 'nasm', 'masm']
```
出力するアセンブリ言語を指定します。

```python
segment('data')
```
で次から関数（コード）を記述することを指定し、`gen_add(3)`で3桁同士のadd関数を生成します。

```python
  with FuncProc(f'mclb_add{N}'):
```
で関数の始まりを示し、

```python
    with StackFrame(3) as sf:
```
で引数が3個のスタックフレームを生成します。

```python
      z = sf.p[0]
      x = sf.p[1]
      y = sf.p[2]
```

`sf.p[i]`は関数の`i`番目のレジスタを意味します。分かりやすいように名前をつけます。

```python
      for i in range(N):
        mov(rax, ptr(x + 8 * i))
        if i == 0:
          add(rax, ptr(y + 8 * i))
        else:
          adc(rax, ptr(y + 8 * i))
        mov(ptr(z + 8 * i), rax)
```

[多倍長整数の実装2（Xbyak）](https://zenn.dev/herumi/articles/bitint-02-xbyak)で紹介したように

```python
mov(rax, ptr(x + 8 * i))
```
でraxに`x[i]`の値を読み込みます。Xbyakの場合は`ptr[...]`ですがPythonでは`ptr(...)`となっています。

```python
      setc(al)
      movzx(eax, al)
```
出力値を設定します。sfを抜けるところで`ret`が挿入されます。

## 出力例

このファイル(gen_add.py)に対して

```bash
python3 gen_add.py -m gas
```
とすると

```asm
# for gas
.text
.align 16
.global mclb_add3
.global _mclb_add3
mclb_add3:
_mclb_add3:
mov (%rsi), %rax
add (%rdx), %rax
mov %rax, (%rdi)
mov 8(%rsi), %rax
adc 8(%rdx), %rax
mov %rax, 8(%rdi)
mov 16(%rsi), %rax
adc 16(%rdx), %rax
mov %rax, 16(%rdi)
setc %al
movzx %al, %eax
ret
```

が出力されます。

```asm
.global mclb_add3
.global _mclb_add3
```
は関数`mclb_add3`が外部からアクセスできるようにします。
アンダースコアがついてるのとついていないのがあるのはLinuxとmacOSでその対応が異なるからです。
マクロやオプションで切り換えられるようにするか迷ったのですが、簡便さを優先しました。

```asm
mov (%rsi), %rax
add (%rdx), %rax
mov %rax, (%rdi)
mov 8(%rsi), %rax
adc 8(%rdx), %rax
...
```

ちゃんとAMD64のABIに従ったレジスタが使われているのが分かります。
次に

```batch
python3 gen_add.py -m masm -win
```
とすると

```nasm
; for masm (ml64.exe)
_text segment
align 16
mclb_add3 proc export
mov rax, [rdx]
add rax, [r8]
mov [rcx], rax
mov rax, [rdx+8]
adc rax, [r8+8]
mov [rcx+8], rax
mov rax, [rdx+16]
adc rax, [r8+16]
mov [rcx+16], rax
setc al
movzx eax, al
ret
mclb_add3 endp
_text ends
end
```

が出力されました。
gasでは`.text`だったところが`_text segment`になっていたり、

```nasm
mclb_add3 proc export
```

と関数をexportする部分が違っていることが分かります。

```nasm
mov rax, [rdx]
add rax, [r8]
mov [rcx], rax
mov rax, [rdx+8]
...
```
レジスタもWin64のABIに従ったものになっています。

コードの終わりもちゃんと

```nasm
mclb_add3 endp
_text ends
end
```
と出力されています。
便利ですね（自画自賛）。

## mulUnitAddの場合

前回[多倍長整数の実装6（乗算後加算とintrinsicの限界）](https://zenn.dev/herumi/articles/bitint-06-muladd)で諦めた、mulxとadoxを組み合わせた`mulUnitAdd(z, x, y)`という関数（z[] += x[] * yという意味）は次のように書けます。

```python
with FuncProc(f'mclb_mulUnitAdd{N}'):
  with StackFrame(3, 2, useRDX=True) as sf:
    z = sf.p[0]
    x = sf.p[1]
    y = sf.p[2]
    t = sf.t[0]
    L = sf.t[1]
    mov(rdx, y)
    xor_(eax, eax)
    mov(t, ptr(z))
    for i in range(N):
      mulx(rax, L, ptr(x + i * 8))
      adox(t, L)
      mov(ptr(z + i * 8), t)
      if i == N-1:
        break
      mov(t, ptr(z + (i+1) * 8))
      adcx(t, rax)
    mov(t, 0)
    adcx(rax, t)
    adox(rax, t)
```

`StackFrame(3, 2, useRDX=True)`の説明だけしておきましょう。
これは関数の引数を3個、一時レジスタを2個、rdxは明示的に利用することを指定します。
一時レジスタは

```python
t = sf.t[0]
L = sf.t[1]
```
のように好きな名前をつけて利用します。
変更してはいけないレジスタ使うと自動的にスタックに退避・復元されます。

`useRDX=True`を指定しないとABIで指定された引数がrdxを使っているとバッティングしておかしくなります。
他の例は[gen_bint_x64.py](https://github.com/herumi/mcl/blob/dev/src/gen_bint_x64.py)を参照してください。

## mclでの利用方法

以上でadd, sub, mulUnit, mulUnitAddを作りました。
これらの関数は[mcl](https://github.com/herumi/mcl)の中では
`addT<N>`や`mulUnitT<N>`などの形で利用できます（名前空間はmcl::bint）。

N桁とN桁の乗算も

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

のままで動きます。

## ベンチマーク

ペアリングや楕円曲線暗号では256bit×256bit→512bitや384bit×384bit→768bitの乗算を使います（上記`mulT<4>`や`mulT<6>`に相当）。
暗号の実装でよく使われる[GNU GMP](https://gmplib.org/)のmpn_mul_nと比較してみましょう。

環境は

- CPU : Xeon Platinum 8280 CPU 2.70GHz (turbo boost off)
- OS : Ubuntu 20.04.4 LTS
- compiler : gcc 9.4.0

です。

|mulT<N>|GMP|mcl (clk)|
|-|---|---|
|N = 4|137|92|
|N = 6|251|206|

1.2~1.5倍速い感じです。
お気軽な実装の割にはなかなかよい性能ですね（がちで実装すればこの数倍速くできます。それはまたの機会に）。
