; replace $8A24-$8A2A
; Attract mode used to leave the sound-command gate set, so PlaySound was a
; no-op for the whole demo. Keep the normal command path enabled.
	clr.b	($ffffc754).w
	nop
