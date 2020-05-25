#include <stdio.h>
#include <xbyak/xbyak_util.h>

struct Code : Xbyak::CodeGenerator {
    Code()
    {
        Xbyak::util::StackFrame sf(this, 3);
        lea(rax, ptr[sf.p[0] + sf.p[1]]);
    }
};

int main()
{
    Code c;
    auto f = c.getCode<int (*)(int, int)>();
    int x = 3;
    int y = 10;
    printf("f(%d, %d)=%d\n", x, y, f(x, y));
}
