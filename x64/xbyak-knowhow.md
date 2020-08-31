# Xbyakのノウハウ
ここではインラインアセンブラや外部アセンブラと異なるXbyak特有の気をつけるべきことを紹介します。

## アクセス保護の制御
`Xbyak::CodeGenerator`はデフォルトではメモリを確保して、その領域にread/write/exec属性を付与します。
しかし、最近はそのようなメモリ領域は攻撃対象となりやすく嫌われる傾向にあります。
OSの設定によっては禁止されていることもあります。

そのため少しでも攻撃リスクを減らすために、実行属性(exec)と書き込み属性(write)を同時につけないようにします。

そのためには`CodeGenerator`のコンストラクタに`DontSetProtectRWE`を指定します。

```
struct Code : Xbyak::CodeGenerator {
    Code()
        : Xbyak::CodeGenerator(4096, Xbyak::DontSetProtectRWE)
    {
        mov(eax, 123);
        ret();
    }
};
```

この場合Xbyakはmalloc(またはmmap)した領域にバイトコードを書き込むだけでメモリの属性は変更しません。
コード生成が完了したら、`getCode`する前に`setProtectModeRE`を呼び出して実行権限を与えます。

```
Code c;
c.setProtectModeRE();
auto f = c.getCode<int (*)()>();
printf("f=%d\n", f());
```

`c.setProtectModeRE();`を呼ばないと(当たり前ですが)Segmentation faultを起こすので注意してください。
再度コードを書き換える場合は`c.setProtectModeRW();`でメモリを読み書きできるようにします。

## 外部変数とのやりとり

コード生成している関数の中から変数や定数を参照したいことはよくあります。
いろいろな方法があるのでいくつか紹介します。

### グローバル変数へのアクセス

x64のメモリアドレスのオフセットは±2GiBです。
32bit時代はメモリ空間が32bitなので問題なかったのですが、メモリ空間が64bitになるといま自分がいる領域とアクセスしたい変数のアドレスの差が32bitに収まるとは限りません。
そのためいったんポインタを経由してアクセスします。

```
static int g_i = 123;
static float g_f = 4.56;

struct Code : Xbyak::CodeGenerator {
    Code()
    {
        mov(rax, (size_t)&g_i);
        mov(eax, ptr[rax]);
        mov(rax, (size_t)&g_f);
        movss(xmm0, ptr[rax]);
    }
};
```

Xbyakがアクセスしたい変数はそれほど多くないでしょう（無節操にアクセスするとそれはそれで困りものです）。
まとめて構造体に入れておくとアクセスしやすいです。

```
struct Param {
	int i;
	float f;
} g_p;

struct Code : Xbyak::CodeGenerator {
    Code()
    {
        mov(rax, (size_t)&g_p);
        mov(eax, ptr[rax]);
        movss(xmm0, ptr[rax + offsetof(Param, f)]);
    }
};
```
offsetofは指定したメンバ変数のオフセットを得るCのマクロです。

## ローカルな静的変数
その関数でのみ利用するちょっとした定数にアクセスしたいときはコードの前や後ろに配置する方法があります。

```
struct Code : Xbyak::CodeGenerator {
    Code()
    {
        Xbyak::Label dataL;
        mov(rax, dataL);
        mov(eax, ptr[rax]);
        ret();
        align(32);
    L(dataL);
        dd(123);

    }
};
```
`mov(rax, dataL);`はraxにdataLが設定されたアドレスを設定する命令です。
次の`mov(eax, ptr[eax]);`でそのアドレスから4byteの整数を読み込んでいます。

`L(dataL);`でdataLを設定し、そこに`dd`で`uint32_t(123)`を4byteとして書き込んでいます。
結果的に`eax`に123が設定されて返る関数です。

`align(32)`はなくても動作するのですが、CPUが`ret`の後ろのデータもデコードしようとして余計な負荷がかかるかもしれません。
念のために開けています。

本当は4096byteずらしてコードとデータを明確に分けるとよいです([separate-code-data.cpp](sample/separate-code-data.cpp))。
cf. Intel最適化マニュアル 3.6.8 Mixing Code and Data Coding Rule 51.
