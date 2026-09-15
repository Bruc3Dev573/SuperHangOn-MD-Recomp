; insert $161AA
; ---------------------------------------------------------------------------
; 60 Hz rival bikes (objects at $FFCC40, $40 bytes each, a0)
; ---------------------------------------------------------------------------

; The object's own tick counter ($39,a0) paces the slower updates, so their
; phase does not depend on when the race started.

; ($30,a0) slipstream counter: +1 / -1 every second tick
Rival60Draft:
	btst	#$0,($39,a0)
	beq.s	.done
	tst.w	($ff0652).l
	beq.s	.dec
	addq.w	#1,($30,a0)
	bra.s	.done
.dec:
	tst.w	($30,a0)
	beq.s	.done
	subq.w	#1,($30,a0)
.done:
	jmp	(loc_00D630).l

; ($a,a0) speed: +1 every second tick (every tick at 30 Hz), or one step
; towards ($32,a0) every fourth tick (every second tick at 30 Hz)
Rival60Speed:
	btst	#$2,($3a,a0)
	beq.s	.follow
	btst	#$0,($39,a0)
	beq.s	.done
	addq.w	#1,($a,a0)
	bra.s	.done
.follow:
	move.b	($39,a0),d0
	not.b	d0
	andi.b	#$3,d0
	bne.s	.done
	moveq	#1,d0
	move.w	($32,a0),d1
	cmp.w	($a,a0),d1
	beq.s	.done
	bpl.s	.up
	neg.w	d0
.up:
	add.w	d0,($a,a0)
.done:
	jmp	(loc_00D65A).l

; collision push on the player: half per tick, then the original add
Rival60Push:
	bsr	Half60_d0
	add.w	($ff065a).l,d0
	rts

; crashed rival: x position stored, tumble phase advances every second tick
Rival60Tumble:
	move.w	d0,($10,a0)
	btst	#$0,($39,a0)
	beq.s	.done
	addq.b	#1,($3e,a0)
.done:
	rts
