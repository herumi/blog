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

### REX2プレフィクス

REX2|7|6|5|4|3|2|1|0
-|-|-|-|-|-|-|-|-
0xD5|M0|R4|X4|B4|W|R3|X3|B3

- `[W:R3:X3:B3]`はREXプレフィクスの`[W:R:X:B]`と同じ
- レガシーマップ0のpush(0x50-0x57)/pop(0x58-0x5F)はREX2.WをPPX(push-popアクセラレーション)ヒントに使う
- `[R4:X4:B4]`は拡張レジスタR, X, Bの最上位ビットを指定する
- REXプレフィクスと同様OSIZE=8bならidx=4, 5, 6, 7はSPL, BPL, SIL, DILを示す
- REX2プレフィクスはレガシーマップ0, 1のREX2.M0つき命令として解釈される
  - レガシーマップ2, 3の命令はサポートしない
  - CRC32, SHA, RAOなどのレガシーマップ2, 3のAPX拡張はEVEXプレフィクスとして提供される
- REX2プレフィクスはプレフィクスの最後に置く
  - スタックプレフィクス, OSIZE, ASIZE, LOCK, REPNE, REPEだけが有効
対応
- op r1, r2 : B = r1, R = r2
- op r, [baes+idx*scale+disp] : B = base, R = r, X = scale

### 新しいデータ転送先NDD(New Data Destination)
- inc ndd, r/m : ndd = r/m + 1
- sub ndd, r/m, imm : ndd = r/m - imm
- sub ndd, r/m, reg : ndd = r/m - reg
- sub ndd, reg, r/m : ndd = reg - r/m

- NDDのレジスタはEVEXのVレジスタとしてエンコードされる(これはAVX-512のNDSとは異なる使い方)
- EVEX.NF=1で明示的に抑制されない限り、NDD形式はフラグレジスタの更新方法は従来と同じ
- OSIZE=8b or 16bのとき、NDDレジスタは従来と異なり上位ビットはゼロクリアされる

### 拡張EVEXプレフィクス
拡張EVEXはEVEXを元にいくつかのペイロードビットを定義し直したもの
- REX2では提供できないレガシー命令のAPX拡張(NDDなど)
- VEX, EVEX命令のAPX拡張

EVEX(0x62)とペイロード

R3|X3|B3|R4|B4|M2|M1|M0
-|-|-|-|-|-|-|-
W|V3|V2|V1|V0|X4|p|p
-|-|-|-|V4|-|-|-

- エスケープシーケンス0x0F, 0x0F38, 0x0F3Aは不要
- 命令のマップIDは`[M2:M1:M0]`でエンコードされる
- R3, X3, B3, R4, V3, V2, V1, V0, X4, V4はビット反転される
- B4とX4はBとXレジスタ識別子のMSB
- 拡張EVEXの前にはASIZE(0x67)とセグメントオーバーライドのみがつけられる
- 拡張EVEXはレガシー命令やVEX命令をEVEX空間に昇格させてAPXを提供する
  - 昇格により拡張レジスタにアクセスできるようになる
  - ステータスフラグの抑制指定
  - レガシー命令の場合NDDやZU(zero-upper)フォームをサポート
  - 昇格された命令のdisp8を持つ場合のスケーリング係数は常に1

### レガシー命令のEVEX拡張

R3|X3|B3|R4|B4|1|0|0
-|-|-|-|-|-|-|-
W|V3|V2|V1|V0|X4|p|p
-|-|-|ND|V4|NF|-|-

- REX2とEVEXのどちらでもエンコードできる場合REX2の方が短いのでREX2を優先する
- EVEXエンコードされたレガシー命令は4+1+1+1+4+4=15ばいとの命令長制限に達成することがある
  - 即値をレジスタにロードしてから短いレジスタソース版を使うのがよい
- NDDをサポートする命令が昇格する
  - inc, dec, not, neg, add, sub, adc, sbb, and, or, xor, sal, sar, shl, shr, rcl, rcr
  - マップ1のrol, ror, shld, shrd, adcx, adox, cmovcc, imul
  - EVEX.ND=0ならNDDは存在しない => EVEX.[V4:V3:V2:V1:V0]=0
  - EVEX.ND=1ならEVEX.[V4:V3:V2:V1:V0]にNDDが入る
- NF(ステータスフラグの更新抑制)をサポートする命令
  - inc, dec, neg, add, sub, and, or, xor, sal, sar, shl, shr, rol, ror, shld, shrd, imul, idiv, mul, div, lzcnt, tzcnt, popcnt
  - EVEX.NF=1で更新抑制

### VEX命令のEVEX拡張

R3|X3|B3|R4|B4|M2|M1|M0
-|-|-|-|-|-|-|-
W|V3|V2|V1|V0|X4|p|p
z|L|L|b|V4|a|a|a

- kmov*, bmiなどの命令が昇格する
- NF=1でフラグ抑制
  - andn, bextr, blsi, blsmsk, blsr, bzhi
- Vレジスタ識別子はNDSとしてエンコードされる

APXは次の命令を昇格しない
- GPRをベースやインデックスレジスタとして利用するメモリオペランドを持っていても、EVEXに対応するベクタレジスタを持たないVEX命令

例
- aesdec, aesdeclast, aesenc, aesenclastはVEX/EVEX両方
  - EVEX形式で32個のGPRとSIMDレジスタを利用できる
- aesimc, aeskeygenassisitはVEXのみ
  - 16個のGPRとSIMDレジスタしか利用できない

- EVEX.{R4,X4,B4}のどれがのビットが昇格されたVEX命令で利用されなかったらそれは無視される
- コード生成器はそれらの値を論理0にすべきである

### EVEX命令のEVEX拡張
- すべてのEVEX命令は拡張EVEXプレフィクスを使うと32個のGPRを利用できる
- B4とX4を除きEVEXと同じ

## デスティネーションレジスタのマージとゼロ拡張
このセクションの規則はデスティネーションレジスタがGPRのときにのみ適用される

- APX以前は64ビットモードでは結果は`[OSIZE-1:0]`に入り`[63:OSIZE]`はゼロになった
- APXではNDDが存在しないときは従来と同じ規則
- NDDが存在するときは`[63:OSIZE]`がゼロクリアされる

## push2/pop2
- push2 v64, b64 ; push v64/push b64
- pop2 v64, b64 ; pop v64/pop b64

## 条件命令の拡張
後で
