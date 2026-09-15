; insert $161AA
; ---------------------------------------------------------------------------
; RAM used by the high rate patches: $FFC620-$FFC6FF is never written by the
; original game (checked by tracing RAM writes).
;
; TickShift (defined when the source is generated, see the Makefile) is the
; log2 of the logic ticks that make one 30 Hz tick of the original game:
; 1 for 60 ticks per second, 2 for 120. TickCount = 1 << TickShift.
; ---------------------------------------------------------------------------
TickCount	equ	1<<TickShift
Sched60Ready	equ	$ffffc620	; b: set when the race tick has finished its tables
Tick60Odd	equ	$ffffc621	; b: tick phase 0 .. TickCount-1 (rounding of partial increments)
Steer60Step	equ	$ffffc622	; b: $ff on ticks where steering input and angle take their step
Steer60Input	equ	$ffffc623	; b: left/right pad bits of the previous tick
Accel60Step	equ	$ffffc624	; b: $ff on ticks where throttle, brake and speed take their step
Accel60Input	equ	$ffffc625	; b: A/B/C pad bits of the previous tick
FastVBlank	equ	$ffffc626	; b: $ff while the race waits for its next tick (the runtime
				;    then gives a VBlank every video frame)
TickStep	equ	$ffffc627	; b: $ff on the last tick of each phase (once per 30 Hz tick)
Steer60Count	equ	$ffffc628	; b: steering phase, the step on TickCount-1 (set on a change)
Accel60Count	equ	$ffffc629	; b: throttle phase, the step on TickCount-1 (set on a change)

; Race tick wait for 1 tick per frame: mark the tick's tables as complete,
; then wait for the next VBlank (VBlank adds 4 to $FFC724).
WaitVBlankTicks4:
	st	(Sched60Ready).w
	st	(FastVBlank).w
.wait:
	cmpi.w	#$4,($ffffc724).w
	blt.s	.wait
	clr.b	(FastVBlank).w
	clr.w	($ffffc724).w
	move.l	d7,-(sp)
	move.b	(Tick60Odd).w,d7
	addq.b	#1,d7
	andi.b	#TickCount-1,d7
	move.b	d7,(Tick60Odd).w
	subi.b	#TickCount-1,d7
	seq	(TickStep).w
	; Steering (left/right) and throttle, brake, turbo (A/B/C) take whole
	; 30 Hz steps once every TickCount ticks, the first one on the tick where
	; their buttons change, so the bike reacts on the first frame and follows
	; the 30 Hz trajectory (lateral position and distance still move a
	; fraction of a step per tick).
	move.b	($ffffc706).w,d7
	andi.b	#$0c,d7
	btst	#$2,(AnalogFlags).w	; analog steering counts as its button
	beq.s	.steer_bits
	moveq	#$04,d7
	tst.b	(AnalogSteer).w
	bmi.s	.steer_bits
	moveq	#$08,d7
.steer_bits:
	cmp.b	(Steer60Input).w,d7
	beq.s	.same
	move.b	d7,(Steer60Input).w
	move.b	#TickCount-1,(Steer60Count).w
	bra.s	.stepdone
.same:
	move.b	(Steer60Count).w,d7
	addq.b	#1,d7
	andi.b	#TickCount-1,d7
	move.b	d7,(Steer60Count).w
.stepdone:
	cmpi.b	#TickCount-1,(Steer60Count).w
	seq	(Steer60Step).w
	move.b	($ffffc706).w,d7
	andi.b	#$70,d7
	cmp.b	(Accel60Input).w,d7
	beq.s	.asame
	move.b	d7,(Accel60Input).w
	move.b	#TickCount-1,(Accel60Count).w
	bra.s	.adone
.asame:
	move.b	(Accel60Count).w,d7
	addq.b	#1,d7
	andi.b	#TickCount-1,d7
	move.b	d7,(Accel60Count).w
.adone:
	cmpi.b	#TickCount-1,(Accel60Count).w
	seq	(Accel60Step).w
	move.l	(sp)+,d7
	rts

; Mode18_Race entry: tick wait, then the original "move.w ($ffffc720).w,d0"
Race60TickWait:
	bsr	WaitVBlankTicks4
	move.w	($ffffc720).w,d0
	rts

; The part of a 30 Hz increment for one tick: (x + phase) >> TickShift, so
; TickCount consecutive ticks add exactly x. Word sized, arithmetic shift;
; condition codes are not meaningful afterwards.
Half60_d1:
	add.b	(Tick60Odd).w,d1
	bcc.s	.nc
	addi.w	#$100,d1
.nc:
	asr.w	#TickShift,d1
	rts
Half60_d0:
	exg	d0,d1
	bsr	Half60_d1
	exg	d0,d1
	rts
