_text segment

public func
func proc
vpextrw rax, xmm0, 3
add eax, ecx
xchg edx, esi
sub ebx, edx
fsubp
fsubrp
func endp

_text ends

end
