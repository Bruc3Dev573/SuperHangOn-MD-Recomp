; insert $161AA
; ---------------------------------------------------------------------------
; high rate player bike sprite: shake (bump / off road) once every TickCount ticks, so
; the random draws and the shake pattern keep their 30 Hz rate
; ---------------------------------------------------------------------------
Player60Shake:
	tst.w	($1c,a0)
	bne.s	.shake
	jmp	(loc_00A556).l
.shake:
	tst.b	(TickStep).w
	bne.s	.go
	clr.w	($1c,a0)
	rts
.go:
	jsr	(Random).l
	jmp	($a524).l

; original mode, off road: penalty countdown ($36,a0) steps once every TickCount ticks
Player60OffroadCount:
	tst.b	(TickStep).w
	beq.s	.keep
	subq.w	#1,($36,a0)
	bpl.s	.keep
	jmp	($a7c6).l
.keep:
	jmp	(loc_00A7D4).l

; tyre squeal repeat ($9,a5) and crash animation frames ($33,a0): original
; steps of 2 once every TickCount ticks (see Spawn60Countdown)
Squeal60Timer:
	tst.b	(TickStep).w
	beq.s	.later
	subq.b	#2,($9,a5)
	bpl.s	.later
	jmp	($9a3a).l
.later:
	jmp	(loc_009A9A).l
Crash60Frame:
	tst.b	($32,a0)
	beq.s	.step			; first frame of the sequence: load it now
	tst.b	(TickStep).w
	beq.s	.later
.step:
	subq.b	#2,($33,a0)
	bpl.s	.later
	jmp	($ac94).l
.later:
	jmp	(loc_00ACB8).l
Crash60Frame2:
	tst.b	($32,a0)
	beq.s	.step
	tst.b	(TickStep).w
	beq.s	.later
.step:
	subq.b	#2,($33,a0)
	bpl.s	.later
	jmp	($aed0).l
.later:
	jmp	(loc_00AEF4).l
