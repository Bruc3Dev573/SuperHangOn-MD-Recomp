; replace $AAC4-$AAD2
; PAUSE blinks with a 32 << TickShift tick period (32 at 30 Hz)
	move.b	#(32<<TickShift)-1,($2d,a0)
loc_00AACA:
	move.b	($2d,a0),d0
	cmpi.b	#(16<<TickShift)-1,d0
