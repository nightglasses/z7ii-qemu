# CMake composes the native source graph; QEMU owns compilation and linking.
BUILD_DIR ?= $(CURDIR)/work/build/qemu
CONFIG_DIR ?= $(CURDIR)/work/config
QEMU_SOURCE := $(CONFIG_DIR)/source
CONFIGURE_FLAGS ?= --target-list=arm-softmmu,rx-softmmu --enable-tcg --disable-docs --disable-tools --disable-user --disable-guest-agent --disable-werror

.PHONY: all configure check bench clean distclean

all: $(BUILD_DIR)/config-host.mak
	$(MAKE) -C "$(BUILD_DIR)" all

$(BUILD_DIR)/config-host.mak:
	$(MAKE) configure

configure:
	cmake -S "$(CURDIR)" -B "$(CONFIG_DIR)"
	mkdir -p "$(BUILD_DIR)"
	cd "$(BUILD_DIR)" && "$(QEMU_SOURCE)/configure" $(CONFIGURE_FLAGS)

check: all
	$(MAKE) -C "$(BUILD_DIR)" check-nikon

bench: all
	$(MAKE) -C "$(BUILD_DIR)" bench-nikon

clean:
	$(MAKE) -C "$(BUILD_DIR)" clean

distclean:
	$(MAKE) -C "$(BUILD_DIR)" distclean
