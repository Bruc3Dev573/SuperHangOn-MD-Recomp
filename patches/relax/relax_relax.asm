; insert $161AA
; ---------------------------------------------------------------------------
; RELAX MODE (PC port): arcade race without timer, rival bikes, score or
; ending; the courses follow each other as one track: after the last stage
; of a course the first stage of the next one comes like any other stage.
; ---------------------------------------------------------------------------
RelaxMode	equ	$ffffc630	; b: $ff while relax mode is selected
RelaxCursor	equ	$ffffc631	; b: 0 arcade, 1 original, 2 relax (mode menu cursor)
RelaxPending	equ	$ffffc632	; b: $ff from the preparation of the next course's first stage to its start
RelaxBoundary	equ	$ffffc633	; b: $ff during the first stage of a course reached without a restart
RelaxHudRedraw	equ	$ffffc634	; b: $ff: redraw the HUD (course name, stage, map) on the next race VBlank
RelaxNextCourse	equ	$ffffc636	; w: the course that follows the current one

RelaxText:
	dc.w	$e62f,$e622,$e629,$e61e,$e635,$0000,$0000,$0000,$0000,$e62a,$e62c,$e621,$e622

RelaxBlank:
	dcb.w	13,$0000

; mode menu: the three choices on rows 14/16/18 (the original used rows 15/17
; for two, which now leaves the third touching the SEGA logo)
Relax_DrawModeMenu:
	move.l	#$579a0003,d7		; clear NEW GAME / PASSWORD
	moveq	#12,d6
	lea	(RelaxBlank).l,a6
	jsr	(VdpWriteWords).l
	move.l	#$589a0003,d7
	moveq	#12,d6
	lea	(RelaxBlank).l,a6
	jsr	(VdpWriteWords).l
	move.l	#$571a0003,d7
	moveq	#12,d6
	lea	(dat_007A8C).l,a6
	jsr	(VdpWriteWords).l
	move.l	#$581a0003,d7
	moveq	#12,d6
	lea	(dat_007A72).l,a6
	jsr	(VdpWriteWords).l
	move.l	#$591a0003,d7
	moveq	#12,d6
	lea	(RelaxText).l,a6
	clr.b	(RelaxCursor).w		; cursor on the current arcade / original choice
	btst	#$2,($ffffb600).w
	beq.s	.arcade
	move.b	#$1,(RelaxCursor).w
.arcade:
	jmp	(VdpWriteWords).l

; title screen init: relax mode off (attract demo and new games)
Relax_TitleInit:
	clr.b	(RelaxMode).w
	clr.b	(RelaxPending).w
	clr.b	(RelaxBoundary).w
	clr.b	(RelaxHudRedraw).w
	jsr	(sub_008210).l
	clr.w	($ffffc754).w
	rts

; up / down on the mode menu: three choices; bit 2 of $FFB600 = original mode
Relax_MoveCursor:
	jsr	(sub_0077F4).l
	move.b	(RelaxCursor).w,d0
	btst	#$0,($ffffc707).w
	beq.s	.down
	subq.b	#1,d0
	bpl.s	.store
	moveq	#2,d0
	bra.s	.store
.down:
	addq.b	#1,d0
	cmpi.b	#$3,d0
	bcs.s	.store
	moveq	#0,d0
.store:
	move.b	d0,(RelaxCursor).w
	bclr	#$2,($ffffb600).w
	cmpi.b	#$1,d0
	bne.s	.done
	bset	#$2,($ffffb600).w
.done:
	jmp	(loc_007592).l

; choice confirmed: relax starts an arcade game with the relax flag set
Relax_Select:
	clr.b	(RelaxMode).w
	cmpi.b	#$2,(RelaxCursor).w
	bne.s	.notrelax
	st	(RelaxMode).w
	jmp	($7568).l
.notrelax:
	btst	#$2,($ffffb600).w
	beq.s	.arcade
	jmp	(loc_007574).l
.arcade:
	jmp	($7568).l

; mode menu cursor: blinking arrow on one of three lines
Relax_DrawCursor:
	move.l	#$57160003,d7
	move.l	d7,($c00004).l
	move.w	#$0,($c00000).l
	move.l	#$58160003,d7
	move.l	d7,($c00004).l
	move.w	#$0,($c00000).l
	move.l	#$59160003,d7
	move.l	d7,($c00004).l
	move.w	#$0,($c00000).l
	btst	#$4,($ffffc726).w
	beq.s	.done
	move.l	#$57160003,d7
	tst.b	(RelaxCursor).w
	beq.s	.draw
	move.l	#$58160003,d7
	cmpi.b	#$1,(RelaxCursor).w
	beq.s	.draw
	move.l	#$59160003,d7
.draw:
	move.l	d7,($c00004).l
	move.w	#$c349,($c00000).l
.done:
	rts

; race timer: frozen in relax mode
Relax_Timer:
	tst.b	(RelaxMode).w
	bne.s	.stop
	move.b	($2,a0),d0
	bpl.s	.run
.stop:
	rts
.run:
	jmp	(loc_0096E4).l

; course completed: next course instead of the ending
Relax_CourseDone:
	tst.b	(RelaxMode).w
	bne.s	.next
	move.w	#$1c,($ffffc720).w
	rts
.next:
	move.w	d0,-(sp)
	move.w	($ff060c).l,d0
	addq.w	#1,d0
	andi.w	#$3,d0
	move.w	d0,($ff060c).l
	move.w	#$10,($ffffc720).w
	move.w	(sp)+,d0
	rts

