	.file	"likely_and_unlikey.cpp"
	.text
	.globl	_Z5summaPi
	.type	_Z5summaPi, @function
_Z5summaPi:
.LFB0:
	.cfi_startproc
	pushq	%rbp
	.cfi_def_cfa_offset 16
	.cfi_offset 6, -16
	movq	%rsp, %rbp
	.cfi_def_cfa_register 6
	movq	%rdi, -24(%rbp)
	movl	$0, -12(%rbp)
	movl	$0, -8(%rbp)
	movl	$0, -4(%rbp)
	jmp	.L2
.L5:
	movl	-4(%rbp), %eax
	cltq
	leaq	0(,%rax,4), %rdx
	movq	-24(%rbp), %rax
	addq	%rdx, %rax
	movl	(%rax), %eax
	cmpl	$127, %eax
	jle	.L3
	movl	-4(%rbp), %eax
	cltq
	leaq	0(,%rax,4), %rdx
	movq	-24(%rbp), %rax
	addq	%rdx, %rax
	movl	(%rax), %eax
	addl	%eax, -12(%rbp)
	jmp	.L4
.L3:
	movl	-4(%rbp), %eax
	cltq
	leaq	0(,%rax,4), %rdx
	movq	-24(%rbp), %rax
	addq	%rdx, %rax
	movl	(%rax), %eax
	addl	%eax, -8(%rbp)
.L4:
	addl	$1, -4(%rbp)
.L2:
	cmpl	$9, -4(%rbp)
	jle	.L5
	movl	-12(%rbp), %eax
	subl	-8(%rbp), %eax
	popq	%rbp
	.cfi_def_cfa 7, 8
	ret
	.cfi_endproc
.LFE0:
	.size	_Z5summaPi, .-_Z5summaPi
	.globl	main
	.type	main, @function
main:
.LFB1:
	.cfi_startproc
	pushq	%rbp
	.cfi_def_cfa_offset 16
	.cfi_offset 6, -16
	movq	%rsp, %rbp
	.cfi_def_cfa_register 6
	subq	$64, %rsp
	movq	%fs:40, %rax
	movq	%rax, -8(%rbp)
	xorl	%eax, %eax
	movl	$0, -52(%rbp)
	jmp	.L8
.L9:
	movl	-52(%rbp), %eax
	imull	%eax, %eax
	imull	-52(%rbp), %eax
	imull	-52(%rbp), %eax
	leal	50(%rax), %edx
	movl	-52(%rbp), %eax
	cltq
	movl	%edx, -48(%rbp,%rax,4)
	addl	$1, -52(%rbp)
.L8:
	cmpl	$9, -52(%rbp)
	jle	.L9
	leaq	-48(%rbp), %rax
	movq	%rax, %rdi
	call	_Z5summaPi
	movl	$0, %eax
	movq	-8(%rbp), %rdx
	subq	%fs:40, %rdx
	je	.L11
	call	__stack_chk_fail@PLT
.L11:
	leave
	.cfi_def_cfa 7, 8
	ret
	.cfi_endproc
.LFE1:
	.size	main, .-main
	.ident	"GCC: (GNU) 15.1.1 20250425"
	.section	.note.GNU-stack,"",@progbits
