; replace $9918-$991E
; main loop counter bit: TickShift bits higher, same rate as at 30 Hz
	btst	#0+TickShift,($ffffc726).w
