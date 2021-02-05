# This Makefile compiles an HPVM project.
# It builds HPVM-related dependencies, then the user provided code.
#
# Paths to some dependencies (e.g., HPVM, LLVM) must exist in Makefile.config,
# which can be copied from Makefile.config.example for a start.

CONFIG_FILE := ../include/Makefile.config

ifeq ($(wildcard $(CONFIG_FILE)),)
    $(error $(CONFIG_FILE) not found. See $(CONFIG_FILE).example)
endif
include $(CONFIG_FILE)

# Replace this with the name of your program
EXE_NAME = run_illixr_msckf

# Compiler Flags
LFLAGS += -lm -lrt

ifeq ($(TARGET),)
    TARGET = seq
endif

# Build dirs
SRC_DIR = ov_msckf/src/
BUILD_DIR = build/$(TARGET)
CURRENT_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

EXE = $(EXE_NAME)-$(TARGET)

INCLUDES += -I$(SRC_DIR)
INCLUDES += -I$(LLVM_SRC_ROOT)/include -I../include -I$(HPVM_BUILD_DIR)/include -Iov_core/src -Iov_msckf/src -I/usr/include/eigen3

## BEGIN HPVM MAKEFILE
LANGUAGE=hpvm
SRCDIR_OBJS=VioManager.ll State.ll run_illixr_msckf.ll Propagator.ll TrackBase.ll TrackKLT.ll InertialInitializer.ll Feature.ll TrackAruco.ll UpdaterMSCKF.ll UpdaterSLAM.ll TrackDescriptor.ll Landmark.ll FeatureInitializer.ll TrackSIM.ll UpdaterHelper.ll
OBJS_SRC=VioManager.cpp State.cpp run_illixr_msckf.cpp Propagator.cpp TrackBase.cpp TrackKLT.cpp InertialInitializer.cpp Feature.cpp TrackAruco.cpp UpdaterMSCKF.cpp UpdaterSLAM.cpp TrackDescriptor.cpp Landmark.cpp FeatureInitializer.cpp
HPVM_OBJS=StateHelper.hpvm.ll
APP = $(EXE)
APP_CFLAGS += $(INCLUDES) -ffast-math -O3 -fno-lax-vector-conversions -fno-vectorize -fno-slp-vectorize
APP_CXXFLAGS += $(INCLUDES) -ffast-math -O3 -fno-lax-vector-conversions -fno-vectorize -fno-slp-vectorize
# APP_CFLAGS += $(INCLUDES) -ffast-math -O3 -fno-lax-vector-conversions -fno-vectorize -fno-slp-vectorize -pg
# APP_CXXFLAGS += $(INCLUDES) -ffast-math -O3 -fno-lax-vector-conversions -fno-vectorize -fno-slp-vectorize -pg
APP_LDFLAGS=`pkg-config opencv --libs` -lboost_system -lboost_filesystem -lboost_date_time -lboost_thread -lboost_chrono -lboost_atomic -lboost_exception -L/usr/local/lib/ -L/usr/lib/x86_64-linux-gnu/

CFLAGS=-Wall -fPIC -I./include -ffast-math -O3 -fno-lax-vector-conversions -fno-vectorize #-fno-slp-vectorize
CXXFLAGS=-O3 -std=c++11 -Wall -I./include -fsee -fomit-frame-pointer -fno-signed-zeros -fno-math-errno  -ffast-math -fno-lax-vector-conversions -fno-vectorize #-fno-slp-vectorize -fno-cxx-exceptions# -funroll-loops
# CXXFLAGS=-O3 -std=c++14 -Wall -fPIC -I./include -pg
# CXXFLAGS=-O3 -fprofile-instr-generate -std=c++14 -Wall -fPIC -I./include // not working, no matter with -O3 or -O2
LD_LIBS=-lpthread -pthread
DBG_FLAGS=-Iov_core/src -Iov_msckf/src -I/usr/include/eigen3 -I/usr/include/
OPT_FLAGS=-O3 -Iov_core/src -Iov_msckf/src

OBJS_CFLAGS = $(APP_CFLAGS) $(PLATFORM_CFLAGS)
LDFLAGS= $(APP_LDFLAGS) $(PLATFORM_LDFLAGS)

HPVM_RT_PATH = $(LLVM_BUILD_DIR)/tools/hpvm/projects/hpvm-rt
HPVM_RT_LIB = $(HPVM_RT_PATH)/hpvm-rt.bc

# TESTGEN_OPTFLAGS = -debug -load LLVMGenHPVM.so -genhpvm -globaldce
TESTGEN_OPTFLAGS = -load LLVMGenHPVM.so -genhpvm -globaldce

ifeq ($(TARGET),seq)
  DEVICE = CPU_TARGET
  # HPVM_OPTFLAGS = -debug -load LLVMBuildDFG.so -load LLVMDFG2LLVM_CPU.so -load LLVMClearDFG.so -dfg2llvm-cpu -clearDFG
  HPVM_OPTFLAGS = -load LLVMBuildDFG.so -load LLVMDFG2LLVM_CPU.so -load LLVMClearDFG.so -dfg2llvm-cpu -clearDFG
  # HPVM_OPTFLAGS += -hpvm-timers-cpu
