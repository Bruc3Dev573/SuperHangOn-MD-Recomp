; replace $8B50-$8B58
; the original waited here for one VBlank before building the sprite list and
; the road line tables; with one tick per frame the uploads are gated by
; Sched60Ready instead
loc_008B50:
	nop
	nop
	nop
	nop