; no rival bikes on the starting grid ...
Relax_Grid:
	jsr	(sub_0070FC).l
	tst.b	(RelaxMode).w
	beq.s	.grid
	jmp	($8c96).l
.grid:
	lea	($ffffcc40).w,a1
	jmp	($8c60).l

; ... and none spawned during the race
Relax_Spawn:
	tst.b	(RelaxMode).w
	beq.s	.spawn
	rts
.spawn:
	move.w	#$ffc0,d0
	move.w	#$40,d1
	jmp	($48c6).l

; race VBlank end: without timer and score the HUD keeps course, stage,
; speed and map (window plane rows 1-3 and the time digits are blanked)
Relax_Hud:
	tst.b	(RelaxMode).w
	beq.w	.done
	tst.b	(RelaxHudRedraw).w
	beq.s	.blank
	clr.b	(RelaxHudRedraw).w
	movem.l	d0-d7/a0-a6,-(sp)
	move.l	#$53000003,($c00004).l	; row 6: the stage map of the previous course
	move.w	#39,d0
.map:
	move.w	#$e6fe,($c00000).l
	dbf	d0,.map
	jsr	(sub_00BA9A).l		; HUD as at the start of a race
	movem.l	(sp)+,d0-d7/a0-a6
.blank:
	movem.l	d0-d1/a5,-(sp)
	lea	($c00000).l,a5
	move.l	#$50800003,d1		; window plane (VRAM $D000, 64 columns): rows 1-3
	moveq	#2,d0
.rows:
	move.l	d1,($4,a5)
	swap	d0
	move.w	#39,d0
.cols:
	move.w	#$e6fe,(a5)
	dbf	d0,.cols
	swap	d0
	addi.l	#$00800000,d1
	dbf	d0,.rows
	move.l	#$52200003,($4,a5)	; time digits: rows 4-5, columns 16-21
	moveq	#5,d0
.time1:
	move.w	#$e6fe,(a5)
	dbf	d0,.time1
	move.l	#$52a00003,($4,a5)
	moveq	#5,d0
.time2:
	move.w	#$e6fe,(a5)
	dbf	d0,.time2
	movem.l	(sp)+,d0-d1/a5
.done:
	lea	($ff1500).l,a0
	rts

; ---- one track --------------------------------------------------------------

; sub_00E348, preparing the stage after the current one: after the last
; stage the goal is prepared (FF057C = $FF80); relax prepares the next
; course's first stage instead, as a normal stage change
Relax_LastStage:
	tst.b	(RelaxMode).w
	bne.s	.next
	move.w	#$ff80,($ff057c).l
	rts
.next:
	move.w	($ff060c).l,d0
	addq.w	#1,d0
	andi.w	#$3,d0
	move.w	d0,(RelaxNextCourse).w
	st	(RelaxPending).w
	addq.l	#4,sp
	jmp	(loc_00E3A2).l

; sub_00ED50 (stage data of course FF060C, stage d1): the stage after the
; current one is the next course's first while it is pending
Relax_CourseOfStage:
	move.w	($ff060c).l,d0
	tst.b	(RelaxPending).w
	beq.s	.done
	move.w	d2,-(sp)
	move.w	($ff060a).l,d2
	addq.w	#1,d2
	cmp.w	d1,d2
	bne.s	.current
	move.w	(RelaxNextCourse).w,d0
	moveq	#0,d1
.current:
	move.w	(sp)+,d2
.done:
	rts

; sub_00F40A (art of the stage after the current one): course in d0 ...
Relax_ArtCourse:
	move.w	($ff060c).l,d0
	tst.b	(RelaxPending).w
	beq.s	.done
	move.w	(RelaxNextCourse).w,d0
.done:
	rts

; ... and stage in d2
Relax_ArtStage:
	moveq	#0,d2
	tst.b	(RelaxPending).w
	bne.s	.done
	move.w	($ff060a).l,d2
	addq.w	#1,d2
.done:
	rts

; loc_00E40A, the road reaches the next stage: the next course starts here
; (its first stage's data, road and roadside objects are then loaded as for
; any stage)
Relax_StageAdvance:
	tst.b	(RelaxPending).w
	bne.s	.course
	clr.b	(RelaxBoundary).w
	addq.w	#1,($ff060a).l
	rts
.course:
	clr.b	(RelaxPending).w
	move.w	(RelaxNextCourse).w,($ff060c).l
	clr.w	($ff060a).l
	clr.w	($ffffc806).w		; off-road obstacles: per stage of the course
	move.w	#$1,($ff052c).l		; stage 1, as at the start of a race (sub_00F448) ...
	move.w	#$1,($ff052e).l
	move.w	#$1,($ff06b4).l		; ... whose first gate does not count a stage
	clr.w	($ffffc7dc).w		; sprite art lists: from the course's first (loc_00C550)
	st	(RelaxBoundary).w
	st	(RelaxHudRedraw).w
	rts

; sub_0045E2, gate at the start of a stage: stage 0 of a course has the
; start or goal gate; a course reached without a restart has a checkpoint
; gate (passing it loads the course's first sprite art)
Relax_GateStage:
	tst.b	(RelaxBoundary).w
	beq.s	.stage
	andi.b	#$fb,ccr		; ne: checkpoint gate
	rts
.stage:
	tst.w	($ff060a).l
	rts
