# AVX10メモ

## AVX10テクニカルペーパーメモ
- [Intel Advanced Vector Extensions 10 Technical Paper](https://cdrdv2.intel.com/v1/dl/getContent/784267)
- AVX-512の全ての機能を含む
- 最大256ビットSIMDレジスタを持つプロセッサにも512ビットSIMDレジスタを持つプロセッサにも対応
- CPUIDのチェックすべきフラグの数を減らす仕組み
- 将来のPコアとEコアの両方で動作

AVX10の収束バージョン(converged version)は
- (256ビット長に制約された)AVX-512の全ての機能を持つ
  - AVX10自体は512ビット長を利用可能
- 8個のマスクレジスタ
- 組み込み丸め

## 機能の列挙方法
- 既存のCPUは新しい命令ごとに新しいCPUIDフラグが追加されてチェックがとても大変
- 新しいバージョニングアプローチをとる
  - AVX10をサポートしていることを示すフラグ
    - AVX10をサポートしていればAVX/AVX2もサポートしている
  - AVX10のバージョン番号
    - 単調増加する
    - バージョンが大きければそれまでの機能が全て使えることを保証する
    - スカラー命令はいつでもサポートする
  - 128/256/512のビット長
    - 128/256は全てのプロセッサでサポート
    - 512はPコアでサポート
- AVX-512はAVX10が導入された時点でもう変更されない
- 新しい命令はAVX10として導入される

## パフォーマンス
- AVX2向けのアプリケーションはAVX10で再コンパイルすればパフォーマンスが向上するはず
- ベクターレジスタのプレッシャー(レジスタが沢山いる)AVX2アプリケーション
  - 16個レジスタを余計に使えるようになる
  - 新しい命令を利用できる
ようは新しい命令群で書き直せば速くなるということだろう

## 可用性
- AVX10.1
  - 128/256ビット FP/INT(512ビットはオプション)
  - 32個のSIMDレジスタ
  - 8個のマスクレジスタ
  - 512(?)ビット埋め込み丸め
  - 埋め込みブロードキャスト
  - Pコアのみ
- AVX10.2
  - 128/256ビット FP/INT(512ビットはオプション)
  - 32個のSIMDレジスタ
  - 8個のマスクレジスタ
  - 256/512(?)ビット埋め込み丸め
  - 埋め込みブロードキャスト
  - Pコア・Eコアサポート

# AVX10アーキテクチャ仕様
- [The Converged Vector ISA:Intel Advanced Vector Extensions 10](https://cdrdv2.intel.com/v1/dl/getContent/784343)
- AVX10は将来のPコアとEコアを含む全てのプロセッサでサポート
- Granite Rapidsで対応している全てのAVX-512をサポート
- AVX10の収束バージョン
  - 最大256ビットSIMDレジスタ
  - 32ビットのマスクレジスタ
  - YMMレジスタに対する組み込み丸め/SAE制御
- AVX10/512のPコアでは
  - 最大512ビットのSIMDレジスタ
  - 64ビットのマスクレジスタ
- AVX10/256仮想マシンを作成するVMX機能
- AVX10/128のサポート予定はない
- AVX10.1は128, 256ビットのAVX-512命令のみサポートする
- 512ビットはGranite Rapids上でpre-enableされる

機能|AVX-512|AVX10.1/256|AVX10.2/256|AVX10.1/512|AVX10.2/512
-|-|-|-|-|-
XMM|yes|yes|yes|yes|yes
YMM|yes|yes|yes|yes|yes
ZMM|yes|no|no|yes|yes
YMM埋め込み丸め|no|no|yes|no|yes
ZMM埋め込み丸め|yes|no|no|yes|yes

# [APX(Advanced Performance Extension)](https://www.intel.com/content/www/us/en/developer/articles/technical/advanced-performance-extensions-apx.html)
- 新しいREX2プレフィクス
- 汎用レジスタ(GPR)が16個→32個
  - loadが10%, storeが20%減少
- 3オペランド
  - 命令数10%減少
- xsave/xrestorに対応
  - MPXが廃止された領域を使うのでxsaveのレイアウトと大きさは変更されない
- push2/pop2命令
  - 2個のレジスタを転送
- フラグ変更抑制オプション
- 既存コードの再コンパイルで対応可能

## 少し詳しく
- modRM : 16bit自体からレジスタ3bitx2で8個x8個の2オペランド
  - `[mod:reg:r/m]=[2:3:3]`
- SIB : メモリ参照の場合のエンコード(即値含む `[eax+ecx*2+4]`など)
  - `[scale:index:base] = [2:3:3]`
- REXプレフィクス(AMDによる64bit拡張)
  - 1bygeのinc/decが無くなった
  - 1bit使って4bitx2で16個x16個の2オペランド
- VEXプレフィクス(Vector Extension)
  - 既存のプレフィクスをまとめて短くする
  - 66h : オペランドのサイズを変更
  - f2h, f3h, 0fh 38h, 0f 3ahなど
  - 3op対応(SIMD/AVXのみ)
  - les/ldsはreg, regがないのでそれを利用
- EVEXプレフィクス(Enhanced VEX)
  - bound(62h)命令の再利用
  - レジスタ32個の3オペランド
  - マスクレジスタ、丸めモード、ブロードキャストなどのフラグ
  - [AVX-512フォーマット詳解](https://www.slideshare.net/herumi/avx512)
- REX2プレフィクス ← New!
  - AAD(d5h)命令の再利用
  - [d5:M0:R4:X4:B4:W:R3:X3:B3]
  - レジスタが5bit : 32個
  - EVEXプレフィクスを整数命令にもつけられるようになった
    - 3オペランド対応
    - EVEXを更に拡張(Extended EVEX of EVEX instructions!)
- ccmp/ctest命令
  - 条件命令を実行するか否かのフラグSCC(souce condition code)をつけられる
  - 複数のcmpの結果のandならjmp
- setccの改善
  - setcc命令は下位8bitを0か1にする(上位bitは変更されない)
  - 最初にゼロクリアが必要/レジスタの部分書き換え
  - 上位を0クリアするフラグの追加
