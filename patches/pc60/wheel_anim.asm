; replace $A7EA-$A7F0
; player bike animation phase advances every 2 << TickShift ticks (2 at 30 Hz)
loc_00A7EA:
	move.w	#(2<<TickShift)-1,($38,a0)
