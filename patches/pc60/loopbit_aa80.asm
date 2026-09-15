; replace $AA80-$AA86
; main loop counter bit: one bit higher, same rate at 60 ticks per second
loc_00AA80:
	btst	#$5,($ffffc726).w
