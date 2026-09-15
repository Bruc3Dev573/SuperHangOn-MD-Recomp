; replace $9D28-$9D2E
; speed update: the 30 Hz acceleration once every TickCount ticks
loc_009D28:
	jsr	(Phys60AddAccel).l
