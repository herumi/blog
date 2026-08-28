# articles/add-imm-fusion2.md の測定結果を描画して images/*.png に保存する
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

# add(rax, 1) x lpN の表
# [lpN, instructions, uops_issued.any, uops_executed.thread, cyc]
addOnly = [
  [ 0,  2,  1.00, 1.00, 1.03],
  [ 1,  3,  2.00, 2.00, 0.98],
  [ 2,  4,  3.00, 2.00, 1.00],
  [ 3,  5,  4.00, 2.01, 1.02],
  [ 4,  6,  5.02, 2.02, 1.00],
  [ 5,  7,  6.00, 2.03, 1.01],
  [ 6,  8,  7.00, 2.87, 1.17],
  [ 7,  9,  8.01, 3.04, 1.34],
  [ 8, 10,  9.01, 3.03, 1.99],
  [ 9, 11, 10.01, 3.05, 2.00],
  [10, 12, 11.01, 3.07, 2.00],
  [11, 13, 12.01, 3.08, 2.01],
  [12, 14, 13.01, 3.92, 2.21],
  [13, 15, 14.02, 4.10, 2.46],
  [14, 16, 15.02, 4.16, 2.62],
  [15, 17, 16.02, 4.41, 2.76],
  [16, 18, 17.01, 4.11, 2.98],
  [17, 19, 18.01, 4.11, 3.04],
  [18, 20, 19.01, 4.95, 3.15],
  [19, 21, 20.00, 5.21, 3.40],
  [20, 22, 21.01, 5.17, 3.50],
  [21, 23, 22.01, 5.46, 3.75],
  [22, 24, 23.01, 5.50, 3.92],
  [23, 25, 24.01, 5.68, 4.01],
  [24, 26, 25.01, 6.00, 4.15],
]

# imul(rax, rax) + add(rax, 1) x lpN の表
# [lpN, instructions, uops_issued.any, uops_executed.thread, cyc]
mulAdd = [
  [ 0,  3,  2.00, 2.00, 2.99],
  [ 1,  4,  3.00, 3.00, 3.01],
  [ 2,  5,  4.00, 3.33, 3.03],
  [ 3,  6,  5.00, 3.33, 3.02],
  [ 4,  7,  6.00, 3.00, 3.00],
  [ 5,  8,  7.01, 3.67, 2.98],
  [ 6,  9,  8.01, 4.00, 2.98],
  [ 7, 10,  9.00, 4.00, 2.99],
  [ 8, 11, 10.01, 4.32, 2.99],
  [ 9, 12, 11.00, 4.33, 2.96],
  [10, 13, 12.00, 4.81, 3.00],
  [11, 14, 13.00, 4.67, 3.07],
  [12, 15, 14.01, 5.00, 3.07],
  [13, 16, 15.00, 5.00, 3.04],
  [14, 17, 16.00, 5.30, 3.12],
  [15, 18, 17.01, 5.07, 3.06],
  [16, 19, 18.01, 5.00, 3.06],
  [17, 20, 19.00, 5.67, 3.17],
  [18, 21, 20.01, 6.01, 3.39],
  [19, 22, 21.00, 6.01, 3.55],
  [20, 23, 22.03, 6.29, 3.74],
  [21, 24, 23.00, 6.26, 3.87],
  [22, 25, 24.00, 6.63, 4.07],
  [23, 26, 25.00, 6.67, 4.11],
  [24, 27, 26.00, 6.91, 4.38],
]

def transpose(tbl):
  return list(zip(*tbl))

lpN, _, issued1, executed1, cyc1 = transpose(addOnly)
_,   _, issued2, executed2, cyc2 = transpose(mulAdd)

model = [(n + 2) / 6 for n in lpN]  # Allocate 6uops/cyc のモデル値

# fusion-add.png
fig2, ax2 = plt.subplots(figsize=(7, 5))
# add only は青系、imul + add は赤系で揃える
blueLight, blue, blueDark = '#9ecae1', '#4292c6', '#08306b'
warmLight, warm, warmDark = '#fdae6b', '#e6550d', '#cb181d'

lines = []  # 凡例は add only の3本を先に、次に imul + add の3本を並べる
lines += ax2.plot(lpN, issued1, 'o--', color=blueLight, label='add only: uops_issued')
lines += ax2.plot(lpN, executed1, 's-', color=blueDark, label='add only: uops_executed')
ax2.set_xlabel('lpN')
ax2.set_ylabel('uops / iteration')
ax2.set_title('uops issued vs executed (add only)')
ax2.set_xticks(range(0, 25, 2))
ax2.grid(alpha=0.3)

ax2r = ax2.twinx()  # 右側の縦軸に cyc
lines += ax2r.plot(lpN, cyc1, '^-', color=blue, label='add only: cyc')
ax2r.set_ylabel('cyc / iteration')
ax2r.set_ylim(0, 5)

def putLegend():
  ax2r.legend(lines, [x.get_label() for x in lines], loc='upper left')

putLegend()
fig2.tight_layout()
fig2.savefig('images/fusion-add.png', dpi=120)
print('save images/fusion-add.png')

# fusion-add-mul.png
ax2r.set_ylim(0, 5)
lines += ax2.plot(lpN, issued2, 'o--', color=warmLight, label='imul + add: uops_issued')
lines += ax2.plot(lpN, executed2, 's-', color=warmDark, label='imul + add: uops_executed')
lines += ax2r.plot(lpN, cyc2, 'v-', color=warm, label='imul + add: cyc')
ax2.set_title('uops issued vs executed')
putLegend()
fig2.tight_layout()
fig2.savefig('images/fusion-add-mul.png', dpi=120)
print('save images/fusion-add-mul.png')

# fusion-cycle.png
fig1, ax1 = plt.subplots(figsize=(7, 5))
ax1.plot(lpN, cyc1, '^-', color=blue, label='add only: cyc')
# imul + add の lpN と add only の lpN+1 は同じ uops 数なので、
# 比較用に add only を1つ左にずらした系列も描く
ax1.plot(lpN[:-1], cyc1[1:], '^--', color=blueLight, label='add only at lpN+1: cyc')
ax1.plot(lpN, cyc2, 'v-', color=warm, label='imul + add: cyc')
ax1.plot(lpN, model, 'k--', label='(lpN+2)/6 (alloc 6uops/cyc)')
ax1.axhline(3, color='gray', ls=':', lw=1, label='imul latency 3')
ax1.set_xlabel('lpN')
ax1.set_ylabel('cyc / iteration')
ax1.set_title('cycles per iteration')
ax1.set_xticks(range(0, 25, 2))
ax1.grid(alpha=0.3)
ax1.legend()
fig1.tight_layout()
fig1.savefig('images/fusion-cycle.png', dpi=120)
print('save images/fusion-cycle.png')
