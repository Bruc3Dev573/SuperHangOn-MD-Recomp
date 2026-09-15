; replace $8AC8-$8AD0
; Race: one tick per video frame. The call and the following move are replaced
; together by an absolute call (same size), which leaves the move's flags for
; the beq that follows.
Mode18_Race:
	jsr	(Race60TickWait).l
