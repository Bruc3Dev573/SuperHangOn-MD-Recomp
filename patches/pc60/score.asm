; insert $161AA
; ---------------------------------------------------------------------------
; high rate score: the per-tick points (BCD, from the speed table) are divided
; by TickCount
; ---------------------------------------------------------------------------
Score60Half:
	movem.l	d0/d2-d3,-(sp)
	; BCD word -> binary
	moveq	#0,d0
	moveq	#3,d3
.tobin:
	mulu.w	#10,d0
	rol.w	#4,d1
	move.w	d1,d2
	andi.w	#$f,d2
	add.w	d2,d0
	dbf	d3,.tobin
	; divided by TickCount with the tick phase added: TickCount ticks give
	; the original points
	moveq	#0,d2
	move.b	(Tick60Odd).w,d2
	add.w	d2,d0
	lsr.w	#TickShift,d0
	; binary -> BCD word
	moveq	#0,d1
	moveq	#3,d3
	moveq	#0,d2
.tobcd:
	ext.l	d0
	divu.w	#10,d0
	swap	d0
	move.w	d0,d2
	lsl.w	#4,d2
	lsl.w	#4,d2
	lsl.w	#4,d2
	lsr.w	#4,d1
	or.w	d2,d1
	clr.w	d0
	swap	d0
	dbf	d3,.tobcd
	andi.l	#$ffff,d1
	move.l	d1,($ff0548).l
	movem.l	(sp)+,d0/d2-d3
	rts
