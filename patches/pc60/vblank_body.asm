; insert $161AA
; ---------------------------------------------------------------------------
; Race VBlank (replaces $E906-$E9CC). The original ran the tick uploads on the
; 1st VBlank of a tick and the HUD work on the 2nd. When the main loop has
; completed a 60 Hz tick (Sched60Ready) both run on this VBlank. Modes that
; still wait with WaitVBlankTicks8 (race start, ranking) keep the original
; alternation.
; ---------------------------------------------------------------------------
VBlank60Race:
	move.l	#$40000010,($c00004).l
	move.l	#$0,($c00000).l
	move.w	($ffffc724).w,d0
	addq.w	#4,d0
	cmpi.w	#$10,d0
	bcs.s	.cap
	moveq	#12,d0
.cap:
	move.w	d0,($ffffc724).w
	tst.b	(Sched60Ready).w
	beq.s	.original
	clr.b	(Sched60Ready).w
	bsr	.state1
	bsr	.state2
	jmp	(loc_00E9CC).l
.original:
	cmpi.w	#$4,d0
	bne.s	.not1
	bsr	.state1
	jmp	(loc_00E9CC).l
.not1:
	cmpi.w	#$8,d0
	bne.s	.late
	bsr	.state2
	jsr	(sub_00DD38).l
	jmp	(loc_00E9CC).l
.late:
	jsr	(sub_00F864).l
	jmp	(loc_00E9CC).l
; original state 1: flip line tables, uploads, joypads
.state1:
	tst.b	($ff06c4).l
	beq.s	.noflip
	eori.w	#$700,($ff0580).l
	clr.b	($ff06c4).l
.noflip:
	move.l	#$9540968c,d1
	move.w	#$977f,d2
	tst.w	($ff0580).l
	beq.s	.buf0
	move.l	#$95c0968f,d1
.buf0:
	move.l	#$93c09401,d0
	move.l	#$4c000083,($ffffc70a).w
	jsr	(sub_00827C).l
	jsr	(sub_008238).l
	jsr	(sub_008464).l
	jsr	(sub_00F86A).l
	jsr	(sub_00F7C4).l
	jsr	(ReadJoypads).l
	jmp	(sub_00DD38).l
; original state 2: HUD / scores / map (without its sub_00DD38 call)
.state2:
	jsr	(sub_00F864).l
	jsr	(sub_00F884).l
	jsr	(sub_00F754).l
	jsr	(sub_00EB16).l
	jsr	(sub_00C04C).l
	jsr	(sub_00C078).l
	jsr	(sub_00C096).l
	jsr	(sub_00C0B4).l
	jsr	(sub_00C142).l
	jsr	(sub_00BB76).l
	jmp	(sub_00BD0C).l
