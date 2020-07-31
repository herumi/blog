# アンロール

## 述語レジスタへの依存除去

再掲
```
Label lp = L();
    ld1w(z0.s, p0/T_z, ptr(src1, x4, LSL, 2));
    ld1w(z1.s, p0/T_z, ptr(src2, x4, LSL, 2));
    add(z0.s, z0.s, z1.s);
    st1w(z0.s, p0, ptr(out, x4, LSL, 2));
    incd(x4);
L(cond);
    whilelt(p0.s, x4, n);
    b_first(lp);
```

このコードはループごとにp0レジスタ(述語レジスタ)を更新している。
ループ最後の端数処理以外はp0レジスタは固定でよい。

上記ループの前に次のコードを追加する。

```
    ptrue(p0.s);
    Label skip;
    b(skip);
Label lp = L();
    ld1w(z0.s, p0/T_z, ptr(src1));
    add(src1, src1, 64);
    ld1w(z1.s, p0/T_z, ptr(src2));
    add(src2, src2, 64);
    fmla(z1.s, p0/T_m, z0.s, z0.s);
    st1w(z1.s, p0, ptr(out));
    add(out, out, 64);
    sub(n, n, 16);
L(skip);
    cmp(n, 16);
    bge(lp);
}
```

- `pture(p0.s)`は述語レジスタを全て1にする(全部処理する)。
- 16要素ずつ処理するのでポインタを64byteずつ増やす。

結果

- こうすると276nsec→134nsecと約2倍の高速化。
- ただし16要素ずつ処理するのでSVEレジスタが512bit固定となる。
  - 即値ではなくレジスタ幅に応じて変更してもよい

## ループアンロール
安直にループを延ばしてみる

Nをループアンロール回数とする(N = 2, 3, 4)。
レジスタ(z0, z1), (z2, z3), (z4, z5), (z6, z7)を使う。
```
Label lp = L();
    for (int i = 0; i < N; i++) ld1w(ZReg(i * 2).s, p0/T_z, ptr(src1, i));
    add(src1, src1, 64 * N);
    for (int i = 0; i < N; i++) ld1w(ZReg(i * 2 + 1).s, p0/T_z, ptr(src2, i));
    add(src2, src2, 64 * N);
    for (int i = 0; i < N; i++) fmla(ZReg(i * 2 + 1).s, p0/T_m, ZReg(i * 2).s, ZReg(i * 2).s);
    for (int i = 0; i < N; i++) st1w(ZReg(i * 2 + 1).s, p0, ptr(out, i));
    add(out, out, 64 * N);
    sub(n, n, 16 * N);
L(skip);
    cmp(n, 16 * N);
    bge(lp);
```

## ベンチマーク

コード|C|述語レジスタに依存|依存しない(N=1)|N=2|N=3|N=4
-|-|-|-|-|-|-
nsec|360|289|135|107|90|79

N=4のとき1ループあたり79/16*2=2.5clk
