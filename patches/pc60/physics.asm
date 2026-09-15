; insert $161AA
; ---------------------------------------------------------------------------
; high rate player physics. The speed takes the whole 30 Hz
; step (acceleration, turbo) on Accel60Step ticks and holds on the others;
; the distance travelled advances a fraction of a step every tick.
; ---------------------------------------------------------------------------

; speed + acceleration (d1, 16.16) into d1; the caller tests and stores d1
Phys60AddAccel:
	tst.b	(Accel60Step).w
	bne.s	.step
	moveq	#0,d1
.step:
	add.l	($ff064a).l,d1
	rts

; turbo: speed += d1 or -= d1 (whole speed units)
Phys60TurboAdd:
	tst.b	(Accel60Step).w
	beq.s	.hold
	add.w	d1,($ff064a).l
.hold:
	rts
Phys60TurboSub:
	tst.b	(Accel60Step).w
	beq.s	.hold
	sub.w	d1,($ff064a).l
.hold:
	rts

; distance travelled in this tick: speed x factor / TickCount
Phys60Distance:
	mulu.w	($ff064a).l,d0
	lsr.l	#TickShift,d0
	rts

; ---------------------------------------------------------------------------
; increments of a 30 Hz tick divided among TickCount ticks (Half60_d0/d1)
; ---------------------------------------------------------------------------

; background heading follow (d0 = gap, 16.16)
Bg60Follow:
	asr.l	#7,d0
	asr.l	#TickShift,d0
	add.l	d0,($8,a6)
	rts

; bump recovery: d0 = push decay, d1 = lateral move (original: -2, 2 x table)
Bump60Recover:
	moveq	#-2,d0
	bsr	Half60_d0
	move.w	(dat_009F94).l,d1
	add.w	d1,d1
	bra	Half60_d1

; curve drift: d0 = (d0 + d1) x 2 divided, plus the lateral position
Curve60Drift:
	add.w	d1,d0
	add.w	d0,d0
	bsr	Half60_d0
	add.w	($ff0614).l,d0
	rts

; crash sequence counter ($2d,a0) += 2 divided; d0 = the counter
Crash60Fade:
	move.l	d0,-(sp)
	moveq	#2,d0
	bsr	Half60_d0
	add.b	d0,($2d,a0)
	move.l	(sp)+,d0
	move.b	($2d,a0),d0
	rts

; wheel: d1 x 2 divided, stored in $FF065E
Phys60Wheel:
	add.w	d1,d1
	bsr	Half60_d1
	move.w	d1,($ff065e).l
	rts

; full-throttle pose timer ($3a,a0) -= 2 divided, then the original moveq #3,d3
Pose60Timer:
	move.w	d0,-(sp)
	moveq	#2,d0
	bsr	Half60_d0
	sub.w	d0,($3a,a0)
	move.w	(sp)+,d0
	moveq	#3,d3
	rts

; rival position along the road ($26,a0) += d0 x 2 divided
Rival60Advance:
	add.w	d0,d0
	bsr	Half60_d0
	add.w	d0,($26,a0)
	rts

; rival lane step 6 divided, then the original move.w ($2a,a0),d1
Rival60Lane6:
	moveq	#6,d0
	bsr	Half60_d0
	move.w	($2a,a0),d1
	rts

; roadside object distance ($26,a0) += d0 x 2 divided
Scenery60Advance:
	add.w	d0,d0
	bsr	Half60_d0
	add.w	d0,($26,a0)
	rts

; lateral position += d0 x 2 divided
Steer60Lateral:
	add.w	d0,d0
	bsr	Half60_d0
	add.w	d0,($ff0614).l
	rts
