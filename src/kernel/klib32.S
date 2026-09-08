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

#===========================================================================
#        enable_irq  (ESP32-S3 / Xtensa LX7, bare metal, call0 ABI)
#===========================================================================
# void enable_irq(unsigned irq)
# Enable a CPU interrupt line by setting its bit in INTENABLE.
# Equivalent of the 8259 version's:
#   out_byte(INT_CTLMASK, in_byte(INT_CTLMASK) & ~(1 << irq));
# except here the bit is *set* to enable (INTENABLE has enable-high
# polarity, opposite of the 8259's mask-active-high), and there is
# only one register, not two (master/slave) - IRQ is 0-31 directly.
#
# a2 = irq (incoming arg, call0 ABI)

    .text
    .align  4
    .global enable_irq
    .type   enable_irq,@function

enable_irq:
    rsil    a3, 15          # cli equivalent: raise INTLEVEL to max,
                             # save old PS (level+flags) in a3
    movi    a4, 1
    ssl     a2               # shift-amount register = irq (low 5 bits used)
    sll     a4, a4           # a4 = 1 << irq
    rsr     a5, INTENABLE    # a5 = in_byte(INT_CTLMASK) equivalent
    or      a5, a5, a4       # a5 |= (1 << irq)   -- set to ENABLE
    wsr     a5, INTENABLE    # out_byte(INT_CTLMASK, ...)
    rsync                    # ensure WSR takes effect before returning
    wsr     a3, PS           # popf equivalent: restore old int level
    rsync
    ret

    .size   enable_irq, . - enable_irq

#===========================================================================
#        disable_irq  (ESP32-S3 / Xtensa LX7, bare metal, call0 ABI)
#===========================================================================
# int disable_irq(unsigned irq)
# Disable a CPU interrupt line by clearing its bit in INTENABLE.
# Returns 1 iff the interrupt was not already disabled, 0 otherwise.
#
# a2 = irq (in), a2 = return value (out)

    .text
    .align  4
    .global disable_irq
    .type   disable_irq,@function

disable_irq:
    rsil    a3, 15           # cli+pushf: mask all ints, save old PS in a3
    movi    a4, 1
    ssl     a2                # SAR = irq
    sll     a4, a4            # a4 = 1 << irq
    rsr     a5, INTENABLE     # a5 = current INTENABLE (== in_byte(mask reg))
    and     a6, a5, a4        # a6 = tested bit
    beqz    a6, dis_already   # bit already 0 -> was already disabled

    movi    a7, -1
    xor     a7, a4, a7        # a7 = ~(1 << irq)
    and     a5, a5, a7        # clear the bit -> disable
    wsr     a5, INTENABLE
    rsync
    wsr     a3, PS            # popf equivalent
    rsync
    movi    a2, 1              # disabled by this call
    ret

dis_already:
    wsr     a3, PS            # popf equivalent
    rsync
    movi    a2, 0              # already disabled
    ret

    .size   disable_irq, . - disable_irq
