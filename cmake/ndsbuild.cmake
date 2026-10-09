# GNU sim owns compilation, its libraries, and the native system fixture.
set(NDS_UPSTREAM "${CMAKE_SOURCE_DIR}/gdb" CACHE PATH "Official GNU NDS32 provider")
native_provider("${NDS_UPSTREAM}")
set(owned sim/nds32/system.c sim/nds32/system.h sim/nds32/boundary.c
  sim/nds32/runner.c sim/nds32/tpj.c sim/nds32/tpj.h sim/nds32/fields.h
  include/gdb/nds32system.h include/hw/nikon/wire.h
  tests/unit/nds/system.c tests/unit/nds/tpj.c tests/unit/nds/budget.c)
foreach(path IN LISTS owned)
  if(EXISTS "${NDS_UPSTREAM}/${path}")
    message(FATAL_ERROR "Project-owned ${path} belongs outside the official gdb/ dependency")
  endif()
endforeach()

# Regenerated inputs stay real files; the official source stays read-only.
set(generated sim/nds32/Makefile.in sim/nds32/configure.ac
  sim/nds32/interp.c sim/nds32/sim-main.h sim/nds32/configure sim/nds32/config.in)
file(GLOB_RECURSE upstream CONFIGURE_DEPENDS LIST_DIRECTORIES true
  RELATIVE "${NDS_UPSTREAM}" "${NDS_UPSTREAM}/*" "${NDS_UPSTREAM}/.*")
foreach(path IN LISTS upstream)
  if(path MATCHES "^\\.git(/|$)" OR path IN_LIST generated)
    continue()
  endif()
  if(IS_DIRECTORY "${NDS_UPSTREAM}/${path}")
    file(MAKE_DIRECTORY "${NATIVE_VIEW}/${path}")
  else()
    get_filename_component(parent "${NATIVE_VIEW}/${path}" DIRECTORY)
    file(MAKE_DIRECTORY "${parent}")
    native_link("${NDS_UPSTREAM}/${path}" "${NATIVE_VIEW}/${path}")
  endif()
endforeach()
foreach(path IN LISTS owned)
  if(NOT EXISTS "${CMAKE_SOURCE_DIR}/${path}")
    message(FATAL_ERROR "Missing native NDS32 source: ${path}")
  endif()
  get_filename_component(parent "${NATIVE_VIEW}/${path}" DIRECTORY)
  file(MAKE_DIRECTORY "${parent}")
  native_link("${CMAKE_SOURCE_DIR}/${path}" "${NATIVE_VIEW}/${path}")
endforeach()
set(NDS_TEST_RULES [==[

# Keep the fixed GNU provider's declaration semantics across the native simulator.
CFLAGS += -std=gnu11

# Compile and link the system fixture with the configured GNU simulator ABI.
SIM_EXTRA_CLEAN += systemtest.o systemtest$(EXEEXT)
.PHONY: check-system
check-system: systemtest$(EXEEXT)
	./systemtest$(EXEEXT)

systemtest.o: $(srcroot)/tests/unit/nds/system.c system.h $(SIM_MAIN_DEPS)
	$(COMPILE) $(srcroot)/tests/unit/nds/system.c
	$(POSTCOMPILE)

systemtest$(EXEEXT): systemtest.o libsim.a $(LIBDEPS)
	$(CC) $(ALL_CFLAGS) $(LDFLAGS) -o $@ systemtest.o libsim.a $(EXTRA_LIBS)

# TPJ uses this same configured library and finite native endpoint.
all: tpj$(EXEEXT)
SIM_EXTRA_CLEAN += runner.o tpj.o tpjtest.o tpj$(EXEEXT) tpjtest$(EXEEXT)
tpj.o: tpj.c tpj.h fields.h system.h $(srcroot)/include/hw/nikon/wire.h $(SIM_MAIN_DEPS)
runner.o: runner.c tpj.h system.h $(srcroot)/include/hw/nikon/wire.h $(SIM_MAIN_DEPS)
tpj$(EXEEXT): runner.o tpj.o libsim.a $(LIBDEPS)
	$(CC) $(ALL_CFLAGS) $(LDFLAGS) -o $@ runner.o tpj.o libsim.a $(EXTRA_LIBS)

.PHONY: check-tpj
check-tpj: tpjtest$(EXEEXT)
	./tpjtest$(EXEEXT)

tpjtest.o: $(srcroot)/tests/unit/nds/tpj.c tpj.h fields.h system.h $(srcroot)/include/hw/nikon/wire.h $(SIM_MAIN_DEPS)
	$(COMPILE) $(srcroot)/tests/unit/nds/tpj.c
	$(POSTCOMPILE)

tpjtest$(EXEEXT): tpjtest.o tpj.o libsim.a $(LIBDEPS)
	$(CC) $(ALL_CFLAGS) $(LDFLAGS) -o $@ tpjtest.o tpj.o libsim.a $(EXTRA_LIBS)

SIM_EXTRA_CLEAN += budgettest.o budgettest$(EXEEXT)
.PHONY: check-budget
check-budget: budgettest$(EXEEXT)
	./budgettest$(EXEEXT)

budgettest.o: $(srcroot)/tests/unit/nds/budget.c system.h $(SIM_MAIN_DEPS)
	$(COMPILE) $(srcroot)/tests/unit/nds/budget.c
	$(POSTCOMPILE)

budgettest$(EXEEXT): budgettest.o libsim.a $(LIBDEPS)
	$(CC) $(ALL_CFLAGS) $(LDFLAGS) -o $@ budgettest.o libsim.a $(EXTRA_LIBS)
]==])

find_program(AUTOCONF autoconf REQUIRED)
find_program(AUTOHEADER autoheader REQUIRED)
execute_process(COMMAND "${AUTOCONF}" --version OUTPUT_VARIABLE version
  RESULT_VARIABLE status)
if(NOT status STREQUAL "0" OR NOT version MATCHES "Autoconf\\) ([0-9]+\\.[0-9]+)")
  message(FATAL_ERROR "Cannot determine the native Autoconf version")
endif()
# The official override.m4 documents this per-input toolchain version override.
set(AUTOCONF_VERSION "${CMAKE_MATCH_1}")
function(native_file path content)
  if(path STREQUAL "sim/nds32/Makefile.in")
    string(APPEND content "${NDS_TEST_RULES}")
  elseif(path STREQUAL "sim/nds32/configure.ac")
    set(content "m4_define([_GCC_AUTOCONF_VERSION], [${AUTOCONF_VERSION}])\n${content}")
  endif()
  native_write("${NATIVE_VIEW}/${path}" "${content}")
endfunction()
include("${CMAKE_CURRENT_LIST_DIR}/nds32.cmake")
set(directory "${NATIVE_VIEW}/sim/nds32")
if(NOT EXISTS "${directory}/configure" OR
    "${directory}/configure.ac" IS_NEWER_THAN "${directory}/configure" OR
    NOT EXISTS "${directory}/config.in")
  foreach(tool "${AUTOCONF}" "${AUTOHEADER}")
    execute_process(COMMAND "${tool}" WORKING_DIRECTORY "${directory}"
      RESULT_VARIABLE status)
    if(NOT status STREQUAL "0")
      message(FATAL_ERROR "Native NDS32 configuration generation failed: ${tool}")
    endif()
  endforeach()
endif()
