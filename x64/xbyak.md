# Xbyakのチュートリアル
[Xbyak](https://github.com/herumi/xbyak)はC++を使ってx64向けCPUのアセンブリ言語での最適化をやりやすくするために開発しています。

## Xbyakの紹介
次の特長があります。
- Windows, Linux, macOSサポート。標準的なC++コンパイラで動作します。
- ヘッダファイルのみなのでincludeするだけで使えます。
- AVX-512フルサポート。今後登場予定のCooper Lakeで対応予定の命令もサポートしています。

Intelの[oneDNN](https://github.com/oneapi-src/oneDNN)やBaiduの[PaddlePaddle](https://github.com/PaddlePaddle/Paddle/)といった深層学習ライブラリで採用されています。
スーパーコンピュータ富嶽が採用するArmv8.2-A+SVE用にも[xbyak_aarch64](https://github.com/fujitsu/xbyak_aarch64)として移植されています。

Xbyakはもともと暗号ライブラリを開発するために開発しました。
自作ペアリングライブラリ[mcl](https://github.com/herumi/mcl)を使ったBLS署名ライブラリ[bls](https://github.com/herumi/bls)は次世代Ethereumのクライアント([Prysmaticlabs](https://prysmaticlabs.com/), [Lighthouse](https://lighthouse.sigmaprime.io/), [Nethermind](https://nethermind.io/)など)やブロックチェーン系プロジェクト([DFINITY](https://dfinity.org/), [Harmony](https://www.harmony.one/), [0Chain](https://0chain.net/), [ChainSafe](https://chainsafe.io/)など)で利用されています。

## 準備
WindowsやLinux上で動作するC++コンパイラを準備して[Xbyak](https://github.com/herumi/xbyak)をcloneしてください。

```
git clone https://github.com/herumi/xbyak
```

Xbyakはヘッダファイルのみで構成されているのでコンパイラオプションに`-I<xbyak directory>`を指定するだけで使えます。
最低限必要なファイルは

```
xbyak/
  xbyak.h
  xbyak_mnemonic.h
  xbyak_util.h
```
です。

## 最小サンプル

[ret.cpp](sample/ret.cpp)
```
#include <stdio.h>
#include <xbyak/xbyak.h>

struct Code : Xbyak::CodeGenerator {
    Code(int x)
    {
        mov(eax, x);
        ret();
    }
};

int main()
{
    Code c1(123);
    Code c2(999);
    auto f1 = c1.getCode<int (*)()>();
    auto f2 = c2.getCode<int (*)()>();
    printf("f1()=%d, f2()=%d\n", f1(), f2()); // f1()=123, f2()=999
}
```
コンパイルして実行すると

```
f1()=123, f2()=999
```
と表示されます。
これは実行時に
```
mov eax, 123
ret
```
という関数と
```
mov eax, 999
ret
```
という関数が生成されて、それらの関数を呼び出しています。

## WindowsとLinuxの呼び出し規約を扱う
32-bit x86環境はOSに関係なく同じ呼び出し規約でしたが、x64環境は異なります。
たとえば

```
void f(int64_t a, int64_t b, int64_t c, int64_t d);
```
という関数が呼び出されたとき、Windowsでは
```
a = rcx
b = rdx
c = r8
d = r9
```
となり、Linuxでは
```
a = rdi
b = rsi
c = rdx
d = rcx
```
となります。また退避すべきレジスタも異なります。
詳細は[x64 アセンブリ言語プログラミング](http://herumi.in.coocan.jp/prog/x64.html)などを参照ください。

これは面倒なのでxbyakではこの差異を吸収する最小限のクラスを用意しています。
`xbyak/xbyak_util.h`をincludeして`Xbyak::util::StackFrame`クラスを利用します。

[add.cpp](sample/add.cpp)
```
#include <stdio.h>
#include <xbyak/xbyak_util.h>

struct Code : Xbyak::CodeGenerator {
    Code()
    {
        Xbyak::util::StackFrame sf(this, 3);
        // linuxではlea rax, [rdi+rsi]
        // Windowsではeal rax, [rcx+rdx]になる
        lea(rax, ptr[sf.p[0] + sf.p[1]]);
    }
};

int main()
{
    Code c;
    auto f = c.getCode<int (*)(int, int)>();
    int x = 3;
    int y = 10;
    printf("f(%d, %d)=%d\n", x, y, f(x, y)); // f(3, 10)=13
}
```

`Xbyak::util::StackFrame sf(this, 引数の数, [関数内で使いたいレジスタ数], [スタックサイズ]);`と使います。

- 引数の数は1から4までの整数を指定します。
  - 引数は`sf.p[i]`で参照します。
- 関数内で使いたいレジスタ数は引数と合わせて最大14です。
  - レジスタは`sf.t[i]`で参照します。
- スタックサイズは関数内で利用するサイズを指定します。
  - [`rsp`, `rsp` + 指定サイズ)の範囲を利用できます。

退避、復元すべきレジスタは自動的に行われるので勝手にret()で抜けるとおかしくなるので注意してください。
汎用レジスタ以外のレジスタ、たとえばxmmレジスタなどの退避復元は行わないので必要に応じて自分でする必要があります。

## ニーモニック
基本的にIntel形式で記述します。
```
NASM                   Xbyak
mov eax, [ebx+ecx] --> mov(eax, ptr [ebx+ecx]);
mov al, [ebx+ecx]  --> mov(al, ptr [ebx + ecx]);
test byte [esp], 4 --> test(byte [esp], 4);
inc qword [rax]    --> inc(qword [rax]);

vaddpd zmm2{k5}, zmm4, zmm2             --> vaddpd(zmm2 | k5, zmm4, zmm2);
vaddpd zmm2{k5}{z}, zmm4, zmm2          --> vaddpd(zmm2 | k5 | T_z, zmm4, zmm2);
vaddpd zmm2{k5}{z}, zmm4, zmm2,{rd-sae} --> vaddpd(zmm2 | k5 | T_z, zmm4, zmm2 | T_rd_sae);
```

詳しい例は[Addressing](https://github.com/herumi/xbyak/#addressing)を参照してください。
