# APXメモ
- [Intel Advanced Performance Extensions (Intel APX) Software Enabling Introduction](https://cdrdv2.intel.com/v1/dl/getContent/784265)
- [Intel Advanced Performance Extensions (Intel APX) Architecture Specification](https://cdrdv2.intel.com/v1/dl/getContent/784266)

## 特徴
- 64ビット専用
- 汎用レジスタにr16からr31が追加される
- NDD(data destination)が追加された3オペランド形式
- 条件ISAの改善
  - フラグの抑制オプション
- 最適化されたレジスタ退避・復元
- 64ビット絶対直接ジャンプ

## 拡張される命令

NDD対応命令
- inc, dec, not, neg, add, sub, adc, sbb, and, or, xor, sal, sar, shl, shr, rcl, rcr,
- rol, ror, shld, shrd, adcx, adox, cmovcc, imul
  - フラグを更新するかしないか設定できる(NF)

- kmov*, bmi
  - NF設定可能

- push2, pop2
- 2個のレジスタのpush, pop

- setcc
  - 上位ビットを0クリアする拡張

- jmpabs
  - 64ビットレジスタでしていされた絶対アドレスにジャンプ

## APX命令フォーマット
### 従来
3種類のエンコード空間
- レガシー
  - マップ0 : 1バイトオペコード(エスケープなし)
  - マップ1 : 2バイトオペコード(0x0F)
  - マップ2, 3 : 3バイトオペコード(0x0F38, 0x0F3A)
- VEX空間(0xC4, 0xC5)
- EVEX空間(0x62)

### APXが与える影響
- レガシー
  - マップ0, 1
    - 明示的なGPRかメモリオペランドを持つ全ての命令 - REX2により拡張レジスタr16-r31にアクセスできる
    - それ以外 : 将来のために予約
  - 選択された命令はEVEX空間に昇格
  - マップ2, 3
    - REX2プレフィクスを使えないので拡張レジスタに直接アクセスできない
    - 一部は昇格フォームを持つ
- VEX空間
  - VEX空間から選択された命令はEVEX空間に昇格
  - それ以外はAPXは使えない
- EVEX
  - 全ての命令が拡張レジスタを利用できる
  - レガシーから昇格した命令はマップ4(予約されていた)に配置される
  - VEXから昇格された命令はマップ番号はそのままで新しいEVEXフォームが追加される

