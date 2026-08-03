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
SIM_SRCS = sim/sim_main.c sim/hal_sim.c sim/compute_module_sim.c

ALL_SRCS = $(CORE_SRCS) $(AI_SRCS) $(COMM_SRCS) $(UTILS_SRCS) $(SIM_SRCS)
OBJS = $(ALL_SRCS:.c=.o)

# Unit test sources (excludes sim_main.c, hal_sim.c to avoid duplicate main)
TEST_SRCS = $(CORE_SRCS) $(AI_SRCS) $(COMM_SRCS) $(UTILS_SRCS) \
            sim/compute_module_sim.c tests/test_usb_ecm_ai_rpc.c
TEST_OBJS = $(TEST_SRCS:.c=.o)

# Targets
SIM_TARGET = pq_sim$(EXE_EXT)
APP_TARGET = pq_terminal$(EXE_EXT)
TEST_TARGET = test_usb_ecm_ai_rpc$(EXE_EXT)

.PHONY: all clean sim app test

all: sim app

sim: $(SIM_TARGET)

app: $(APP_TARGET)

test: $(TEST_TARGET)

$(SIM_TARGET): $(OBJS)
	$(CC) -o $@ $^ $(LIBS)

$(APP_TARGET): app/main.o
	$(CC) -o $@ $< $(LIBS)

$(TEST_TARGET): $(TEST_OBJS)
	$(CC) -o $@ $^ $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

clean:
	-$(RM) $(OBJS) app/main.o tests/test_usb_ecm_ai_rpc.o
	-$(RM) $(SIM_TARGET) $(APP_TARGET) $(TEST_TARGET)
	-$(RMDIR) build build-linux

# Cross-compilation for T536 (aarch64)
CROSS_CC = aarch64-linux-gnu-gcc
CROSS_CFLAGS = -std=c99 -Wall -Wextra -O2

linux-arm:
	$(CROSS_CC) $(CROSS_CFLAGS) $(INCLUDES) -c app/main.c -o app/main_arm.o
	$(CROSS_CC) -o pq_terminal_arm$(EXE_EXT) app/main_arm.o $(LIBS)
