# CMake composes the native source graph; QEMU owns compilation and linking.
BUILD_DIR ?= $(CURDIR)/work/build/qemu
CONFIG_DIR ?= $(CURDIR)/work/config
QEMU_SOURCE := $(CONFIG_DIR)/source
CONFIGURE_FLAGS ?= --target-list=arm-softmmu,rx-softmmu --enable-tcg --disable-docs --disable-tools --disable-user --disable-guest-agent --disable-werror
NDS_BUILD_DIR ?= $(CURDIR)/work/build/nds
NDS_CONFIG_DIR ?= $(CURDIR)/work/config/nds
NDS_SOURCE := $(NDS_CONFIG_DIR)/source
NDS_CONFIGURE_FLAGS ?= --target=nds32le-elf --enable-sim --disable-gdb --disable-binutils --disable-gas --disable-ld --disable-gprof --disable-nls --disable-werror --with-system-zlib

.PHONY: all prepare configure check bench clean distclean nds ndsprepare ndsconfigure ndscheck

all: $(BUILD_DIR)/config-host.mak
	$(MAKE) -C "$(BUILD_DIR)" all

prepare:
	cmake -S "$(CURDIR)" -B "$(CONFIG_DIR)" -DNATIVE_COMPONENT=qemu

configure $(BUILD_DIR)/config-host.mak: | prepare
	mkdir -p "$(BUILD_DIR)"
	cd "$(BUILD_DIR)" && "$(QEMU_SOURCE)/configure" $(CONFIGURE_FLAGS)

check: all
	$(MAKE) -C "$(BUILD_DIR)" check-nikon

bench: all
	$(MAKE) -C "$(BUILD_DIR)" bench-nikon

nds: $(NDS_BUILD_DIR)/Makefile
	$(MAKE) -C "$(NDS_BUILD_DIR)" all-sim MAKEINFO=true

ndsprepare:
	cmake -S "$(CURDIR)" -B "$(NDS_CONFIG_DIR)" -DNATIVE_COMPONENT=nds

ndsconfigure $(NDS_BUILD_DIR)/Makefile: | ndsprepare
	mkdir -p "$(NDS_BUILD_DIR)"
	cd "$(NDS_BUILD_DIR)" && "$(NDS_SOURCE)/configure" $(NDS_CONFIGURE_FLAGS)

ndscheck: nds
	$(MAKE) -C "$(NDS_BUILD_DIR)/sim/nds32" check-system

clean:
	$(MAKE) -C "$(BUILD_DIR)" clean

distclean:
	$(MAKE) -C "$(BUILD_DIR)" distclean
