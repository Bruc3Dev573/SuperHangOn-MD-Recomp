; insert $161AA
; ---------------------------------------------------------------------------
; 60 Hz rival spawning and slow-speed timers (sub_00468E, a5 = $FFC80E)
; ---------------------------------------------------------------------------

; $FFC816 countdown: one step every second tick
Spawn60Timer:
	tst.w	($ffffc816).w
	beq.s	.done
	tst.b	(Tick60Odd).w
	beq.s	.done
	subq.w	#1,($ffffc816).w
.done:
	rts

; Countdowns tested with bpl after a step of 2 or more expire half a tick
; early if the step is halved; they step by the original amount on every
; second tick instead, which reproduces the 30 Hz timing exactly.

; spawn countdown ($4,a5)
Spawn60Countdown:
	tst.b	(Tick60Odd).w
	beq.s	.later
	sub.w	d0,($4,a5)
	bpl.s	.later
	jmp	($480c).l
.later:
	jmp	(loc_00485E).l

; slow-speed spawn timer ($0,a5)
Spawn60Slow:
	tst.b	(Tick60Odd).w
	beq.s	.later
	subq.w	#2,($0,a5)
	bpl.s	.later
	jmp	($4896).l
.later:
	jmp	(loc_0048BC).l
