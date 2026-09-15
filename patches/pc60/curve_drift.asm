; replace $EA26-$EA30
; drift outwards in curves: the 30 Hz step (the original doubled it) divided
; among TickCount ticks
loc_00EA26:
	jsr	(Curve60Drift).l
	nop
	nop
