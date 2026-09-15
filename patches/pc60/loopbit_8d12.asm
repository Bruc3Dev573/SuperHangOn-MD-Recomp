; replace $8D12-$8D18
; main loop counter bit: one bit higher, same rate at 60 ticks per second
	btst	#$2,($ffffc726).w
