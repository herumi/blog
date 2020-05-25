#include <stdio.h>
#include <xbyak/xbyak.h>

struct Code : Xbyak::CodeGenerator {
    Code(int x)
    {
        mov(eax, x);
        ret();
    }
};

int main()
{
    Code c1(123);
    Code c2(999);
    auto f1 = c1.getCode<int (*)()>();
    auto f2 = c2.getCode<int (*)()>();
    printf("f1()=%d, f2()=%d\n", f1(), f2());
}
