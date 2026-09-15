; replace $AE26-$AE2C
; after a crash, input stays blocked for 2 << TickShift ticks (2 at 30 Hz)
	move.w	#2<<TickShift,($ffffc7fe).w
