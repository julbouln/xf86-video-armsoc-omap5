/* neon-memcpy.S
 Copyright (c) 2009-2010, Code Aurora Forum. All rights reserved.

 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions are met:
     * Redistributions of source code must retain the above copyright
       notice, this list of conditions and the following disclaimer.
     * Redistributions in binary form must reproduce the above copyright
       notice, this list of conditions and the following disclaimer in the
       documentation and/or other materials provided with the distribution.
     * Neither the name of Code Aurora nor the names of its contributors may
       be used to endorse or promote products derived from this software
       without specific prior written permission.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 POSSIBILITY OF SUCH DAMAGE.
 ***************************************************************************/

/***************************************************************************
  Neon memcpy: Attempts to do a memcpy with Neon registers if possible,
     Inputs:
        dest: The destination buffer
        src: The source buffer
        n: The size of the buffer to transfer
     Outputs:

***************************************************************************/

/*
 * General note:
 * The original code that was compiled for rvct used PUSH/POP and VPUSH/VPOP
 * However, it looks like the 2006 CodeSourcery Assembler has issues generating
 * the correct object code for VPOP, resulting in horrific stack crashes.
 * As a result, I've temporarily move PUSH->STMDB, POP->LDMIA, VPUSH->VSTMDB,
 * and VPOP->VLDMIA.  We can revert this back once we update our toolchain.
 *
 * Also, VSHL swaps the source register and the shift-amount register
 * around in 2006-q3.  I've coded this incorrectly so it turns out correct
 * in the object code, but we'll need to undo that later...
 */

	.code 32
	.align 4
	.globl neon_memcpy
	.func

neon_memcpy:
	/*
	 * First, make sure we're not copying < 4 bytes.  If so, we'll
         * just handle it here.
	 */
#if defined __GNUC__ && (4 == __GNUC__ && 1 == __GNUC_MINOR__ && 1 == __GNUC_PATCHLEVEL__)
	stmdb		sp!, {r0}
#else
	push		{r0}
#endif
	cmp		r2, #4
	bgt		neon_gt_4
	/* Copy 0-4 bytes, if needed, and return.*/
	cmp		r2, #0
neon_smallcopy_loop:
	beq		neon_smallcopy_done
	ldrb		r12, [r1], #1
	subs		r2, r2, #1
	strb		r12, [r0], #1
	b		neon_smallcopy_loop
neon_smallcopy_done:
#if defined __GNUC__ && (4 == __GNUC__ && 1 == __GNUC_MINOR__ && 1 == __GNUC_PATCHLEVEL__)
	ldmia		sp!, {r0}
#else
	pop		{r0}
#endif
	bx		lr

	/* Copy 4 or more bytes*/
