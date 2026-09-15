; replace $AAC4-$AAD2
; PAUSE blinks with a 64-tick period (32 at 30 Hz)
	move.b	#$3f,($2d,a0)
loc_00AACA:
	move.b	($2d,a0),d0
	cmpi.b	#$1f,d0
