export BR2_EXTERNAL += $(realpath external)
export BR2_GLOBAL_PATCH_DIR += $(realpath external)

.DEFAULT_GOAL := all

%:
	$(MAKE) -C buildroot $@
