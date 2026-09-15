; replace $D5B8-$D5BA
; lane change: 4 >> TickShift per tick (4 at 30 Hz)
loc_00D5B8:
	moveq	#4>>TickShift,d0
