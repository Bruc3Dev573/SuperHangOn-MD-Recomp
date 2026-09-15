; replace $EBA0-$EBA6
; background follows the road heading: the original 1/128 of the gap per 30 Hz
; tick divided by TickCount per tick (same time constant)
	jsr	(Bg60Follow).l
