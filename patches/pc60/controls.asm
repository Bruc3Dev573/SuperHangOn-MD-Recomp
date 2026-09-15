; insert $161AA
; ---------------------------------------------------------------------------
; high rate controls: throttle, brake and steering ramps (whole steps once every
; TickCount ticks), with the steering target of an analog stick
; ---------------------------------------------------------------------------

; Analog steering: the frontend writes a controller stick here every frame
; (0 when it is not used). The value is the target of the steering ramp, which
; moves towards it with the game's own steps: fully pushed gives the button's
; ramp. With the flag clear the buttons drive the ramp as in the original.
AnalogFlags	equ	$ffffc640	; b: bit 2 steering is analog
AnalogSteer	equ	$ffffc643	; b: signed, -127 (left) .. 127 (right)

; Throttle and brake ramps take the whole 30 Hz step on Accel60Step ticks
; (once every TickCount ticks, the first one when A, B or C changes) and stay
; unchanged on the others.
Ctl60Throttle:
	tst.b	(Accel60Step).w
	bne.s	.step
	jmp	(loc_00978A).l
.step:
	btst	#$4,($ffffc706).w
	beq.s	.release
	jmp	(loc_00977C).l
.release:
	jmp	($976a).l
Ctl60BrakeRamp:
	tst.b	(Accel60Step).w
	bne.s	.step
	jmp	(loc_0097EA).l
.step:
	btst	#$6,($ffffc706).w
	beq.s	.release
	jmp	(loc_0097DE).l
.release:
	jmp	($97d8).l
; steering ramp: the whole steps d1/d2/d3 on Steer60Step ticks, none on the
; others; then the original "moveq #0,d0 / move.w ($ffffc73c).w,d0"
Ctl60Steer:
	tst.b	(Steer60Step).w
	bne.s	.step
	moveq	#0,d1
	moveq	#0,d2
	moveq	#0,d3
.step:
	moveq	#0,d0
	move.w	($ffffc73c).w,d0
	btst	#$2,(AnalogFlags).w
	bne.s	.analog
	rts
; steering $FFC73C (d0, $3A .. $C6, centre $80, left above): towards the
; target $80 - stick x 70/127 with the button steps: d2 away from the centre,
; d3 back from the other side, d1 back towards the centre on the same side
.analog:
	movem.l	d4-d5,-(sp)
	move.b	(AnalogSteer).w,d4
	ext.w	d4
	muls.w	#-70,d4
	divs.w	#127,d4
	addi.w	#$80,d4
	cmp.w	d4,d0
	beq.s	.done
	bcc.s	.right
	; left (up)
	move.w	d2,d5
	cmpi.w	#$80,d0
	bcc.s	.left_step
	move.w	d3,d5
	cmpi.w	#$80,d4
	bhi.s	.left_step
	move.w	d1,d5
.left_step:
	add.w	d5,d0
	cmp.w	d4,d0
	bls.s	.done
	move.w	d4,d0
	bra.s	.done
.right:
	move.w	d2,d5
	cmpi.w	#$80,d0
	bcs.s	.right_step
	move.w	d3,d5
	cmpi.w	#$80,d4
	bcs.s	.right_step
	move.w	d1,d5
.right_step:
	sub.w	d5,d0
	bcs.s	.right_clamp
	cmp.w	d4,d0
	bcc.s	.done
.right_clamp:
	move.w	d4,d0
.done:
	movem.l	(sp)+,d4-d5
	addq.l	#4,sp			; (the caller's button code is skipped)
	jmp	(loc_00988E).l

; steering angle ($38,a0) follows the input by at most 12 on Steer60Step
; ticks (the original limit), not at all on the others
Steer60Angle:
	tst.b	(Steer60Step).w
	bne.s	.limit
	moveq	#0,d0
	jmp	(loc_00A438).l
.limit:
	cmpi.w	#$c,d0
	ble.s	.low
	move.w	#$c,d0
	jmp	(loc_00A438).l
.low:
	cmpi.w	#$fff4,d0
	bgt.s	.done
	move.w	#$fff4,d0
.done:
	jmp	(loc_00A438).l
