#
# Xtensa LX7 kernel helper routines for CP32 / ESP32-S3.
#
# Currently we only need phys_copy(): a memmove-like physical copy helper
# used by the MINIX-derived kernel code.
#

	.section .text, "ax"
	.align	4
	.global phys_copy
	.type	phys_copy, @function

# void phys_copy(phys_bytes source, phys_bytes destination, phys_bytes bytecount)
#   a2 = source
#   a3 = destination
#   a4 = bytecount
phys_copy:
	beqz	a4, 2f

	/* If destination is below source, copy forward. */
	bltu	a3, a2, 1f
	add	a5, a2, a4
	bltu	a3, a5, 3f

1:
	/* Forward byte copy. */
	beqz	a4, 2f
	l8ui	a5, a2, 0
	s8i	a5, a3, 0
	addi	a2, a2, 1
	addi	a3, a3, 1
	addi	a4, a4, -1
	bnez	a4, 1b
	j	2f

3:
	/* Backward byte copy for overlap. */
	add	a2, a2, a4
	add	a3, a3, a4
	beqz	a4, 2f
4:
	addi	a2, a2, -1
	addi	a3, a3, -1
	l8ui	a5, a2, 0
	s8i	a5, a3, 0
	addi	a4, a4, -1
	bnez	a4, 4b

2:
	ret

	.size phys_copy, .-phys_copy
