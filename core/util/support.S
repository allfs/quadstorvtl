/*-
 * Copyright (c) 2003 Peter Wemm.
 * Copyright (c) 1993 The Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/amd64/amd64/support.S,v 1.130.2.2.2.1 2010/12/21 17:09:25 kensmith Exp $
 */

#include "sysdefs/asmacros.h"

/*
 * bcopy family
 * void bzero(void *buf, u_int len)
 */

/* done */
ENTRY(bzero)
	movq	%rsi,%rcx
	xorl	%eax,%eax
	shrq	$3,%rcx
	cld
	rep
	stosq
	movq	%rsi,%rcx
	andq	$7,%rcx
	rep
	stosb
	ret
END(bzero)
	
ENTRY(bcmp)
	movq	%rdx,%rcx
	shrq	$3,%rcx
	cld					/* compare forwards */
	repe
	cmpsq
	jne	1f

	movq	%rdx,%rcx
	andq	$7,%rcx
	repe
	cmpsb
1:
	setne	%al
	movsbl	%al,%eax
	ret
END(bcmp)

/*
 * bcopy(src, dst, cnt)
 *       rdi, rsi, rdx
 *  ws@tools.de     (Wolfgang Solfrank, TooLs GmbH) +49-228-985800
 */
ENTRY(bcopy)
	xchgq	%rsi,%rdi
	movq	%rdx,%rcx

	movq	%rdi,%rax
	subq	%rsi,%rax
	cmpq	%rcx,%rax			/* overlapping && src < dst? */
	jb	1f

	shrq	$3,%rcx				/* copy by 64-bit words */
	cld					/* nope, copy forwards */
	rep
	movsq
	movq	%rdx,%rcx
	andq	$7,%rcx				/* any bytes left? */
	rep
	movsb
	ret

	/* ALIGN_TEXT */
1:
	addq	%rcx,%rdi			/* copy backwards */
	addq	%rcx,%rsi
	decq	%rdi
	decq	%rsi
	andq	$7,%rcx				/* any fractional bytes? */
	std
	rep
	movsb
	movq	%rdx,%rcx			/* copy remainder by 32-bit words */
	shrq	$3,%rcx
	subq	$7,%rsi
	subq	$7,%rdi
	rep
	movsq
	cld
	ret
END(bcopy)
	
/*
 * Note: memcpy does not support overlapping copies
 */
ENTRY(memcpy)
	movq	%rdx,%rcx
	shrq	$3,%rcx				/* copy by 64-bit words */
	cld					/* copy forwards */
	rep
	movsq
	movq	%rdx,%rcx
	andq	$7,%rcx				/* any bytes left? */
	rep
	movsb
	ret
END(memcpy)
