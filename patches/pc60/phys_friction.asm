; replace $9B80-$9B88
; sliding: speed x (2^k - 1) / 2^k per tick with k = 4 + TickShift, close to
; the 30 Hz x 30/32 spread over TickCount ticks (31/32 for 60 ticks per second,
; 63/64 for 120)
	asl.w	#4+TickShift,d0
	nop
	sub.w	d1,d0
	asr.w	#4+TickShift,d0
