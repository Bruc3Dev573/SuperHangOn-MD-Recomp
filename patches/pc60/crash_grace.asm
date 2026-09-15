; replace $AE26-$AE2C
; after a crash, input stays blocked for 4 ticks (2 at 30 Hz)
	move.w	#$4,($ffffc7fe).w
