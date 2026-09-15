; replace $A7EA-$A7F0
; player bike animation phase advances every 4 ticks instead of 2
loc_00A7EA:
	move.w	#$3,($38,a0)
