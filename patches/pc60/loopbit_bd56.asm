; replace $BD56-$BD5C
; main loop counter bit: TickShift bits higher, same rate as at 30 Hz
	btst	#1+TickShift,($ffffc726).w
