#include <xbyak/xbyak.h>
#include <xbyak/xbyak_util.h>
#include <fstream>

struct Code : Xbyak::CodeGenerator {
	Code(int N)
	{
		Xbyak::util::StackFrame sf(this, 3);
		const auto& z = sf.p[0];
		const auto& x = sf.p[1];
		const auto& y = sf.p[2];
		for (int i = 0; i < N; i++) {
			mov(rax, ptr[x + 8 * i]);
			if (i == 0) {
				add(rax, ptr[y + 8 * i]);
			} else {
				adc(rax, ptr[y + 8 * i]);
			}
			mov(ptr[z + 8 * i], rax);
		}
		setc(al);
		movzx(eax, al);
	}
};

int main()
{
	Code code(4);
	auto add4 = code.getCode<uint64_t (*)(uint64_t *, const uint64_t *, const uint64_t *)>();
	std::ofstream ofs("code", std::ios::binary);
	ofs.write((const char*)add4, code.getSize());
}
