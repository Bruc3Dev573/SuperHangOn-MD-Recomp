; replace $E51C-$E522
; goal palette effect: step every 4 << TickShift ticks (4 at 30 Hz)
	move.w	#4<<TickShift,($ffffc74e).w
