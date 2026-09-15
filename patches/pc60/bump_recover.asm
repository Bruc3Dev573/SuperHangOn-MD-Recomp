; replace $A3D6-$A3E0
; recovery after a bump: the push decay and the bike's move of a 30 Hz tick,
; divided among TickCount ticks
sub_00A3D6:
	jsr	(Bump60Recover).l
	nop
	nop
