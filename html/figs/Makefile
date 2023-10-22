svg := document_repository.svg

lsvg_svg := $(foreach prefix,$(patsubst %.svg,%,$(svg)),$(foreach i,$(shell seq 1 $(shell ./get_layer.py num $(prefix).svg)),layers/$(prefix)_$(i).svg))

all : $(lsvg_svg)

define lsvg_svg_rule
# layered svg -> tex+pdf
layers/$(1)_$(2).svg : layers/%_$(2).svg : %.svg
	mkdir -p $(dir layers/$(1)_$(2).svg)
	inkscape -D -z --file=$$< --export-plain-svg=$$@ --export-id=$(3) --export-id-only # layer=$(2)
endef

$(foreach f,$(svg),\
	$(foreach i,$(shell seq 1 $(shell ./get_layer.py num $(f))),\
	$(eval $(call lsvg_svg_rule,$(patsubst %.svg,%,$(f)),$(i),$(shell ./get_layer.py id $(f) $(i))))))
