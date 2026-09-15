; replace $BC48-$BC4E
; main loop counter bit: TickShift bits higher, same rate as at 30 Hz
	btst	#3+TickShift,($ffffc726).w
