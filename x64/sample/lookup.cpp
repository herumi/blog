#include <xbyak/xbyak.h>

struct Code : Xbyak::CodeGenerator {
    Code()
    {
        Xbyak::Label lpL, case0L, case1L, case2L, case3L;
#ifdef XBYAK64_WIN
        const auto& x = rcx;
#elif defined(XBYAK64_GCC)
        const auto& x = rdi;
#endif
        mov(rax, lpL);
        jmp(ptr[rax + x * 8]);
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
