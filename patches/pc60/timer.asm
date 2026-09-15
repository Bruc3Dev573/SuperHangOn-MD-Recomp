; replace $F546-$F558
; race timers count 30 << TickShift ticks per second (30 at 30 Hz)
	move.b	#30<<TickShift,($1,a0)
	move.b	#30<<TickShift,($2,a0)
	move.b	#30<<TickShift,($ffffc88c).w
