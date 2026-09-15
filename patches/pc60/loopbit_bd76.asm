; replace $BD76-$BD7C
; main loop counter bit: TickShift bits higher, same rate as at 30 Hz
	btst	#3+TickShift,($ffffc726).w
