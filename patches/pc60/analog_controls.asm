; insert $161AA
; Host analog values live in the unused scheduler RAM area. Values are
; unsigned 0..255; steering is 0..255 with 128 at the centre.

; Map a trigger amount onto the game's full throttle range. The original
; release path remains gradual, while a live analog value sets the target.
AnalogThrottleTarget:
	moveq	#0,d0
	move.b	(AnalogThrottle).w,d0
	beq.s	.release
	mulu.w	#$a8,d0
	lsr.w	#8,d0
	addq.w	#1,d0
	jmp	(loc_00978A).l
.release:
	moveq	#0,d0
	move.w	($ffffc738).w,d0
	subi.w	#$f,d0
	bcs.s	.min
	cmpi.w	#$1,d0
	bcc.s	.done
.min:
	move.w	#$1,d0
.done:
	jmp	(loc_00978A).l

AnalogBrakeTarget:
	moveq	#0,d0
	move.b	(AnalogBrake).w,d0
	beq.s	.release
	mulu.w	#$c5,d0
	lsr.w	#8,d0
	addq.w	#1,d0
	jmp	(loc_0097EA).l
.release:
	moveq	#0,d0
	move.w	($ffffc73a).w,d0
	subi.w	#$f,d0
	bcs.s	.min
	cmpi.w	#$1,d0
	bcc.s	.done
.min:
	move.w	#$1,d0
.done:
	jmp	(loc_0097EA).l

AnalogSteeringTarget:
	moveq	#0,d0
	move.b	(AnalogSteer).w,d0
	mulu.w	#$8c,d0
	lsr.w	#8,d0
	addi.w	#$3a,d0
	move.w	d0,($ffffc73c).w
	moveq	#0,d1
	moveq	#0,d2
	moveq	#0,d3
	rts
