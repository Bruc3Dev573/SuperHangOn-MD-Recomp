; replace $BD56-$BD5C
; main loop counter bit: one bit higher, same rate at 60 ticks per second
	btst	#$2,($ffffc726).w
