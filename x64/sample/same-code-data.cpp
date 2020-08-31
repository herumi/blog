#include <xbyak/xbyak.h>

struct Code : Xbyak::CodeGenerator {
    Code()
    {
        Xbyak::Label dataL;
        mov(eax, ptr[rip + dataL]);
        ret();
    L(dataL);
        dd(123);
    }
};

int main()
    try
{
    Code c;
    auto f = c.getCode<int (*)()>();
    printf("%d\n", f());
} catch (std::exception& e) {
    printf("err %s\n", e.what());
    return 1;
}
