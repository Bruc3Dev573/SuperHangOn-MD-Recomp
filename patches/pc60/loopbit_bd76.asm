; replace $BD76-$BD7C
; main loop counter bit: one bit higher, same rate at 60 ticks per second
	btst	#$4,($ffffc726).w
