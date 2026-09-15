; replace $A2E0-$A2E6
; full-throttle pose timer: the 30 Hz step of 2 divided among TickCount ticks
	jsr	(Pose60Timer).l
