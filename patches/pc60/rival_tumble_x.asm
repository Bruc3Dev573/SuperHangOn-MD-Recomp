; replace $D828-$D82A
; crashed rival slides sideways: the 30 Hz x8 divided by TickCount
loc_00D828:
	if TickShift=1
	nop
	else
	asr.w	#TickShift-1,d0
	endif
