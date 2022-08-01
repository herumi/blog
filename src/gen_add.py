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

parser = argparse.ArgumentParser()
parser.add_argument('-win', '--win', help='output win64 abi', action='store_true')
parser.add_argument('-m', '--mode', help='output asm syntax', default='nasm')
param = parser.parse_args()

setWin64ABI(param.win)
init(param.mode)

segment('text')
gen_add(3)
termOutput()
