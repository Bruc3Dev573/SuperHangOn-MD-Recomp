; insert $161AA
; ---------------------------------------------------------------------------
; 60 Hz controls: throttle, brake and steering ramps (whole steps on every second tick)
; ---------------------------------------------------------------------------

AnalogFlags	equ	$ffffc626	; b: bit 0 throttle, bit 1 brake, bit 2 steering
AnalogThrottle	equ	$ffffc627	; b: accelerator amount, 0..255
AnalogBrake	equ	$ffffc628	; b: brake amount, 0..255
AnalogSteer	equ	$ffffc629	; b: steering position, 0..255 (128=center)

; Throttle and brake ramps take the whole 30 Hz step on Accel60Step ticks
; (every second tick, the first one when A, B or C changes) and stay unchanged
; on the others. A live analog axis supplies the target directly.
Ctl60Throttle:
	btst	#0,(AnalogFlags).w
	beq.s	.digital
	tst.b	(Accel60Step).w
	beq.s	.hold
	jmp	(AnalogThrottleTarget).l
.hold:
	moveq	#0,d0
	move.w	($ffffc738).w,d0
	jmp	(loc_00978A).l
.digital:
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
	btst	#1,(AnalogFlags).w
	beq.s	.digital
	tst.b	(Accel60Step).w
	beq.s	.hold
	jmp	(AnalogBrakeTarget).l
.hold:
	moveq	#0,d0
	move.w	($ffffc73a).w,d0
	jmp	(loc_0097EA).l
.digital:
	tst.b	(Accel60Step).w
	bne.s	.step
	jmp	(loc_0097EA).l
.step:
	btst	#$6,($ffffc706).w
	beq.s	.release
	jmp	(loc_0097DE).l
.release:
	jmp	($97d8).l

; Steering axis is an absolute position. The host clears the digital
; left/right bits while this path is active, so the original ramp below does
; not apply a second increment.
Ctl60Steer:
	btst	#2,(AnalogFlags).w
	beq.s	.digital
	jmp	(AnalogSteeringTarget).l
.digital:
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
