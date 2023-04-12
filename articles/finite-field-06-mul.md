---
title: "有限体の実装6（LLVM DSLによるMontgomery乗算の実装）"
emoji: "🧮"
type: "tech"
topics: ["有限体", "DSL", "llvm", "Montgomery乗算", "AArch64"]
published: true
---
## 初めに

今回は前回紹介したLLVM DSLを用いてMontgomery乗算を実装し、その性能を評価します。
記事全体の一覧は[有限体の実装一覧](https://zenn.dev/herumi/articles/finite-field-01-add#%E6%9C%89%E9%99%90%E4%BD%93%E3%81%AE%E5%AE%9F%E8%A3%85%E4%B8%80%E8%A6%A7)参照。

## PythonによるMontgomery乗算
まずMontgomery乗算に必要な計算を復習します。

### 記号

$p$ は $N$ 個の`uint64_t`で表現できる素数, $L=64$, $M=2^L$, $M' M - p' p = 1$ となる整数 $0 < M' < p$, $0 < p' < M$ を選んでおく。
$p'$ はコードではipとする。また $p$ のビット長は64の倍数でない（non fullbit）とします。

### mont関数

```python
# Pythonによる実装（再掲）
def mont(x, y):
  MASK = 2**L - 1
  t = 0
  for i in range(N):
    t += x * ((y >> (L * i)) & MASK) # (A)
    q = ((t & MASK) * self.ip) & MASK # (B)
    t += q * self.p # (C)
    t >>= L # (D)
  if t >= self.p: # (E)
    t -= self.p
  return t
```

(A)はyを64ビットの配列として表現したときのi番目の要素とxを掛けてtに足します。ここで前回作ったmulUnitを使います。
(B)はそのtの下位64ビットとipを掛けて下位64ビットを求めます。
(C)でもmulUnitを使います。
(D)は論理右シフトするだけ、(E)はselectを使います。

というわけでLLVM DSLで実装すると次のようになります。

```python
# unit = L = 64
def gen_mont(name, mont, mulUnit):
  pz = IntPtr(unit)
  px = IntPtr(unit)
  py = IntPtr(unit)
  with Function(name, Void, pz, px, py):
    t = Imm(0, unit*N+unit)
    for i in range(N):
      y = load(getelementptr(py, i))  # y = py[i]
      t = add(t, call(mulUnit, px, y))  # t += mulUnit(px, y)
      q = mul(trunc(t, unit), ip)          # q = uint64_t(t * ip)
      t = add(t, call(mulUnit, pp, q))  # t += mulUnit(pp, q)
      t = lshr(t, unit)                           # t >>= L
    t = trunc(t, unit*N)
    vc = sub(t, loadN(pp, N))
    c = trunc(lshr(vc, unit*N-1), 1)
    storeN(select(c, t, vc), pz)
  ret(Void)
```
Pythonによる実際に動くコードとほぼ一対一で記述できていることが分かります。

## ベンチマーク評価
前述のmontがどの程度の性能なのか評価しましょう。
M1 Mac上では[blst](https://github.com/supranational/blst)、x64上では[mcl](https://github.com/herumi/mcl)と比較します。
pはBLS署名などで広く使われているBLS12-381に現れる381ビット素数とします。

### MacBook Pro, M1, 2020, 単位nsec

M1|DSL|blst
-|-|-
mont|31.50|31.40

### Xeon Platinum 8280 turbo boost off, 単位nsec

x64|DSL|DSL + mulx|mcl
-|-|-|-
mont|42.86|35.52|28.93

DSLによるMontgomeryの実装は[mcl-ff](https://github.com/herumi/mcl-ff)にあります。

### 考察

blstのM1(AArch64)用のコードは手で書かれたアセンブラが使われています。DSL+LLVMで生成したコードはその速度にほぼ匹敵します。
x64ではmulxを使うオプションをつけてもmclの80%ぐらいの速度しかでませんでした。
これはmcl（やblst）がadox/adcxを使うのに対してLLVMでは-march=nativeをつけてもそれらの命令を使ったコードを生成しないからです。
このあたりの事情は[intrinsic関数とその限界](https://zenn.dev/herumi/articles/bitint-06-muladd#intrinsic%E9%96%A2%E6%95%B0%E3%81%A8%E3%81%9D%E3%81%AE%E9%99%90%E7%95%8C)で紹介したのと同じ理由です。
それでもx64 asmの知識が無くてもここまでの速度が出せるのはたいしたものです。DSLは単一のコードでM1/x64に対応できてますし。

M1で互角の性能を出した理由の一つがAArch64の汎用レジスタが29個とx64の約2倍あるためと思われます。
adox/adcxが無いとmulUnitの結果をaddするところで一時レジスタが多く必要になりますがAArch64では十分対応出来ます。
それに対してx64ではadox/adcxを使わないなら一時レジスタが足りないのでメモリに退避せざるを得ません。
このあたりが性能の差に現れたのでしょう。

## まとめ
Montgomery乗算関数をLLVM IRを生成するDSLで作りました。
性能はM1 Mac上なら人間が手で書いたものとほぼ互角、x64上での80%ぐらいの性能が出せることが分かりました。
開発のしやすさと性能のバランスを考えるとDSLによる開発は十分選択肢の一つとして考慮できると思われます。
