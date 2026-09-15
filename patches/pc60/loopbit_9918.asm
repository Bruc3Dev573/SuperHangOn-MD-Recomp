; replace $9918-$991E
; main loop counter bit: one bit higher, same rate at 60 ticks per second
	btst	#$1,($ffffc726).w
