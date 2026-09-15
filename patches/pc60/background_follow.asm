; replace $EBA0-$EBA2
; background follows the road heading: 1/256 of the gap per tick instead of
; 1/128 (same time constant at twice the rate)
	asr.l	#8,d0
