; insert $161AA
; ---------------------------------------------------------------------------
; 60 Hz controls: throttle, brake and steering ramps (whole steps on every second tick)
; ---------------------------------------------------------------------------

; Throttle and brake ramps take the whole 30 Hz step on Accel60Step ticks
; (every second tick, the first one when A, B or C changes) and stay unchanged
; on the others.
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
	rts

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
