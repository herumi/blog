# Xbyakのノウハウ
ここではインラインアセンブラや外部アセンブラと異なるXbyak特有の気をつけるべきことを紹介します。

## アクセス保護の制御
`Xbyak::CodeGenerator`はデフォルトではメモリを確保して、その領域にread/write/exec属性を付与します。
しかし、最近はそのようなメモリ領域は攻撃対象となりやすく嫌われる傾向にあります。
OSの設定によっては禁止されていることもあります。

そのため少しでも攻撃リスクを減らすために、実行属性(exec)と書き込み属性(write)を同時につけないようにするとよいです。

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

## グローバル変数へのアクセス

コード生成している関数の中からグローバル変数や定数を参照したいことはよくあります。

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
このようにすると先頭だけ設定したら、あとはオフセットでアクセスできます。

## ローカルな静的変数
その関数でのみ利用するちょっとした定数にアクセスしたいときはコードの後ろに配置する方法があります。

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

`align(32)`はなくても動作するのですが、CPUが`ret`の後ろのデータもデコードしようとしないよう開けています。

本当は4096byteずらしてコードとデータを明確に分けるとよいです。
cf. Intel最適化マニュアル 3.6.8 Mixing Code and Data Coding Rule 51.

[データとコードの配置](#%E3%83%87%E3%83%BC%E3%82%BF%E3%81%A8%E3%82%B3%E3%83%BC%E3%83%89%E3%81%AE%E9%85%8D%E7%BD%AE)も参照してください。

浮動小数点数の値を書き込む場合はビットパターンを整数に変換する関数を用意しておくとよいでしょう。

```
uint32_t ftoi(float f)
{
    uint32_t v;
    memcpy(&v, &f, sizeof(v));
    return v;
}

dd(ftoi(1.23f));
```

## ripによる相対アドレス
データへのオフセットが±2GiB以内にあることが分かっている場合にはripによる相対アクセスができます。

```
struct Code : Xbyak::CodeGenerator {
    Code()
    {
        Xbyak::Label dataL;
        mov(eax, ptr[rip + dataL]);
        ret();
    L(dataL);
        dd(123);
    }
};
```
こちらのほうが1命令減らせますね。

## テーブルルックアップによる分岐
`putL`を使うとラベルをメモリに配置できます。

```
    Code()
    {
        Xbyak::util::StackFrame sf(this, 1);
        Xbyak::Label lpL, case0L, case1L, case2L, case3L;
        mov(rax, lpL);
        jmp(ptr[rax + sf.p[0] * 8]);
        align(32);
    L(lpL);
        putL(case0L);
        putL(case1L);
        putL(case2L);
        putL(case3L);
    L(case0L);
        mov(eax, 1);
        ret();
    L(case1L);
        mov(eax, 10);
        ret();
    L(case2L);
        mov(eax, 100);
        ret();
    L(case3L);
        mov(eax, 1000);
        ret();
    }
```

`Xbyak::util::StackFrame sf(this, 1);`はWindowsとLinuxの呼び出し規約の差を吸収するためです。
`sf.p[0]`が生成された関数の第一引数のレジスタを表します。Linuxなら直接rdi(Windowsならrcx)と書いてもよいです。

```
    mov(rax, lpL);
    jmp(ptr[rax + sf.p[0] * 8]);
```
ラベル`lpL`から8byteずつ並んでいるポインタの配列の`sf.p[0]`番目を取り出します。
まともに使うときは値が配列のサイズに入っているかちゃんと確認する必要があります。

```
L(lpL);
    putL(case0L);
    putL(case1L);
    putL(case2L);
    putL(case3L);
```
C++では
```
uint64_t *lpL[] = {
    case0L, case1L, case2L, case3L
};
```
に相当します。

## データとコードの配置
比較的大きめのプログラムになるとデータとコードをきちんと管理したくなります。
その場合はユーザがメモリレイアウトを決めてそれをXbyakに伝えることにします。

たとえば連続するdata領域4KiB、コード領域4KiBを用意してそれをXbyakで利用してみましょう([separate-code-data.cpp](sample/separate-code-data.cpp))。

```
+------+
| data |
|      |
+------+
| code |
|      |
+------+
```
まず

```
struct DataCode {
    uint8_t data[4096];
    uint8_t code[4096];
};
alignas(4096) DataCode g_dataCode;
```
でdataとcodeの領域を用意します。
`Xbyak::CodeGenerator`にユーザが指定した領域を渡します。

```
struct Code : Xbyak::CodeGenerator {
    Xbyak::Label dataL;
    Code()
        : Xbyak::CodeGenerator(sizeof(g_dataCode), &g_dataCode)
    {
        L(dataL);
        dd(123);
        setSize(sizeof(g_dataCode.data));
    }
```
先頭4096byteがdata領域なのでその先頭アドレスを`L(dataL)`でアクセスできるようにします。
`dd(123)`やその他の方法ですきなように`DataCode::data`をいじります。

データの構築が終わったら
`setSize(sizeof(g_dataCode.data));`でXbyakが書き込む場所をdataの最後（=codeの先頭）に設定します。

```
    Code c;
    auto f = c.getCurr<int (*)()>();
```
このとき、関数ポインタfのアドレスは`g_dataCode.code`の先頭アドレスに等しくなります。

```
    mov(rax, dataL);
    mov(eax, ptr[rax]);
    ret();
```
前節と同じく`dataL`を介して値を読み込みます。
最後にコード領域のみread/exec属性に変更します。

```
    protect(g_dataCode.code, sizeof(g_dataCode.code), Xbyak::CodeArray::PROTECT_RE);
```
