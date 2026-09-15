; replace $D6CE-$D6D4
; position along the road relative to the player: the 30 Hz advance divided
; among TickCount ticks
loc_00D6CE:
	jsr	(Rival60Advance).l
