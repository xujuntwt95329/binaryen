	.text
	.file	"/b/build/slave/linux/build/src/buildbot/work/gcc/gcc/testsuite/gcc.c-torture/execute/950221-1.c"
	.globl	g1
	.type	g1,@function
g1:                                     # @g1
	.param  	i32, i32
	.result 	i32
	.local  	i32
# BB#0:                                 # %entry
	return  	$2
.Lfunc_end0:
	.size	g1, .Lfunc_end0-g1

	.globl	g2
	.type	g2,@function
g2:                                     # @g2
	.param  	i32
	.result 	i32
# BB#0:                                 # %entry
	block   	.LBB1_2
	i32.const	$push0=, -559038737
	i32.ne  	$push1=, $0, $pop0
	br_if   	$pop1, .LBB1_2
# BB#1:                                 # %if.end
	i32.const	$push2=, 0
	call    	exit, $pop2
	unreachable
.LBB1_2:                                  # %if.then
	call    	abort
	unreachable
.Lfunc_end1:
	.size	g2, .Lfunc_end1-g2

	.globl	f
	.type	f,@function
f:                                      # @f
	.result 	i32
	.local  	i32, i32
# BB#0:                                 # %entry
	i32.const	$1=, 0
	block   	.LBB2_3
	i32.load	$push1=, parsefile($1)
	i32.load	$0=, 0($pop1)
	br_if   	$0, .LBB2_3
# BB#1:                                 # %entry
	i32.load	$push0=, el($1)
	i32.const	$push2=, 0
	i32.eq  	$push3=, $pop0, $pop2
	br_if   	$pop3, .LBB2_3
# BB#2:                                 # %if.end
	return  	$1
.LBB2_3:                                  # %alabel
	i32.call	$discard=, g2, $0
	unreachable
.Lfunc_end2:
	.size	f, .Lfunc_end2-f

	.globl	main
	.type	main,@function
main:                                   # @main
	.result 	i32
	.local  	i32
# BB#0:                                 # %alabel.i
	i32.const	$0=, 0
	i32.store	$push0=, el($0), $0
	i32.load	$push1=, parsefile($pop0)
	i32.const	$push2=, -559038737
	i32.store	$push3=, 0($pop1), $pop2
	i32.call	$discard=, g2, $pop3
	unreachable
.Lfunc_end3:
	.size	main, .Lfunc_end3-main

	.type	basepf,@object          # @basepf
	.bss
	.globl	basepf
	.align	2
basepf:
	.zero	8
	.size	basepf, 8

	.type	parsefile,@object       # @parsefile
	.data
	.globl	parsefile
	.align	2
parsefile:
	.int32	basepf
	.size	parsefile, 4

	.type	el,@object              # @el
	.bss
	.globl	el
	.align	2
el:
	.int32	0                       # 0x0
	.size	el, 4

	.type	filler,@object          # @filler
	.globl	filler
	.align	4
filler:
	.zero	49152
	.size	filler, 49152


	.ident	"clang version 3.8.0 "
	.section	".note.GNU-stack","",@progbits