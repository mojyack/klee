OUT = ../../out/apps

.PHONY: all

APPS = hlt rpn

$(OUT):
	mkdir -p $@

all: $(APPS:%=$(OUT)/%)

$(OUT)/%: $(wildcard %/*)
	mkdir -p ${DIR} ../out/apps_build/$(notdir $@)
	$(MAKE) -C $(notdir $@) OUT=$(OUT) BUILD=$(OUT)_build/$(notdir $@) INCLUDES="$(INCLUDES)" COMMON_FLAGS="$(COMMON_FLAGS)" LIBRARY="$(LIBRARY)" all
