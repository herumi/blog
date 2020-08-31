#include <xbyak/xbyak.h>

struct DataCode {
    uint8_t data[4096];
    uint8_t code[4096];
};
alignas(4096) DataCode g_dataCode;

struct Code : Xbyak::CodeGenerator {
    Xbyak::Label dataL;
    Code()
        : Xbyak::CodeGenerator(sizeof(g_dataCode), &g_dataCode)
    {
        L(dataL);
        dd(123);
        setSize(sizeof(g_dataCode.data));
    }
    void gen()
    {
        mov(rax, dataL);
        mov(eax, ptr[rax]);
        ret();
        protect(g_dataCode.code, sizeof(g_dataCode.code), Xbyak::CodeArray::PROTECT_RE);
    }
    ~Code()
    {
        protect(g_dataCode.code, sizeof(g_dataCode.code), Xbyak::CodeArray::PROTECT_RW);
    }
};

int main()
    try
{
    printf("data=%p\n", g_dataCode.data);
    Code c;
    auto f = c.getCurr<int (*)()>();
    printf("code=%p\n", g_dataCode.code);
    c.gen();
    printf("%d\n", f());
} catch (std::exception& e) {
    printf("err %s\n", e.what());
    return 1;
}
