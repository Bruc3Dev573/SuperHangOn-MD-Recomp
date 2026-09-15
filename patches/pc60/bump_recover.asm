; replace $A3D6-$A3E0
; recovery after a bump: push decays 1 per tick and moves the bike half as far
sub_00A3D6:
	moveq	#-1,d0
	move.w	(dat_009F94).l,d1
	nop
