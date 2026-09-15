; replace $AA80-$AA86
; main loop counter bit: TickShift bits higher, same rate as at 30 Hz
loc_00AA80:
	btst	#4+TickShift,($ffffc726).w
