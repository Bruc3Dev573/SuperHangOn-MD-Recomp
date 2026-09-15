; replace $B486-$B48E
; course end: the goal banner recedes $80 >> TickShift per tick ($80 at 30 Hz)
loc_00B486:
	subi.w	#$80>>TickShift,($ff06bc).l