neon_gt_4:
	/* Preload what we can...*/
	pld		[r0,#0]
	pld		[r1,#0]
#if defined __GNUC__ && (4 == __GNUC__ && 1 == __GNUC_MINOR__ && 1 == __GNUC_PATCHLEVEL__)
	stmdb		sp!, {r4-r5}
#else
	push		{r4-r5}
#endif

neon_check_align:
	/* Check normal word alignment for target. */
	ands		r12, r0, #0x3
	beq		source_alignment_check

	/*
	 * Target is not aligned.  Step through until we get that
	 * word-aligned.  This works better than a loop, according
	 * to our pipeline modeler.
	 */
	cmp		r12, #2
	ldrb		r3, [r1], #1
	ldrleb		r4, [r1], #1
	ldrltb		r5, [r1], #1
	rsb		r12, r12, #4
	sub		r2, r2, r12
	strb		r3, [r0], #1
	strleb		r4, [r0], #1
	strltb		r5, [r0], #1

source_alignment_check:
	ands		r12, r1, #0x3
	bne		neon_memcpy_nonaligned	/* Source is not word aligned.*/
neon_try_16_align:
	cmp		r2, #64
	blt		neon_align_route
	/* This is where we try 16-byte alignment. */
	ands		r12, r0, #0xf
	beq		neon_align_route
	rsb		r12, r12, #16
neon_16_start:
	sub		r2, r2, r12
	lsrs		r3, r12, #2
neon_align_16_4:
	ldr		r4, [r1], #4
	subs		r3, r3, #1
	str		r4, [r0], #4
	bne		neon_align_16_4
neon_align_route:
	/* In this case, both source and target are word-aligned. */
	cmp		r2, #32768
	bge		neon_copy_128p_a
	cmp		r2, #256
	bge		neon_copy_128_a
	cmp		r2, #64
	bge		neon_copy_32_a
	b		neon_copy_finish_a
	nop
neon_copy_128p_a:
	/* We'll copy blocks 128-bytes at a time, but try to call pld to
	 * load in the next page, if possible.
	 */
#if defined __GNUC__ && (4 == __GNUC__ && 1 == __GNUC_MINOR__ && 1 == __GNUC_PATCHLEVEL__)
	vstmdb		sp!, {q4-q7}
#else
	vpush		{q4-q7}
#endif
	mov		r12, r2, lsr #7
neon_copy_128p_loop_a:
	vld1.32		{q0, q1}, [r1]!
	vld1.32		{q2, q3}, [r1]!
	vld1.32		{q4, q5}, [r1]!
	vld1.32		{q6, q7}, [r1]!
	pld		[r1, #0]
	pld		[r1, #1024]
	vst1.32		{q0, q1}, [r0]!
	vst1.32		{q2, q3}, [r0]!
	vst1.32		{q4, q5}, [r0]!
	vst1.32		{q6, q7}, [r0]!
	subs		r12, r12, #1
	bne		neon_copy_128p_loop_a
#if defined __GNUC__ && (4 == __GNUC__ && 1 == __GNUC_MINOR__ && 1 == __GNUC_PATCHLEVEL__)
	vldmia		sp!, {q4-q7}
#else
	vpop		{q4-q7}
#endif
	ands		r2, r2, #0x7f
	beq		neon_end
	cmp		r2, #32
	blt		neon_copy_finish_a
	b		neon_copy_32_a
	/* Copy blocks of 128-bytes (word-aligned) at a time*/
neon_copy_128_a:
#if defined __GNUC__ && (4 == __GNUC__ && 1 == __GNUC_MINOR__ && 1 == __GNUC_PATCHLEVEL__)
	vstmdb		sp!, {q4-q7}
#else
	vpush		{q4-q7}
#endif
	/*
	 * Move to a 1-s based countdown to determine when to loop.  That
	 * allows the subs to set the Z flag without having to explicitly
	 * call cmp to a value.
	 */
	mov		r12, r2, lsr #7
neon_copy_128_loop_a:
	vld1.32		{q0, q1}, [r1]!
	vld1.32		{q2, q3}, [r1]!
	vld1.32		{q4, q5}, [r1]!
	vld1.32		{q6, q7}, [r1]!
	pld		[r1, #0]
	pld		[r1, #128]
	vst1.32		{q0, q1}, [r0]!
	vst1.32		{q2, q3}, [r0]!
	vst1.32		{q4, q5}, [r0]!
	vst1.32		{q6, q7}, [r0]!
	subs		r12, r12, #1
	pld		[r0, #0]
	pld		[r0, #128]
	bne		neon_copy_128_loop_a
#if defined __GNUC__ && (4 == __GNUC__ && 1 == __GNUC_MINOR__ && 1 == __GNUC_PATCHLEVEL__)
	vldmia		sp!, {q4-q7}
#else
	vpop		{q4-q7}
#endif
	ands		r2, r2, #0x7f
	beq		neon_end
	cmp		r2, #32
	blt		neon_copy_finish_a
	/* Copy blocks of 32-bytes (word aligned) at a time*/
neon_copy_32_a:
	mov		r12, r2, lsr #5
neon_copy_32_loop_a:
	vld1.32		{q0,q1}, [r1]!
	subs		r12, r12, #1
	pld		[r1,#0]
	vst1.32		{q0,q1}, [r0]!
	bne		neon_copy_32_loop_a
	ands		r2, r2, #0x1f
	beq		neon_end
neon_copy_finish_a:
neon_copy_16_a:
	movs		r12, r2, lsr #4
	beq		neon_copy_8_a
neon_copy_16_a_loop:
	vld1.32		{q0}, [r1]!
	subs		r12, r12, #1
	vst1.32		{q0}, [r0]!
	bne		neon_copy_16_a_loop
	ands		r2, r2, #0xf
	beq		neon_end
neon_copy_8_a:
	cmp		r2, #8
	blt		neon_copy_4_a
	ldm		r1!, {r4-r5}
	subs		r2, r2, #8
	stm		r0!, {r4-r5}
	/* Copy 4-bytes of word-aligned data at a time*/
neon_copy_4_a:
	cmp		r2, #4
	blt		neon_copy_finish
	ldr		r4, [r1], #4
	subs		r2, r2, #4
	str		r4, [r0], #4
	b		neon_copy_finish

	/*
	 * Handle unaligned data.  The basic concept here is that we'll
	 * try to pull out enough data from the source to get that word-
	 * aligned, then do our writes word-aligned, storing the difference
	 * in a register, and shifting the data as needed.
	 */
neon_memcpy_nonaligned:
	/*
	 * If this is <8 bytes, it makes more sense to just copy it
	 * quickly instead of incurring all kinds of overhead.
	 */
	cmp		r2, #8 /* Let's try this...*/
	ble		neon_copy_finish
	/*
	 * This is where we'll pull out either 1, 2, or 3 bytes of data
	 * from the source as needed to align it, then store off those
	 * bytes in r4.  When we read in the (now) aligned data from the
	 * source, we'll shift the bytes and AND in the r4 data, then write
	 * to the target aligned.
	 *
	 * The conditional ldr calls work slightly faster than the
	 * previous method, confirmed by our pipeline modeler.
	 */
#if defined __GNUC__ && (4 == __GNUC__ && 1 == __GNUC_MINOR__ && 1 == __GNUC_PATCHLEVEL__)
	stmdb		sp!, {r6-r9}
#else
	push		{r6-r9}
#endif
	cmp		r12, #2
	ldrb		r4, [r1], #1
	ldrleb		r5, [r1], #1
	ldrltb		r6, [r1], #1
	rsb		r8, r12, #4
	sub		r2, r2, r8
	lsl		r8, r8, #3
	orrle		r4, r4, r5, lsl #8
	orrlt		r4, r4, r6, lsl #16
	rsb		r9, r8, #32

	cmp		r2, #64
	blt		neon_unaligned_route
	ands		r12, r0, #0xf
	beq		neon_unaligned_route
	rsb		r12, r12, #16
neon_16_start_u:
	sub		r2, r2, r12
	lsrs		r6, r12, #2
neon_align_16_4_u:
	ldr		r5, [r1], #4
	subs		r6, r6, #1
	orr		r4, r4, r5, lsl r8
	str		r4, [r0], #4
	mov		r4, r5, lsr r9
	bne		neon_align_16_4_u
neon_unaligned_route:
	/* Decide which loop block to branch to.*/
	cmp		r2, #256
	bge		neon_copy_64_u
	cmp		r2, #64
	bge		neon_copy_32_u
	b		neon_copy_finish_u
	/* Copy data in 64-byte blocks.*/
neon_copy_64_u:
#if defined __GNUC__ && (4 == __GNUC__ && 1 == __GNUC_MINOR__ && 1 == __GNUC_PATCHLEVEL__)
	vstmdb		sp!, {q4}
	vstmdb		sp!, {q5-q8}
#else
	vpush		{q4}
	vpush		{q5-q8}
#endif
	/* We'll need this for the q register shift later.*/
	vdup.u32	q8, r8
	/*
	 * As above, we determine how many times we can go through the
	 * 64-byte copy loop, then countdown.
	 */
	mov		r12, r2, lsr #6
	and		r2, r2, #0x3f
neon_copy_64_u_loop:
	/* Load 64-bytes into q4-q7.*/
	vld1.32		{q4, q5}, [r1]!
	vld1.32		{q6, q7}, [r1]!
	/*
	 * Shift q0-q3 right so everything but the data we need due to the
	 * alignment falls off the right-hand side.  The branching
	 * is needed, since vshr requires the shift to be an immediate
	 * value.
	 */
	lsls		r5, r8, #28
	bcc		neon_copy_64_u_b8
	bpl		neon_copy_64_u_b16
	vshr.u64	q0, q4, #40
	vshr.u64	q1, q5, #40
	vshr.u64	q2, q6, #40
	vshr.u64	q3, q7, #40
	b		neon_copy_64_unify
neon_copy_64_u_b8:
	vshr.u64	q0, q4, #56
	vshr.u64	q1, q5, #56
	vshr.u64	q2, q6, #56
	vshr.u64	q3, q7, #56
	b		neon_copy_64_unify
neon_copy_64_u_b16:
	vshr.u64	q0, q4, #48
	vshr.u64	q1, q5, #48
	vshr.u64	q2, q6, #48
	vshr.u64	q3, q7, #48
neon_copy_64_unify:
	/*
	 * Shift q4-q7 left by r8 bits to take the alignment into
	 * account.
	 */
#if defined __GNUC__ && (4 == __GNUC__ && 1 == __GNUC_MINOR__ && 1 == __GNUC_PATCHLEVEL__)
	vshl.u64	q4, q8, q4
	vshl.u64	q5, q8, q5
	vshl.u64	q6, q8, q6
	vshl.u64	q7, q8, q7
#else
	vshl.u64	q4, q4, q8
	vshl.u64	q5, q5, q8
	vshl.u64	q6, q6, q8
	vshl.u64	q7, q7, q8
#endif
	/*
	 * The data in s14 will be needed for the next loop iteration.  Move
	 * that to r5.
	 */
	vmov		r5, s14
	/* We'll vorr the shifted data with the data that needs to move back.*/
	vorr		d9, d9, d0
	/* Copy the data from the previous loop into s14.*/
	vmov		s14, r4
	vorr		d10, d10, d1
	vorr		d11, d11, d2
	vorr		d12, d12, d3
	vorr		d13, d13, d4
	vorr		d14, d14, d5
	vorr		d15, d15, d6
	vorr		d8, d8, d7
	subs		r12, r12, #1
	pld		[r1, #0]
	pld		[r1, #128]
	/* Save off the r5 data into r4 for the next iteration.*/
	mov		r4, r5
	vst1.32		{q4, q5}, [r0]!
	vst1.32		{q6, q7}, [r0]!
	pld		[r0, #0]
	pld		[r0, #128]
	bne		neon_copy_64_u_loop
#if defined __GNUC__ && (4 == __GNUC__ && 1 == __GNUC_MINOR__ && 1 == __GNUC_PATCHLEVEL__)
	vldmia		sp!, {q5-q8}
	vldmia		sp!, {q4}
#else
	vpop		{q5-q8}
	vpop		{q4}
#endif
	cmp		r2, #32
	bge		neon_copy_32_u
	b		neon_copy_finish_u
neon_copy_32_u:
#if defined __GNUC__ && (4 == __GNUC__ && 1 == __GNUC_MINOR__ && 1 == __GNUC_PATCHLEVEL__)
	vstmdb		sp!, {q4}
#else
	vpush		{q4}
#endif
	vdup.u32	q4, r8
	mov		r12, r2, lsr #5
	and		r2, r2, #0x1f
neon_copy_32_u_loop:
	vld1.32		{q0, q1}, [r1]!
	lsls		r5, r8, #28
	bcc		neon_copy_32_u_b8
	bpl		neon_copy_32_u_b16
	vshr.u64	q2, q0, #40
	vshr.u64	q3, q1, #40
	b		neon_copy_32_unify
neon_copy_32_u_b8:
	vshr.u64	q2, q0, #56
	vshr.u64	q3, q1, #56
	b		neon_copy_32_unify
neon_copy_32_u_b16:
	vshr.u64	q2, q0, #48
	vshr.u64	q3, q1, #48
neon_copy_32_unify:
#if defined __GNUC__ && (4 == __GNUC__ && 1 == __GNUC_MINOR__ && 1 == __GNUC_PATCHLEVEL__)
	vshl.u64	q0, q4, q0
	vshl.u64	q1, q4, q1
#else
	vshl.u64	q0, q0, q4
	vshl.u64	q1, q1, q4
#endif
	vmov		r5, s14
	vorr		d1, d1, d4
	vmov		s14, r4
	vorr		d2, d2, d5
	vorr		d3, d3, d6
	vorr		d0, d0, d7
	subs		r12, r12, #1
	pld		[r1, #0]
	mov		r4, r5
	vst1.32		{q0, q1}, [r0]!
	bne		neon_copy_32_u_loop
#if defined __GNUC__ && (4 == __GNUC__ && 1 == __GNUC_MINOR__ && 1 == __GNUC_PATCHLEVEL__)
	vldmia		sp!, {q4}
#else
	vpop		{q4}
#endif
neon_copy_finish_u:
neon_copy_16_u:
	movs		r12, r2, lsr #4
	beq		neon_copy_8_u
	vdup.u32	q2, r8
	and		r2, r2, #0xf
neon_copy_16_u_loop:
	vld1.32		{q0}, [r1]!
	lsls		r5, r8, #28
	bcc		neon_copy_16_u_b8
	bpl		neon_copy_16_u_b16
	vshr.u64	q1, q0, #40
	b		neon_copy_16_unify
neon_copy_16_u_b8:
	vshr.u64	q1, q0, #56
	b		neon_copy_16_unify
neon_copy_16_u_b16:
	vshr.u64	q1, q0, #48
neon_copy_16_unify:
#if defined __GNUC__ && (4 == __GNUC__ && 1 == __GNUC_MINOR__ && 1 == __GNUC_PATCHLEVEL__)
	vshl.u64	q0, q2, q0
#else
	vshl.u64	q0, q0, q2
#endif
	vmov		r5, s6
	vorr		d1, d1, d2
	vmov		s6, r4
	vorr		d0, d0, d3
	subs		r12, r12, #1
	mov		r4, r5
	vst1.32		{q0}, [r0]!
	bne		neon_copy_16_u_loop
neon_copy_8_u:
	cmp		r2, #8
	blt		neon_copy_4_u
	ldm		r1!, {r6-r7}
	subs		r2, r2, #8
	orr		r4, r4, r6, lsl r8
	mov		r5, r6, lsr r9
	orr		r5, r5, r7, lsl r8
	stm		r0!, {r4-r5}
	mov		r4, r7, lsr r9
neon_copy_4_u:
	cmp		r2, #4
	blt		neon_copy_last_bits_u
	ldr		r5, [r1], #4
	subs		r2, r2, #4
	orr		r4, r4, r5, lsl r8
	str		r4, [r0], #4
	mov		r4, r5, lsr r9
neon_copy_last_bits_u:
	/*
	 * Remember, r8 contains the size of the data in r4 in bits,
	 * so to get to bytes we'll need to shift 3 places
	 */
	lsr		r8, r8, #0x3
	/* Write out the bytes stored in r4.*/
neon_copy_last_bits_u_loop:
	strb		r4, [r0], #1
	subs		r8, r8, #1
	lsrne		r4, r4, #8
	bne		neon_copy_last_bits_u_loop
#if defined __GNUC__ && (4 == __GNUC__ && 1 == __GNUC_MINOR__ && 1 == __GNUC_PATCHLEVEL__)
	ldmia		sp!, {r6-r9}
#else
	pop		{r6-r9}
#endif
neon_copy_finish:
	cmp		r2, #0
	beq		neon_end
	/*
	 * This just copies the data from source to target one byte
	 * at a time.  For some small values, this makes more sense.
	 * Note that since this code copies data a byte at a time,
	 * both the aligned and unaligned paths can use it.
	 */
neon_copy_finish_loop:
	ldrb		r4, [r1], #1
	subs		r2, r2, #1
	strb		r4, [r0], #1
	bne		neon_copy_finish_loop
neon_end:
#if defined __GNUC__ && (4 == __GNUC__ && 1 == __GNUC_MINOR__ && 1 == __GNUC_PATCHLEVEL__)
	ldmia		sp!, {r4-r5}
	ldmia		sp!, {r0}
#else
	pop		{r4-r5}
	pop		{r0}
#endif
	bx		lr

	.endfunc
	.end
