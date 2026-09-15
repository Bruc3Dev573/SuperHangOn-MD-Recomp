; insert $161AA
; ---------------------------------------------------------------------------
; 60 Hz roadside scenery spawning (sub_004512): the spawn pattern advances on
; every second tick, keeping the original object density along the road
; ---------------------------------------------------------------------------
Scenery60Spawn:
	tst.b	(Tick60Odd).w
	beq.s	.skip
	tst.w	($c,a5)
	beq.s	.skip
	jmp	($455a).l
.skip:
	jmp	(loc_0045D2).l

; road palette flash: its countdown steps on every second tick
Flash60Countdown:
	tst.b	(Tick60Odd).w
	beq.s	.keep
	subq.b	#1,($1b,a0)
	bne.s	.keep
	jmp	($a3b2).l
.keep:
	jmp	(loc_00A3BC).l

; engine sound ducking after overtaking ($FFC826): one step every second tick
Duck60Timer:
	tst.w	($ffffc826).w
	beq.s	.done
	tst.b	(Tick60Odd).w
	beq.s	.done
	subq.w	#1,($ffffc826).w
.done:
	jmp	(loc_008B6A).l

; hit test against roadside objects: counted on every second tick, so fast
; objects are sampled at the same positions as in the 30 Hz game
Hit60Scenery:
	tst.b	(Tick60Odd).w
	beq.s	.done
	addq.w	#1,($ff0656).l
.done:
	rts
