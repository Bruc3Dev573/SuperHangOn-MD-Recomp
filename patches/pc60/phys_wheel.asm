; replace $9EE2-$9EEA
; $FF0662 advances by the 30 Hz 2 x speed/34 divided among TickCount ticks
loc_009EE2:
	jsr	(Phys60Wheel).l
	nop
