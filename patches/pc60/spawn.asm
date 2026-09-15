; insert $161AA
; ---------------------------------------------------------------------------
; high rate rival spawning and slow-speed timers (sub_00468E, a5 = $FFC80E)
; ---------------------------------------------------------------------------

; $FFC816 countdown: one step once every TickCount ticks
Spawn60Timer:
	tst.w	($ffffc816).w
	beq.s	.done
	tst.b	(TickStep).w
	beq.s	.done
	subq.w	#1,($ffffc816).w
.done:
	rts

; Countdowns tested with bpl after a step of 2 or more expire early if the
; step is divided; they step by the original amount once every TickCount
; ticks instead, which reproduces the 30 Hz timing exactly.

; spawn countdown ($4,a5)
Spawn60Countdown:
	tst.b	(TickStep).w
	beq.s	.later
	sub.w	d0,($4,a5)
	bpl.s	.later
	jmp	($480c).l
.later:
	jmp	(loc_00485E).l

; slow-speed spawn timer ($0,a5)
Spawn60Slow:
	tst.b	(TickStep).w
	beq.s	.later
	subq.w	#2,($0,a5)
	bpl.s	.later
	jmp	($4896).l
.later:
	jmp	(loc_0048BC).l
