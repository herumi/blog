#include <xbyak/xbyak.h>
#include <xbyak/xbyak_util.h>

struct Code : Xbyak::CodeGenerator {
    Code()
    {
        Xbyak::util::StackFrame sf(this, 1);
        Xbyak::Label lpL, case0L, case1L, case2L, case3L;
        mov(rax, lpL);
        jmp(ptr[rax + sf.p[0] * 8]);
        align(32);
    L(lpL);
        putL(case0L);
        putL(case1L);
        putL(case2L);
        putL(case3L);
    L(case0L);
        mov(eax, 1);
        ret();
    L(case1L);
        mov(eax, 10);
        ret();
    L(case2L);
        mov(eax, 100);
        ret();
    L(case3L);
        mov(eax, 1000);
        ret();
    }
};

int main()
{
    Code c;
    auto f = c.getCode<int (*)(int)>();
    for (int i = 0; i < 4; i++) {
        printf("%d %d\n", i, f(i));
    }
}
