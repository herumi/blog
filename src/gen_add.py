from gen_x86asm import *
import argparse

def gen_add(N):
  align(16)
  with FuncProc(f'mclb_add{N}'):
    with StackFrame(3) as sf:
      z = sf.p[0]
      x = sf.p[1]
      y = sf.p[2]
      for i in range(N):
        mov(rax, ptr(x + 8 * i))
        if i == 0:
          add(rax, ptr(y + 8 * i))
        else:
          adc(rax, ptr(y + 8 * i))
        mov(ptr(z + 8 * i), rax)
      setc(al)
      movzx(eax, al)

# t[] = [px] + [py]
def add_rmm(t, px, py):
  for i in range(len(t)):
    mov(t[i], ptr(px + 8 * i))
    if i == 0:
      add(t[i], ptr(py + 8 * i))
    else:
      adc(t[i], ptr(py + 8 * i))

# t1[] = t2[]
def copy_rr(t1, t2):
  for i in range(len(t1)):
    mov(t1[i], t2[i])

# t[] -= [px]
def sub_rm(t, px):
  for i in range(len(t)):
    if i == 0:
      sub(t[i], ptr(px + 8 * i))
    else:
      sbb(t[i], ptr(px + 8 * i))

# [px] = t[]
def store(px, t):
  for i in range(len(t)):
    mov(ptr(px + 8 * i), t[i])

def cmovc_rr(t1, t2):
  for i in range(len(t1)):
    cmovc(t1[i], t2[i])

def gen_fp_add(N):
  align(16)
  with FuncProc(f'mclb_fp_add{N}'):
    with StackFrame(4, N*2-2) as sf:
      z = sf.p[0]
      x = sf.p[1]
      y = sf.p[2]
      p = sf.p[3]
      t = sf.t[0:N]
      t2 = sf.t[N:]
      xor_(eax, eax)
      # t = x + y
      add_rmm(t, x, y)
      # use rax for CF
      setc(al)
      # x, y are free
      t2.append(x)
      t2.append(y)
      copy_rr(t2, t)
      # t[N] = x + y - p
      sub_rm(t, p)
      sbb(eax, 0)
      # t = t2 if t < 0
      cmovc_rr(t, t2)
      store(z, t)

parser = argparse.ArgumentParser()
parser.add_argument('-win', '--win', help='output win64 abi', action='store_true')
parser.add_argument('-m', '--mode', help='output asm syntax', default='nasm')
param = parser.parse_args()

setWin64ABI(param.win)
init(param.mode)

segment('text')
#gen_add(3)
gen_fp_add(4)
term()
