; replace $A2E0-$A2E4
; full-throttle pose timer: 1 per tick instead of 2
	subq.w	#1,($3a,a0)
