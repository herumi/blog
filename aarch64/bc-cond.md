# bc.cond(ヒント付き条件分岐 hinted conditional branch)

## 説明
- [BC.cond](https://developer.arm.com/documentation/ddi0596/2021-09/Base-Instructions/BC-cond--Branch-Consistent-conditionally-)
- with a hint that this branch will behave very consistently and is very unlikely to change direction.
  - この分岐は一貫した振る舞いをし、方向を変える可能性は非常に低い。
  - 多分likely()やunlikely()やマクロのヒントを使った分岐を実現する。
  - もしかしたらループ処理も?
- HaveFeatHBC()でないと未定義
  - ARMv8.8以降でなければ実装されていない
  - apt getで入るaarch64-linux-gnu-as (version 2.34)ではエラー
    - -march=armv8.8-a+hbcで有効になる?(未確認)

従来の条件分岐
- [B.cond](https://developer.arm.com/documentation/ddi0596/2021-09/Base-Instructions/B-cond--Branch-conditionally-)
