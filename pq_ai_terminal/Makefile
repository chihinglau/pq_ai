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
INCLUDES = -Iinclude -Icore -Iai -Icomm -Iutils -Isim -Idrivers

# Sources
CORE_SRCS = core/pq_metrics.c core/event_trigger.c core/wave_freeze.c \
            core/feature_extract.c core/scenario_detect.c
AI_SRCS = ai/iforest_infer.c ai/ae_infer.c ai/cnn1d_infer.c
COMM_SRCS = comm/proto_mqtt.c comm/time_sync.c
UTILS_SRCS = utils/ring_buffer.c utils/json_builder.c utils/sqlite_wrapper.c utils/pq_config.c
SIM_SRCS = sim/sim_main.c sim/hal_sim.c

ALL_SRCS = $(CORE_SRCS) $(AI_SRCS) $(COMM_SRCS) $(UTILS_SRCS) $(SIM_SRCS)
OBJS = $(ALL_SRCS:.c=.o)

# Targets
SIM_TARGET = pq_sim$(EXE_EXT)
APP_TARGET = pq_terminal$(EXE_EXT)

.PHONY: all clean sim app

all: sim app

sim: $(SIM_TARGET)

app: $(APP_TARGET)

$(SIM_TARGET): $(OBJS)
	$(CC) -o $@ $^ $(LIBS)

$(APP_TARGET): app/main.o
	$(CC) -o $@ $< $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

clean:
	-$(RM) $(OBJS) app/main.o
	-$(RM) $(SIM_TARGET) $(APP_TARGET)
	-$(RMDIR) build build-linux

# Cross-compilation for T536 (aarch64)
CROSS_CC = aarch64-linux-gnu-gcc
CROSS_CFLAGS = -std=c99 -Wall -Wextra -O2

linux-arm:
	$(CROSS_CC) $(CROSS_CFLAGS) $(INCLUDES) -c app/main.c -o app/main_arm.o
	$(CROSS_CC) -o pq_terminal_arm$(EXE_EXT) app/main_arm.o $(LIBS)
