---
title: "有限体の実装5（PythonによるLLVM DSLの紹介）"
emoji: "🧮"
type: "tech"
topics: ["有限体", "DSL", "llvm", "Python"]
published: true
---
## 初めに

今回はPythonで作ったLLVM DSLを実装します。
記事全体の一覧は[有限体の実装一覧](https://zenn.dev/herumi/articles/finite-field-01-add#%E6%9C%89%E9%99%90%E4%BD%93%E3%81%AE%E5%AE%9F%E8%A3%85%E4%B8%80%E8%A6%A7)参照。

## LLVM DSL
[多倍長整数の実装8（LLVMを用いたasmコード生成）](http://localhost:8000/articles/bitint-08-llvm)ではC++によるLLVMコード生成の方法を紹介しました。
あれから同じ機能を持ったPythonによるLLVMコード生成DSLを作ったのでそれを紹介します。
[s_xbyak_llvm.py](https://github.com/herumi/mcl-ff/blob/main/s_xbyak_llvm.py)はDSLからLLVM IRコード（以下ll）を生成するためのPythonツールです。
もともとは、C++でJITを実現するための[Xbyak](https://github.com/herumi/xbyak)を開発していたのですが、JITじゃなくてもDSLでアセンブラを掛けるのが便利であることが分かってx64用の[s_xbyak.py](https://github.com/herumi/mcl-ff/blob/main/s_xbyak.py)を開発し（sは静的の意味）、それを今度はLLVMに適用したのです。
PythonによるDSLはコンパイル不要なので楽です。
詳しい説明は先月末に開催された[Open Source Cryptography Workshop](https://rsvp.withgoogle.com/events/open-source-cryptography-workshop)で講演した資料[Low-Level Implementation of Finite Field Operations using LLVM/Asm](https://speakerdeck.com/herumi/asm)もご覧ください。

## 有限体の加算
まだ開発中なのでDSLの文法は変わる可能性がありますが、まずは簡単な有限体の加算の実装を見ながら紹介しましょう。
Pythonによる普通の有限体の加算は次のように書けます。
`select(cond, x, y)`は`cond`が`True`のとき`x`、そうでないとき`y`を返す関数です。LLVM IRに同様の命令があるのでそれを使って分岐を排除したコードにしておきます。

```python
def select(cond, x, y):
  if cond:
    return x
  else:
    return y

# return (x + y) % p
def fp_add(x, y):
  z = x + y
  w = z – p
  return select(w < 0, z, w)
```

これに対応したLLVM IRを生成するDSLは次のように書けます（素数はコード生成時に固定する）。

```python
# unit = 64
# pはfullBitではないとする
# round(bitlen(p)/unit) = N

segment('data')
pp = makeVar('p', mont.bit, mont.p, const=True, static=True)

segment('text')
gen_fp_add('fp_add', mont.pn, pp)

def gen_fp_add(name, N, pp):
  bit = unit * N
  pz = IntPtr(unit)
  px = IntPtr(unit)
  py = IntPtr(unit)
  with Function(name, Void, pz, px, py):
    x = loadN(px, N)
    y = loadN(py, N)
    x = add(x, y)
    p = load(pp)
    y = sub(x, p)
    c = trunc(lshr(y, bit - 1), 1)
    x = select(c, x, y)
    storeN(x, pz)
    ret(Void)
```
`makeVar`は変数を作ります。今回はpは381ビットの素数です。
`pz = IntPtr(unit)`などで64ビットポインタ型であることを指定します。
`with Function(name, Void, pz, px, py):`で`Void name(pz, px, py)`型の関数の宣言をします。
`x = loadN(px, N)`でアドレス`px`から`N`個分の値を読み込んで`x`に設定します。`x`は`N * unit`ビット整数になります。`y`も同様。
`x = add(x, y)`で`x`と`y`の値を足して`x`に代入します。LLVM IRはSSAですが、DSLでは最代入できるので便利です。`p`がfullBitでないことを仮定しているので加算結果はオーバーフローしません。
`p = load(pp)`で`p`の値を読み込み`y = sub(x, p)`で`x`から`p`を引いた値を`y`に設定します。
`y`の最上位ビットを取り出すために、`bit - 1`ビット論理右シフトして1ビット整数にキャストします。
`x = select(c, x, y)`で`c = 1`なら`x`、そうでなければ`y`が`x`に設定されます。
`storeN(x, pz)`で`pz`に`x`を書き込みます。

非常に読みやすく記述できます。

## 生成コード
上記DSLをN = 3で呼び出すと次のLLVM IRが生成されます。

```
@p = internal unnamed_addr constant i384 4002409555221667393417789825735904156556882819939007885332058136124031650490837864442687629129015664037894272559787

define void @mcl_fp_add(i64* noalias %r1, i64* noalias %r2, i64* noalias %r3)
{
%r4 = bitcast i64* %r2 to i384*
%r5 = load i384, i384* %r4
%r6 = bitcast i64* %r3 to i384*
%r7 = load i384, i384* %r6
%r8 = add i384 %r5, %r7
%r9 = load i384, i384 *@p
%r10 = sub i384 %r8, %r9
%r11 = lshr i384 %r10, 383
%r12 = trunc i384 %r11 to i1
%r13 = select i1 %r12, i384 %r8, i384 %r10
%r14 = bitcast i64* %r1 to i384*
store i384 %r13, i384* %r14
ret void
}
```
そしてこのコードを`clang++-14 -S -O2 -target x86_64 -masm=intel`でasmを生成すると次のようになりました。

```nasm
mcl_fp_add_x64:
    push    r15
    push    r14
    push    r13
    push    r12
    push    rbx
    mov r9, qword ptr [rdx + 40]
    mov r8, qword ptr [rdx]
    add r8, qword ptr [rsi]
    mov r14, qword ptr [rdx + 8]
    adc r14, qword ptr [rsi + 8]
    mov r10, qword ptr [rdx + 16]
    adc r10, qword ptr [rsi + 16]
    mov r15, qword ptr [rdx + 24]
    adc r15, qword ptr [rsi + 24]
    mov r11, qword ptr [rdx + 32]
    adc r11, qword ptr [rsi + 32]
    adc r9, qword ptr [rsi + 40]
    movabs  r12, 5044313057631688021
    add r12, r8
    movabs  r13, -2210141511517208576
    adc r13, r14
    movabs  rbx, -7435674573564081701
    adc rbx, r10
    movabs  rax, -7239337960414712512
    adc rax, r15
    movabs  rcx, -5412103778470702296
    adc rcx, r11
    movabs  rsi, -1873798617647539867
    adc rsi, r9
    mov rdx, rsi
    sar rdx, 63
    cmovs   r13, r14
    cmovs   rax, r15
    cmovs   rbx, r10
    cmovs   rsi, r9
    cmovs   rcx, r11
    cmovs   r12, r8
    mov qword ptr [rdi + 32], rcx
    mov qword ptr [rdi + 40], rsi
    mov qword ptr [rdi + 16], rbx
    mov qword ptr [rdi + 24], rax
    mov qword ptr [rdi], r12
    mov qword ptr [rdi + 8], r13
    pop rbx
    pop r12
    pop r13
    pop r14
    pop r15
    ret
```
なんと64ビット6個分の配列ppの値は定数リテラルとしてコードの中に埋め込まれました。しかも引き算ではなく加算コードに変換されています。
LLVM IRのselectはcmovs命令になりました。

-taget aarch64としてM1用のコードを生成されると次のようになりました。

```asm
mcl_fp_add_x64:
    ldp x12, x11, [x1]
    mov x8, #21845
    ldp x14, x13, [x2]
    movk    x8, #17921, lsl #48
    ldp x10, x9, [x1, #16]
    ldp x16, x15, [x2, #16]
    adds    x12, x14, x12
    ldp x14, x17, [x1, #32]
    adcs    x11, x13, x11
    mov x1, #6501
    ldp x13, x18, [x2, #32]
    adcs    x10, x16, x10
    mov x16, #2523
    adcs    x9, x15, x9
    mov x15, #1319895040
    movk    x15, #1, lsl #32
    movk    x16, #2383, lsl #16
    adcs    x13, x13, x14
    movk    x15, #57684, lsl #48
    adcs    x14, x18, x17
    mov x17, #60736
    adds    x8, x12, x8
    movk    x16, #11615, lsl #32
    movk    x17, #3194, lsl #16
    mov x18, #21288
    movk    x16, #39119, lsl #48
    adcs    x15, x11, x15
    movk    x17, #46203, lsl #32
    movk    x18, #48308, lsl #16
    movk    x17, #39816, lsl #48
    adcs    x16, x10, x16
    movk    x18, #22601, lsl #32
    movk    x1, #50816, lsl #16
    movk    x18, #46308, lsl #48
    adcs    x17, x9, x17
    movk    x1, #60949, lsl #32
    adcs    x18, x13, x18
    movk    x1, #58878, lsl #48
    adcs    x1, x14, x1
    asr x2, x1, #63
    cmp x2, #0
    csel    x14, x1, x14, ge
    csel    x13, x18, x13, ge
    csel    x9, x17, x9, ge
    csel    x10, x16, x10, ge
    csel    x11, x15, x11, ge
    csel    x8, x8, x12, ge
    stp x13, x14, [x0, #32]
    stp x10, x9, [x0, #16]
    stp x8, x11, [x0]
    ret
```

こちらもselectはcselという条件代入命令になっていますね。きわめて優秀です。

次にN桁x1桁→(N+1)桁の乗算mulUnitを実装します。[多倍長整数の実装5（乗算とmulx）](https://zenn.dev/herumi/articles/bitint-05-mulx)のLLVM版です。

```cpp
// N桁のpxとyを掛けて下位のN個をpzに、最上位の1個を返り値とする関数
Unit mulUnit<N>(Unit pz[N], const Unit px[N], Unit y);
```

N = 4の例

```text
    [x3:x2:x1:x0]
X              y
 ----------------
          [H0:L0]
       [H1:L1]
    [H2:L2]
 [H3:L3]
-----------------
 [z4:z3:z2:z1:z0]
```

LLVM IRは64ビット×64ビット→128ビットの乗算しかありません。
そこでアルゴリズムは筆算と同じく`xi`×`y`を全て計算し、それらを足していきます。
また加算の繰り上がりも無いので`[L3:L2:L1:L0]`と`[H3:H2:H1:H0]`をまとめて二つの大きな整数の加算として求めます。

```text
L=[ 0:L3:L2:L1:L0]
      ＋
H=[H3:H2:H1:H0: 0]
      ↓
z=[z4:z3:z2:z1:z0]
```

補助的に64ビット整数のN個のリストからN * 64ビット整数を作るpack関数を用意しました。

```Python
# return [xs[n-1]:xs[n-2]:...:xs[0]]
def pack(xs):
  x = xs[0]
  for y in xs[1:]:
    shift = x.bit
    size = x.bit + y.bit
    x = zext(x, size)
    y = zext(y, size)
    y = shl(y, shift)
    x = or_(x, y)
  return x
```

LLVM IRでは順にゼロ拡張（zext）して左シフト（shl）しながらorします。
packを使うとmulUnitの関数は次のように書けます。

```Python
with Function(f'mulUnit{N}', z, px, y)
  Ls = []
  Hs = []
  y = zext(y, unit*2)
  for i in range(N):
    x = load(getelementptr(px, i)) # x[i]
    xy = mul(zext(x, unit*2), y)
    Ls.append(trunc(xy, unit))
    Hs.append(trunc(lshr(xy, unit), unit))
  bu = bit + unit
  L = zext(pack(Ls), bu)
  H = shl(zext(pack(Hs), bu), unit)
  z = add(L, H)
```

`for`の中で`x[i] * y`を計算し、その上位64ビットを`Hs`に、下位64ビットを`Ls`にリストとして保持します。

```Python
L = zext(pack(Ls), bu)
H = shl(zext(pack(Hs), bu), unit)
```
でpackして作った値をLとHという整数にまとめて計算します。
このようにすると予想できるかもしれませんが、このDSLから生成されたLLVM IRの後半はひたすらzext、shift, orが連続する固まりとなります。

```
...
%r43 = zext i64 %r9 to i128
%r44 = shl i128 %r43, 64
%r45 = or i128 %r42, %r44
%r46 = zext i128 %r45 to i192
%r47 = zext i64 %r12 to i192
%r48 = shl i192 %r47, 128
%r49 = or i192 %r46, %r48
%r50 = zext i192 %r49 to i256
%r51 = zext i64 %r15 to i256
%r52 = shl i256 %r51, 192
%r53 = or i256 %r50, %r52
%r54 = zext i256 %r53 to i320
%r55 = zext i64 %r18 to i320
%r56 = shl i320 %r55, 256
%r57 = or i320 %r54, %r56
%r58 = zext i320 %r57 to i384
%r59 = zext i64 %r21 to i384
%r60 = shl i384 %r59, 320
%r61 = or i384 %r58, %r60
%r62 = zext i384 %r41 to i448
%r63 = zext i384 %r61 to i448
%r64 = shl i448 %r63, 64
%r65 = add i448 %r62, %r64
ret i448 %r65
}
```

こんなコードをasmに変換して高速なものになるのか不安かもしれません。
しかし、LLVMは優秀です。N=3のとき次のx64用コードを生成しました。

```nasm
mclb_mulUnit3:
    mov rcx, rdx
    mov rax, rdx
    mul qword [rsi]
    mov r8, rdx
    mov r9, rax
    mov rax, rcx
    mul qword [rsi + 8]
    mov r10, rdx
    mov r11, rax
    mov rax, rcx
    mul qword [rsi + 16]
    add r11, r8
    adc rax, r10
    adc rdx, 0
    mov [rdi], r9
    mov [rdi + 8], r11
    mov [rdi + 16], rax
    mov rax, rdx
    ret
```

ゼロ拡張やシフト、orは不要と判断して綺麗さっぱり消えています。すごいですね。
更にmulxを使うよう-mbmi2を指定すると、

```nasm
mclb_mulUnit3:
    mulx r10, r8, [rsi]
    mulx r9, rcx, [rsi + 8]
    mulx rax, rdx, [rsi + 16]
    add rcx, r10
    adc rdx, r9
    adc rax, 0
    mov [rdi], r8
    mov [rdi + 8], rcx
    mov [rdi + 16], rdx
    ret
```

と文句無いコードとなりました。
AArch64の場合は、x64と異なり乗算は64ビット×64ビットの下位64ビットを取得するmulと、上位64ビットを取得するumulhがあります。
どちらもキャリーフラグを更新しないのでmulxに相当します。生成コードは

```asm
mulUnit3:
    ldp x9, x10, [x1]
    mov x8, x0            # pz = x8
    ldr x11, [x1, #16]    # x = [x11:x10:x9]
    umulh   x12, x9, x2   # H0 = x[0] * y
    umulh   x13, x10, x2  # H1 = x[1] * y
    mul x10, x10, x2      # L1 = x[1] * y
    mul x14, x11, x2      # L2 = x[2] * y
    umulh   x11, x11, x2  # H2 = x[2] * y
    adds    x10, x12, x10 # z1 = H0 + L1
    adcs    x12, x13, x14 # z2 = H1 + L2 + CF
    mul x9, x9, x2        # L0 = x[0] * y
    cinc    x0, x11, hs   # ret = H2 + CF
    str x12, [x8, #16]    # pz[2] = z2
    stp x9, x10, [x8]     # pz[0] = L0, pz[1] = z1
    ret
```

となり、こちらも完璧ですね。

## まとめ
Pythonで記述するLLVM IRを生成するDSLを作りました。
読み書きしやすく、なかなかよいコード生成をすることが分かりました。
次回はMontgomery乗算を実装し、その性能を評価します。
