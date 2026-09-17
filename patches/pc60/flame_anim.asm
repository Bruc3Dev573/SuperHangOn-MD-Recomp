; replace $A5C6-$A5CA
; turbo flames: the phase ($3e,a0) chooses one of two art blocks with its bit
; 3, so the original alternates them once per 30 Hz tick; the step is divided
; among TickCount ticks (at 120 ticks per second the flames flickered at
; 60 Hz, which the eye sees as a steady flame)
loc_00A5C6:
	addq.w	#8>>TickShift,($3e,a0)
