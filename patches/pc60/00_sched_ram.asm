; insert $161AA
; ---------------------------------------------------------------------------
; RAM used by the 60 Hz patches: $FFC620-$FFC6FF is never written by the
; original game (checked by tracing RAM writes).
; ---------------------------------------------------------------------------
Sched60Ready	equ	$ffffc620	; b: set when the race tick has finished its tables
Tick60Odd	equ	$ffffc621	; b: 0/1 alternating per tick (half steps of odd increments)
Steer60Step	equ	$ffffc622	; b: $ff on ticks where steering input and angle take their step
Steer60Input	equ	$ffffc623	; b: left/right pad bits of the previous tick
Accel60Step	equ	$ffffc624	; b: $ff on ticks where throttle, brake and speed take their step
Accel60Input	equ	$ffffc625	; b: A/B/C pad bits of the previous tick

; Race tick wait for 1 tick per frame: mark the tick's tables as complete,
; then wait for the next VBlank (VBlank adds 4 to $FFC724).
WaitVBlankTicks4:
	st	(Sched60Ready).w
.wait:
	cmpi.w	#$4,($ffffc724).w
	blt.s	.wait
	clr.w	($ffffc724).w
	bchg	#0,(Tick60Odd).w
	; Steering (left/right) and throttle, brake, turbo (A/B/C) take whole
	; 30 Hz steps on every second tick, the first one on the tick where their
	; buttons change, so the bike reacts on the first frame and follows the
	; 30 Hz trajectory (lateral position and distance still move half a step
	; per tick).
	move.l	d7,-(sp)
	move.b	($ffffc706).w,d7
	andi.b	#$0c,d7
	cmp.b	(Steer60Input).w,d7
	beq.s	.same
	move.b	d7,(Steer60Input).w
	st	(Steer60Step).w
	bra.s	.stepdone
.same:
	not.b	(Steer60Step).w
.stepdone:
	move.b	($ffffc706).w,d7
	andi.b	#$70,d7
	cmp.b	(Accel60Input).w,d7
	beq.s	.asame
	move.b	d7,(Accel60Input).w
	st	(Accel60Step).w
	bra.s	.adone
.asame:
	not.b	(Accel60Step).w
.adone:
	move.l	(sp)+,d7
	rts

; Mode18_Race entry: tick wait, then the original "move.w ($ffffc720).w,d0"
Race60TickWait:
	bsr.s	WaitVBlankTicks4
	move.w	($ffffc720).w,d0
	rts

; Half of an increment for one 60 Hz tick: (x + Tick60Odd) >> 1, so two
; consecutive ticks add exactly x (rounded down, then up). Word sized,
; arithmetic shift; condition codes are not meaningful afterwards.
Half60_d1:
	add.b	(Tick60Odd).w,d1
	bcc.s	.nc
	addi.w	#$100,d1
.nc:
	asr.w	#1,d1
	rts
Half60_d0:
	exg	d0,d1
	bsr.s	Half60_d1
	exg	d0,d1
	rts
