# PQ AI Terminal - Pure Makefile (no CMake required)
# Supports: Windows MinGW, Linux GCC, cross-compilation

# Detect OS
ifeq ($(OS),Windows_NT)
    TARGET_OS = windows
    EXE_EXT = .exe
    LIBS = -lwinmm -lws2_32
    RM = del /Q
    RMDIR = rmdir /S /Q
    PLATFORM_DEF = -DPLATFORM_WINDOWS
else
    TARGET_OS = linux
    EXE_EXT =
    LIBS = -lm -lpthread
    RM = rm -f
    RMDIR = rm -rf
    PLATFORM_DEF = -DPLATFORM_LINUX
endif

# Compiler settings
CC = gcc
CFLAGS = -std=c99 -Wall -Wextra -O2 $(PLATFORM_DEF)
INCLUDES = -Iinclude -Icore -Iai -Icomm -Iutils -Isim -Idrivers -Itests

# Sources
CORE_SRCS = core/pq_metrics.c core/event_trigger.c core/wave_freeze.c \
            core/feature_extract.c core/scenario_detect.c
AI_SRCS = ai/iforest_infer.c ai/ae_infer.c ai/cnn1d_infer.c ai/ai_rpc.c
COMM_SRCS = comm/proto_mqtt.c comm/time_sync.c comm/usb_ecm.c
UTILS_SRCS = utils/ring_buffer.c utils/json_builder.c utils/sqlite_wrapper.c utils/pq_config.c
DRV_SRCS = drivers/wave_sampler_hal.c drivers/hal_platform.c
SIM_SRCS = sim/sim_main.c sim/hal_sim.c sim/compute_module_sim.c

# Library sources (shared by all targets with main)
LIB_SRCS = $(CORE_SRCS) $(AI_SRCS) $(COMM_SRCS) $(UTILS_SRCS) $(DRV_SRCS)
LIB_OBJS = $(LIB_SRCS:.c=.o)

ALL_SRCS = $(LIB_SRCS) $(SIM_SRCS)
OBJS = $(ALL_SRCS:.c=.o)

# Unit test sources (excludes sim_main.c, hal_sim.c to avoid duplicate main)
TEST_SRCS = $(CORE_SRCS) $(AI_SRCS) $(COMM_SRCS) $(UTILS_SRCS) $(DRV_SRCS) \
            sim/compute_module_sim.c tests/test_usb_ecm_ai_rpc.c
TEST_OBJS = $(TEST_SRCS:.c=.o)

# Targets
SIM_TARGET = pq_sim$(EXE_EXT)
APP_TARGET = pq_terminal$(EXE_EXT)
ACQ_TARGET = pq_acq_wave$(EXE_EXT)
EXPORT_TARGET = wave_export$(EXE_EXT)
TEST_TARGET = test_usb_ecm_ai_rpc$(EXE_EXT)

.PHONY: all clean sim app acq export test

all: sim app acq export

sim: $(SIM_TARGET)

app: $(APP_TARGET)

acq: $(ACQ_TARGET)

export: $(EXPORT_TARGET)

test: $(TEST_TARGET)

$(SIM_TARGET): $(OBJS)
	$(CC) -o $@ $^ $(LIBS)

$(APP_TARGET): app/main.o $(LIB_OBJS)
	$(CC) -o $@ $^ $(LIBS)

$(ACQ_TARGET): app/acq_wave_main.o $(LIB_OBJS)
	$(CC) -o $@ $^ $(LIBS)

$(EXPORT_TARGET): app/wave_export.o $(LIB_OBJS)
	$(CC) -o $@ $^ $(LIBS)

$(TEST_TARGET): $(TEST_OBJS)
	$(CC) -o $@ $^ $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

clean:
	-$(RM) $(OBJS) app/main.o app/acq_wave_main.o app/wave_export.o tests/test_usb_ecm_ai_rpc.o
	-$(RM) $(SIM_TARGET) $(APP_TARGET) $(ACQ_TARGET) $(EXPORT_TARGET) $(TEST_TARGET)
	-$(RMDIR) build build-linux

# Cross-compilation for T536 (aarch64)
CROSS_CC = /opt/toolchain/gcc-linaro-11.3.1-2022.06-x86_64_aarch64-linux-gnu/bin/aarch64-linux-gnu-gcc
CROSS_STRIP = /opt/toolchain/gcc-linaro-11.3.1-2022.06-x86_64_aarch64-linux-gnu/bin/aarch64-linux-gnu-strip
CROSS_CFLAGS = -std=c99 -Wall -Wextra -O2 -DPLATFORM_LINUX

linux-arm:
	@echo "Cross-compiling for T536 (aarch64)..."
	@echo "Using compiler: $(CROSS_CC)"
	@for src in $(LIB_SRCS); do \
		obj=$${src%.c}_arm.o; \
		echo "  CC $$src"; \
		$(CROSS_CC) $(CROSS_CFLAGS) $(INCLUDES) -c $$src -o $$obj || exit 1; \
	done
	@for src in app/main.c app/acq_wave_main.c app/wave_export.c; do \
		obj=$${src%.c}_arm.o; \
		echo "  CC $$src"; \
		$(CROSS_CC) $(CROSS_CFLAGS) $(INCLUDES) -c $$src -o $$obj || exit 1; \
	done
	$(CROSS_CC) -o pq_terminal_arm app/main_arm.o $(LIB_SRCS:.c=_arm.o) -lm -lpthread
	$(CROSS_CC) -o pq_acq_wave_arm app/acq_wave_main_arm.o $(LIB_SRCS:.c=_arm.o) -lm -lpthread
	$(CROSS_CC) -o wave_export_arm app/wave_export_arm.o $(LIB_SRCS:.c=_arm.o) -lm -lpthread
	$(CROSS_STRIP) pq_terminal_arm
	$(CROSS_STRIP) pq_acq_wave_arm
	$(CROSS_STRIP) wave_export_arm
	@echo ""
	@echo "Cross-compilation complete!"
	@echo "  pq_terminal_arm:  $$(file pq_terminal_arm | grep -o 'ARM.*')"
	@echo "  pq_acq_wave_arm: $$(file pq_acq_wave_arm | grep -o 'ARM.*')"
	@echo "  wave_export_arm:  $$(file wave_export_arm | grep -o 'ARM.*')"
	@ls -la pq_terminal_arm pq_acq_wave_arm wave_export_arm
