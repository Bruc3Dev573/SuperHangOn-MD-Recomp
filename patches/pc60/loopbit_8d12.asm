; replace $8D12-$8D18
; main loop counter bit: TickShift bits higher, same rate as at 30 Hz
	btst	#1+TickShift,($ffffc726).w
