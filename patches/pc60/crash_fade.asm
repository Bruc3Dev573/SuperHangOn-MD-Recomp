; replace $A988-$A98C
; A988 sequence counter: 1 per tick instead of 2
loc_00A988:
	addq.b	#1,($2d,a0)
