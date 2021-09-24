# A64FXのアーキテクチャ

## マニュアル

- [A64FX Microarchitecture Manual](https://github.com/fujitsu/A64FX)
- [A64 -- SVE Instructions](https://developer.arm.com/documentation/ddi0596/2020-12/SVE-Instructions)

## ステージ

A64FXは命令をスーパースカラーで実行する。
- 同時に複数の命令を実行できる。
- 依存関係のない命令の順序を入れ換える(アウトオブオーダー)。
- 処理を複数の要素に分割して、並行に処理させる(パイプライン)。

大きく5個のステージからなる
- 命令フェッチステージ
  - メモリ、命令キャッシュや分岐予測などを用いて命令を読み込みIBUFF(Instruction BUFFer)に保存する
- デコードステージ
  - IBUFFから最大4命令取得してμOP命令に分解する
  - movprfxは後続する命令とpack処理される
  - デコードされた命令をリザベーション・ステーションRS(Reservation Station)に渡す
- 実行ステージ
  - RSのμOPを実行する
    - 整数演算(EXA/EXB)
    - SIMD&FP, SVE(FLA/FLB)
    - 述語命令(PR)
    - ロード・ストア(EAGA/EAGB)
    - 分岐命令パイプライン
- ロード・ストアステージ
  - データキャッシュやロード・ストアパイプライン
- コミットステージ
  - 実行結果の確認をする
    - 例外や分岐予測結果の確認

○ SVEの演算のパイプラインの例

-|1,2|3～6|7,8|9～17|18～20|21|22|23,34
-|-|-|-|-|-|-|-|-
SVE|D|P|B|X|U|EXP|C|W

- D ; デコード
- P ; スケジューリング
- B ; 物理レジスタの読み込み
- X ; 命令実行
- U ; 結果の更新
- EXP ; 例外判定
- C ; コミット
- W ; 物理レジスタの更新

## レイテンシ
- 上記命令実行にかかる時間(クロック数)
- 複数の命令のタイミングによって同じステージにぶつかるとレイテンシを切り換えて(延ばして)対応する
- 命令によってレイテンシは異なる

主な命令のレイテンシ
- add, sub ; 4
- 論理演算 ; 3~4
- fadd, fmul, fmad, fscale ; 9
- whilelt ; 1+3
- incd ; 4
- ld1w ; 11
- frinta, frinti, frintm, frintnなど ; 9
- fmax, fmin ; 4
- fsqrt ; 98(float x 16のとき)
- fdiv ; 98(float x 16のとき)
- fexpa, frecpe, frsqrte ; 4
- frecps, frsqrts ; 9

## 命令

SVEの命令一覧は[A64 -- SVE Instructions (alphabetic order)](https://developer.arm.com/documentation/ddi0596/2020-12/SVE-Instructions)を参照。

命令によって引数のタイプが異なる

- `op(dst, src1, src2)` ; dst = op(src1, src2)
- `op(dst, pred, src)` ; dst = op(src) with pred
- `op(dst, pred, src1, src2)` ; dst = op(src1, src2) with pred

### 積和

```
fmad(a, b, c) ; a = a * b + c
fmsb(a, b, c) ; a =-a * b + c
fmla(a, b, c) ; a = a + b * c
fmls(a, b, c) ; a = a - b * c

fnmad(a, b, c); a =-a * b - c
fnmsb(a, b, c); a = a * b - c
fnmla(a, b, c); a =-a - b * c
fnmls(a, b, c); a =-a + b * c
```

```
movprfx(dst, pred, src3);
fmad(dst, pred, src1, src2); // dst = src3 * src1 + src2
```

### 除算

```
fdiv(dst, pred, src2); // dst /= src2;
```

```
movprfx(dst, pred, src1);
fdiv(dst, pred, src2); // dst = src1 / src2
```
