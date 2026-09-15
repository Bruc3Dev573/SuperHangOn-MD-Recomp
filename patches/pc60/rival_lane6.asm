; replace $D5F2-$D5F8
; lane change towards the target lane: the 30 Hz step of 6 divided among
; TickCount ticks
loc_00D5F2:
	jsr	(Rival60Lane6).l
