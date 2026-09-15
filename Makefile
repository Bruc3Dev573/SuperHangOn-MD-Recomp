# Super Hang-On (MD) disassembly build
#
#   make            generate src/shangon.asm from the ROM and build build/shangon.md
#   make verify     build and compare against rom/baserom.md
#   make disasm     regenerate src/shangon.asm from symbols + trace entries

VASM      := tools/bin/vasmm68k_mot
VASMFLAGS := -quiet -no-opt -m68000 -Fbin
PYTHON    := python3

BASEROM   := rom/baserom.md
SRC       := src/shangon.asm
OUT       := build/shangon.md

# patched builds: make patched PATCHES="patches/pc60 patches/relax" NAME=pc60
PATCHES   ?=
NAME      ?= patched
# equates for the overlays: TickShift = log2 of the logic ticks per 30 Hz tick
# (patches/pc60: 1 for 60 ticks per second, 2 for 120)
DEFINES   ?= TickShift=1
PSRC      := build/$(NAME).asm
POUT      := build/$(NAME).md

.PHONY: all verify disasm clean patched

all: $(OUT)

$(OUT): $(SRC) $(BASEROM) | build
	$(VASM) $(VASMFLAGS) -L build/shangon.lst -o $@ $(SRC)

# the disassembly is generated from the ROM (not kept in the repository)
$(SRC): $(BASEROM) symbols.txt analysis/trace_entries.txt tools/disasm/gen.py tools/disasm/analyze.py tools/disasm/m68k.py
	mkdir -p src build
	$(PYTHON) tools/disasm/gen.py $(BASEROM) symbols.txt analysis/trace_entries.txt $(SRC) --report build/disasm_report.txt

build:
	mkdir -p build

disasm: $(BASEROM)
	mkdir -p src build
	$(PYTHON) tools/disasm/gen.py $(BASEROM) symbols.txt analysis/trace_entries.txt $(SRC) --report build/disasm_report.txt

verify: $(OUT)
	@if cmp -s $(BASEROM) $(OUT); then echo "OK: $(OUT) matches $(BASEROM)"; \
	 else echo "MISMATCH"; cmp -l $(BASEROM) $(OUT) | head -20; exit 1; fi

patched: $(BASEROM) | build
	rm -f $(POUT)
	$(PYTHON) tools/disasm/gen.py $(BASEROM) symbols.txt analysis/trace_entries.txt $(PSRC) \
	  $(foreach d,$(PATCHES),--overlay $(d)) $(foreach d,$(DEFINES),--define $(d))
	$(VASM) $(VASMFLAGS) -L build/$(NAME).lst -o $(POUT) $(PSRC)
	$(PYTHON) tools/checksum.py $(POUT)

clean:
	rm -rf build
