; replace $9D28-$9D2E
; speed update: half of the 30 Hz acceleration per tick
loc_009D28:
	jsr	(Phys60AddAccel).l
