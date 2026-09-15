; replace $F546-$F558
; race timers count 60 ticks per second instead of 30
	move.b	#$3c,($1,a0)
	move.b	#$3c,($2,a0)
	move.b	#$3c,($ffffc88c).w
