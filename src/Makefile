FILE=asm-tasting.asm
all:
	nasm -f win64 -l nasm.lst $(FILE)
	yasm -f win64 -l yasm.lst $(FILE)
	ml64 /Flmasm.lst /c m$(FILE)
	cat nasm.lst
	cat yasm.lst
	cat masm.lst