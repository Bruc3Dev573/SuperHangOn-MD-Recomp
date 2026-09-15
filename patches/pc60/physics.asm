; insert $161AA
; ---------------------------------------------------------------------------
; 60 Hz player physics. The speed takes the whole 30 Hz
; step (acceleration, turbo) on Accel60Step ticks and holds on the others;
; the distance travelled advances half a step every tick.
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

; distance travelled in this tick: speed x factor / 2
Phys60Distance:
	mulu.w	($ff064a).l,d0
	lsr.l	#1,d0
	rts
