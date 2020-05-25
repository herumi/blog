# floatの指数関数のAVX-512による実装
指数関数exp()は深層学習の活性化関数でtanhやシグモイド関数などを利用するときに使われます。
expの近似計算にAVX-512を使った方法を紹介します。

## 実装する関数
```
void expf_vC(float *dst, const float *src, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        dst[i] = std::exp(src[i]);
    }
}
```

## コード
[fmath2.hpp](https://github.com/herumi/fmath/blob/master/fmath2.hpp)にあります。

```
fmath::void expf_v(float *dst, const float *src, size_t n);
```
という関数が定義されていて、上記expf_vCと(誤差を除いて)同等の処理をします。

## ベンチマーク

### 速度
ベンチマークは[exp_v.cpp](https://github.com/herumi/fmath/blob/master/exp_v.cpp)で行いました。

`float x[16384];`に対してexpを求める計算をexpf_vCとexpf_vとで比較しました。

環境はOS : Ubuntu 19.10, CPU : Xeon Platinum 8280 2.7GHz, compiler : gcc-9.2.1 -Ofastです。

関数|std::exp|expf_v
:-|:-|:-
時間(clk)|140.8k|10.6k

clkはrdtscによるCPUクロックの計測で10倍以上高速化されています。

### 誤差

相対誤差を(真の値 - 実装値) / 真の値とします。
`std::exp(float x)`を真の値としてx = -30から30まで1e-5ずつ増やして計算した値の相対誤差の平均を出すと2e-6となりました。

## アルゴリズム

### exp(x)の性質
指数関数exp(x)は数学的には

exp(x) = 1 + x + x^2/2! + x^3/3! + ... + x^n/n! + ...

と定義されています。
xはマイナス無限大からプラス無限大までとりえます。

ここで`!`は階乗の記号で`4! = 4 * 3 * 2 * 1`です。
定義は無限個の値の和ですが、コンピュータではもちろん途中で打ち切って有限和で近似計算します。

xの絶対値が1より小さいときはx^nはとても小さく、更にn!で割るのでもっと小さくなります。
したがって少ない和で打ち切っても誤差は小さくてすみます。
しかしxが1より大きいとx^nはとても大きくなり、打ち切り誤差が大きくなります。
どのようにして誤差を小さくするかがポイントです。

y = a^xの逆関数をx = log_a(y)と書きます。
特にa = eのときlog_e(y) = log(y)と省略します。

- a^(x+y) = a^x a^y
- log(x^y) = y log(x)
- x^y = z^(y log_z(x)) ; 底の変換公式

などが成り立ちます。

### 計算の範囲

まずxのとり得る範囲を調べましょう。
xの型はfloatです(ここではx64が対象なのでfloatは32bit浮動小数点数とします)。
調べてみるとx = -87.3より小さいとfloatで正しく扱える最小の数FLT_MIN=1.17e-38より小さく、
逆にx = 88.72より大きいと最大の数FLT_MAX=3.4e38よりも大きくなってinfになります。
従ってxは-87.3 <= x <= 88.72としてよいでしょう。

### 近似計算
2の整数巾乗2^nはビットシフトを使って高速に計算できます。
したがってexp(x) = e^xから2の整数巾を作り出すことを考えます。

底の変換公式を使って

e^x = 2^(x log_2(e))

と変形し、x'=x log_2(e)を整数部分nと小数部分aに分割します。

x' = n + a (|a| <= 0.5)

そうするとe^x = 2^n × 2^aです。

2^nはビットシフトで計算できるので残りは2^aの計算です。
ここで再度底の変換をします。

2^a = e^(a log(2)) = e^b ; b = a log(2)とおく

|a| <= 0.5でlog(2) = 0.693なので|b| = |a log(2)| <= 0.346.

bが0に近い値なのでe^bを冒頭の級数展開を使って近似計算します。

6次の項は0.346^6/6! = 2.4e-6とfloatの分解能に近いので5次で切りましょう。

e^b = 1 + b + b^2/2! + b^3/3! + b^4/4! + b^5/5!

### アルゴリズム
結局次のアルゴリズムを採用します。

```
input : x
output : e^x

1. x = max(min(x, expMax), expMin)
2. x = x * log_2(e)
3. n = round(x) ; 四捨五入
4. a = x - n
5. b = a * log(2)
6. z = 1 + b(1 + b(1/2! + b(1/3! + b(1/4! + b/5!))))
7. return z * 2^n
```

意外と短いですね。

## AVX-512での実装

AVX-512は一つのレジスタが512ビットあるのでfloatなら512/32 = 16個まとめて処理できます。
レジスタはzmm0からzmm31まで32個利用できます。

### AVX-512の命令概略
AVX-512用の命令をまとめておきます。
詳細は[Intel64 and IA-32 Architectures Software Developer Manuals](https://software.intel.com/en-us/articles/intel-sdm)を参照ください。

アセンブリ言語の表記はIntel形式で、オペランドはdst, src1, src2の順序です。
dst, srcはdst, dst, srcの省略記法です。


命令|意味|注釈|
-|-|-|
vmovaps [mem], zmm0<br>vmovaps zmm0, [mem]|[mem] = zmm0<br>zmm0 = [mem]|memは16の倍数のメモリアドレスであること|
vmovups [mem], zmm0<br>vmovaps zmm0, [mem]|[mem] = zmm0<br>zmm0 = [mem]|memの制約はない|
vaddps zmm0, zmm1, zmm2|zmm0 = zmm1 + zmm2|floatとして<br>vsubps, vmulpsなども同様|
vminps zmm0, zmm1, zmm2|zmm0 = min(zmm1, zmm2)|floatとして|
vfmadd213ps zmm0, zmm1, zmm2|zmm0 = zmm0 * zmm1 + zmm2|floatとして|
vpbroadcastd zmm0, eax|eaxを16個分zmm0にコピーする||
vrndscaleps zmm0, zmm1, 0|zmm0 = round(zmm1)|結果はfloat型|
vscalefps zmm0, zmm1, zmm2|zmm0 = zmm1 * 2^zmm2|zmm2はfloat型をfloorする|

### 初期化

```cpp
// exp_v(float *dst, const float *src, size_t n);
void genExp(const Xbyak::Label& expDataL)
{
#ifdef XBYAK64_WIN
    const int keepRegN = 6;
#else
    const int keepRegN = 0;
#endif
    using namespace Xbyak;
    util::StackFrame sf(this, 3, util::UseRCX, 64 * keepRegN);
```

StackFrameは関数のプロローグを生成するクラスです。
3は引数が3個、UseRCXはrcxレジスタを明示的に使う指定、
zmmレジスタの保存のため64 * keepRegN byteスタックを確保します。


```cpp
    const Reg64& dst = sf.p[0];
    const Reg64& src = sf.p[1];
    const Reg64& n = sf.p[2];
```

StackFrameクラスのsf.p[i]で関数の引数のi番目のレジスタを表します。
WindowsとLinuxとで引数のレジスタが異なるのでここで吸収します。

```
    // prolog
#ifdef XBYAK64_WIN
    for (int i = 0; i < keepRegN; i++) {
        vmovups(ptr[rsp + 64 * i], Zmm(i + 6));
    }
#endif
```
AVX-512のZmmレジスタを保存します。
関数内でWindowsではzmm6以降を利用する場合は保存する必要があります。

```
    // setup constant
    const Zmm& expMin = zmm3;
    const Zmm& expMax = zmm4;
    const Zmm& log2 = zmm5;
    const Zmm& log2_e = zmm6;
    const Zmm expCoeff[] = { zmm7, zmm8, zmm9, zmm10, zmm11 };
    lea(rax, ptr[rip+constVarL]);
    vbroadcastss(expMin, ptr[rax + offsetof(ConstVar, expMin)]);
    ...
```
各種定数をレジスタにセットします。
vpbroadcastdはfloat変数1個を32個Zmmレジスタにコピーする命令です。

```cpp
    vpbroadcastd(expMin, ptr[rip + expDataL + (int)offsetof(ConstVar, expMin)]);
```

はXbyak特有の書き方です。
LabelクラスexpDataLは各種定数(ConstVarクラス)が置かれている先頭アドレスを指します。
ripで相対アドレスを利用し、Cのoffsetofマクロでクラスメンバのオフセット値を加算します。

### メインコード

```
vminps(zm0, expMax); // x = min(x, expMax)
vmaxps(zm0, expMin); // x = max(x, expMin)
vmulps(zm0, log2_e); // x *= log_2(e)
vrndscaleps(zm1, zm0, 0); // n = round(x)
vsubps(zm0, zm1); // a = x - n
vmulps(zm0, log2); // a *= log2
```

アルゴリズムの1から5行目に対応します。
vminps, vmaxpsで入力値を[expMin, expMax]の範囲内にクリッピングします。
vmulpsでlog_2(e)倍します。
vrndscalepsで整数へ最近似丸め(round)します。第3引数の0が最近似丸めを表します。
結果はfloat型です。

```cpp
vmovaps(zm2, expCoeff[4]); // 1/5!
vfmadd213ps(zm2, zm0, expCoeff[3]); // b * (1/5!) + 1/4!
vfmadd213ps(zm2, zm0, expCoeff[2]); // b(b/5! + 1/4!) + 1/3!
vfmadd213ps(zm2, zm0, expCoeff[1]); // b(b(b/5! + 1/4!) + 1/3!) + 1/2!
vfmadd213ps(zm2, zm0, expCoeff[0]); // b(b(b(b/5! + 1/4!) + 1/3!) + 1/2!) + 1
vfmadd213ps(zm2, zm0, expCoeff[0]); // b(b(b(b(b/5! + 1/4!) + 1/3!) + 1/2!) + 1) + 1
```
アルゴリズムの6行目に対応します。
vfmadd213ps(x, y, z)は積和演算命令で、x = x * y + zを実行します。

```
vscalefps(zm0, zm2, zm1); // zm2 * 2^zm1
```
`vscalefps`はAVX-512専用命令です。
上記のvrndscalepsで求めたnの2のべき乗を掛けます。
これでexp(x)の計算が終了です。

## 端数処理

floatを16個ずつ処理すると元の配列の個数nが16の倍数でないとき端数が出ます。
その処理方法について解説します。

AVX2までのSIMD命令では端数処理が苦手でした。
命令が16単位なので残り5個をレジスタに読み込むといった処理がやりにくいのです。
そのためSIMD命令を使わない通常の方法でループを回す方法をとることが多いです。

AVX-512ではそれを解決するためのマスクレジスタk1, ..., k7が登場しています。
マスクレジスタは各ビットがデータ処理する(1)かしない(0)かを指定するレジスタです。
データ処理しない場合は更にゼロで埋める(T_z)か値を変更しないかを選択できます。

たとえば

```
vmovups(zmm0|k1|T_z, ptr[src]); // zmm0 = *src;
```
でk1 = 0b11111;の場合、下位5ビットが立っているのでfloat *srcのsrc[0], ..., src[4]だけがzmm0にコピーされ、T_zを指定しているので残りはゼロで埋められます。

実装コードでの解説に戻ります。

```cpp
and_(ecx, 15); // ecx = n % 16
mov(eax, 1); // eax = 1
shl(eax, cl);  // eax = 1 << n
sub(eax, 1);   // eax = (1 << n) - 1 ; nビットのmask
kmovd(k1, eax); // マスク設定
```
ecxにループの回数nが入っているとき15とandをとり端数を得ます。
((1 << n) - 1)はデータが入っていない部分が0となるマスクです(n = 3なら0b111となる)。

```cpp
vmovups(zm0|k1|T_z, ptr[src]);
```
`vmovups(zm0, ptr[src])`はsrcからzm0に16個のfloatを読む命令ですが、
`vmovups(zm0|k1|T_z, ptr[src])`とk1でマスクすると指定したビットが立った部分しかメモリにアクセスしません。

ここで重要な点は内部的に512ビット読み込んでからゼロにするのではなく
マスクされていない領域にread/write属性が無くても例外が発生しないという点です。

安心してページ境界にアクセスできます。

## 係数の決め方

最後にアルゴリズムの6行目

```
6. z = 1 + b(1 + b(1/2! + b(1/3! + b(1/4! + b/5!))))
```

の値の改善方法について紹介します。
この式は無限に続く和を途中で打ち切ったものでした。
したがって必ず正しい値よりも小さくなります。

1/k!の値を微調整することで誤差をより小さく出来ます。

bのとり得る範囲はL = log(2)/2として[-L, L]でした。
関数f(x) = 1 + A + Bx + Cx^2 + Dx^3 + Ex^4 + Fx^5として
区間[-L, L]でf(x)とexp(x)の差の2乗誤差の平均を最小化する(A, B, C, D, E, F)を見つけます。

数学的にはI(A, B, C, D, E, F):=∫_\[-L,L\](exp(x) - f(x))^2 dxとして
IをA, B, C, D, E, Fで偏微分した値が全て0になる解を求めます。

Mapleでは

```maple
f := x->A+B*x+C*x^2+D*x^3+E*x^4+F*x^5;
g:=int((f(x)-exp(x))^2,x=-L..L);
sols:=solve({diff(g,A)=0,diff(g,B)=0,diff(g,C)=0,diff(g,D)=0,diff(g,E)=0,diff(g,F)=0},{A,B,C,D,E,F});
Digits:=1000;
s:=eval(sols,L=log(2)/2);
evalf(s,20);
```

で求めました。
雑な比較ですが単純に打ち切ったときに比べて誤差が半分程度になりました。

[Sollya](http://sollya.gforge.inria.fr/)を使って

```
remez(exp(x),5,[-log(2)/2,log(2)/2]);
```
で求めるやり方もあります(個人的には2乗誤差を小さくする前者の方がよい印象 : 数値計算専門の方教えてください)。
