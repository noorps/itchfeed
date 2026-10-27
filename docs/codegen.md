# What the big-endian loads compile to

`include/itch/endian.hpp` loads each field with `memcpy` into a native integer
and then `__builtin_bswap*`. I checked the output with `tools/codegen.sh`,
which compiles `tools/codegen_probe.cpp` with the same flags as the benchmark.

With `-march=native` on the i7-1355U, GCC folds each load and swap into one
`movbe` instruction. The 6-byte timestamp is an 8-byte `movbe` from two bytes
earlier, then a mask. Decoding an Add Order uses `movbe` for the 2- and 8-byte
fields and handles the two 4-byte fields (shares, price) together with one
`vpshufb`. There is no byte loop and no call, so I made no change here. The
profile in `docs/profile_baseline_std.txt` puts dispatch plus decode at about 2%
of samples anyway.

Without `-march=native`, plain x86-64 has no `movbe`, and the same source
compiles to `mov` plus `bswap`. That is still two instructions.

```
c++ -O2 -g -march=native  (c++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0)
probe_be16(unsigned char const*):
	movbew	(%rdi), %ax
	ret
probe_be32(unsigned char const*):
	movbel	(%rdi), %eax
	ret
probe_be48(unsigned char const*):
	movabsq	$281474976710655, %rdx
	movbeq	-2(%rdi), %rax
	andq	%rdx, %rax
	ret
probe_be64(unsigned char const*):
	movbeq	(%rdi), %rax
	ret
probe_decode_A(unsigned char const*):
	movbew	1(%rsi), %dx
	vmovq	20(%rsi), %xmm0
	movq	%rdi, %rax
	vmovq	28(%rsi), %xmm1
	movw	%dx, (%rdi)
	movbeq	11(%rsi), %rdx
	vpblendw	$12, %xmm1, %xmm0, %xmm0
	vpshufb	.LC0(%rip), %xmm0, %xmm0
	movq	%rdx, 8(%rdi)
	movzbl	19(%rsi), %edx
	vmovq	%xmm0, 20(%rdi)
	movb	%dl, 16(%rdi)
	ret
```
