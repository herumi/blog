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

def vec_rr(op, x, y):
  for i in range(len(x)):
    op(x[i], y[i])

def vec_rm(op, x, addr):
  for i in range(len(x)):
    op(x[i], ptr(addr + 8 * i))

def vec_mr(op, addr, x):
  for i in range(len(x)):
    op(ptr(addr + 8 * i), x[i])

# x[] = y[]
def mov_rr(x, y):
  vec_rr(mov, x, y)

# t[] -= [px]
def sub_rm(t, px):
  for i in range(len(t)):
    if i == 0:
      sub(t[i], ptr(px + 8 * i))
    else:
      sbb(t[i], ptr(px + 8 * i))

# [addr] = x[]
def store(addr, x):
  vec_mr(mov, addr, x)

def cmovc_rr(x, y):
  vec_rr(cmovc, x, y)

def gen_fp_add(N):
  align(16)
  with FuncProc(f'mclb_fp_add{N}'):
    with StackFrame(4, N*2-2) as sf:
      pz = sf.p[0]
      px = sf.p[1]
      py = sf.p[2]
      pp = sf.p[3]
      X = sf.t[0:N]
      T = sf.t[N:]
      xor_(eax, eax)     # eax = 0
      add_rmm(X, px, py) # X = px[] + py[]
      setc(al)           # eax = CF
      T.append(px)
      T.append(py)
      mov_rr(T, X)       # T = X
      sub_rm(T, pp)      # T -= pp[]
      sbb(eax, 0)        # check CF
      cmovc_rr(T, X)     # T = X if T < 0
      store(pz, T)

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