else
  DEVICE = GPU_TARGET
  # HPVM_OPTFLAGS = -debug -load LLVMBuildDFG.so -load LLVMLocalMem.so -load LLVMDFG2LLVM_OpenCL.so -load LLVMDFG2LLVM_CPU.so -load LLVMClearDFG.so -localmem -dfg2llvm-opencl -dfg2llvm-cpu -clearDFG
  HPVM_OPTFLAGS = -load LLVMBuildDFG.so -load LLVMLocalMem.so -load LLVMDFG2LLVM_OpenCL.so -load LLVMDFG2LLVM_CPU.so -load LLVMClearDFG.so -localmem -dfg2llvm-opencl -dfg2llvm-cpu -clearDFG
  HPVM_OPTFLAGS += -hpvm-timers-cpu -hpvm-timers-ptx
endif
  # TESTGEN_OPTFLAGS += -hpvm-timers-gen

CFLAGS += -DDEVICE=$(DEVICE)
CXXFLAGS += -DDEVICE=$(DEVICE)

CXXFLAGS += $(DBG_FLAGS)

# Add BUILDDIR as a prefix to each element of $1
INBUILDDIR=$(addprefix $(BUILD_DIR)/,$(1))

.PRECIOUS: $(BUILD_DIR)/%.ll

OBJS = $(call INBUILDDIR,$(SRCDIR_OBJS))
TEST_OBJS = $(call INBUILDDIR,$(HPVM_OBJS))
KERNEL = $(TEST_OBJS).kernels.ll

ifeq ($(TARGET),gpu)
  KERNEL_OCL = $(TEST_OBJS).kernels.cl
endif

HOST_LINKED = $(BUILD_DIR)/$(APP).linked.ll
HOST = $(BUILD_DIR)/$(APP).host.ll

ifeq ($(OPENCL_PATH),)
FAILSAFE=no_opencl
else 
FAILSAFE=
endif

ifeq ($(DEBUG),1)
	HPVM_OPTFLAGS += -debug
	GENHPVM_OPTFLAGS += -debug
endif

# Targets
default: $(FAILSAFE) $(BUILD_DIR) $(KERNEL_OCL) $(EXE)

clean :
	if [ -f $(EXE) ]; then rm $(EXE); fi
	if [ -f DataflowGraph.dot ]; then rm DataflowGraph.dot*; fi
	if [ -d $(BUILD_DIR) ]; then rm -rf $(BUILD_DIR); fi

$(KERNEL_OCL) : $(KERNEL)
	$(OCLBE) $< -o $@

$(EXE) : $(HOST_LINKED)
	$(CXX) -O3 $(LDFLAGS) $< -o $@

$(HOST_LINKED) : $(HOST) $(OBJS) $(HPVM_RT_LIB)
	$(LLVM_LINK) $^ -S -o $@

$(HOST) $(KERNEL): $(BUILD_DIR)/$(HPVM_OBJS)
	$(OPT) $(HPVM_OPTFLAGS) -S $< -o $(HOST)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.ll : $(SRC_DIR)/%.cpp
	$(CC) $(OBJS_CFLAGS) -emit-llvm -S -o $@ $<

$(BUILD_DIR)/%.ll : $(SRC_DIR)/core/%.cpp
	$(CC) $(OBJS_CFLAGS) -emit-llvm -S -o $@ $<

$(BUILD_DIR)/%.ll : $(SRC_DIR)/state/%.cpp
	$(CC) $(OBJS_CFLAGS) -emit-llvm -S -o $@ $<

$(BUILD_DIR)/%.ll : $(SRC_DIR)/update/%.cpp
	$(CC) $(OBJS_CFLAGS) -emit-llvm -S -o $@ $<

$(BUILD_DIR)/%.ll : $(SRC_DIR)/../../ov_core/src/track/%.cpp
	$(CC) $(OBJS_CFLAGS) -emit-llvm -S -o $@ $<

$(BUILD_DIR)/%.ll : $(SRC_DIR)/../../ov_core/src/init/%.cpp
	$(CC) $(OBJS_CFLAGS) -emit-llvm -S -o $@ $<

$(BUILD_DIR)/%.ll : $(SRC_DIR)/../../ov_core/src/feat/%.cpp
	$(CC) $(OBJS_CFLAGS) -emit-llvm -S -o $@ $<

$(BUILD_DIR)/%.ll : $(SRC_DIR)/../../ov_core/src/types/%.cpp
	$(CC) $(OBJS_CFLAGS) -emit-llvm -S -o $@ $<

$(BUILD_DIR)/StateHelper.ll : $(SRC_DIR)/state/StateHelper.cpp
	$(CC) $(CXXFLAGS) -emit-llvm -S -o $@ $<

$(BUILD_DIR)/StateHelper.hpvm.ll : $(BUILD_DIR)/StateHelper.ll
	$(OPT) $(TESTGEN_OPTFLAGS) $< -S -o $@

## END HPVM MAKEFILE
