CXX = clang++
CXXFLAGS = -std=c++20 -O2 -Wall -Wextra -Wpedantic
BUILD_DIR ?= ../.nx2-build/native
UNAME_S := $(shell uname -s)
DYLIB_SUFFIX := $(if $(filter Darwin,$(UNAME_S)),dylib,so)
DYLIB_FLAG := $(if $(filter Darwin,$(UNAME_S)),-dynamiclib,-shared)

.PHONY: all clean
all: $(BUILD_DIR)/nx2-ampere-lower $(BUILD_DIR)/libnx2_zbic.$(DYLIB_SUFFIX)

$(BUILD_DIR)/libnx2_zbic.$(DYLIB_SUFFIX): zbic_bridge.c ../third_party/zbic-zstd/zstd.c ../third_party/zbic-zstd/zstd.h ../third_party/zbic-zstd/zstd_errors.h
	mkdir -p "$(BUILD_DIR)"
	$(CC) -std=c11 -O2 -fPIC -DZSTD_ZBIC_SUPPORT=1 -I../third_party/zbic-zstd $(DYLIB_FLAG) zbic_bridge.c ../third_party/zbic-zstd/zstd.c -pthread -o "$@"

$(BUILD_DIR)/nx2-ampere-lower: ampere_lower.cpp include/common/common_types.h ../third_party/eden-nx2/lower_to_maxwell.h
	mkdir -p "$(BUILD_DIR)"
	$(CXX) $(CXXFLAGS) -Iinclude -I../third_party/eden-nx2 ampere_lower.cpp -o "$@"

clean:
	rm -f "$(BUILD_DIR)/nx2-ampere-lower"
	rm -f "$(BUILD_DIR)/libnx2_zbic.$(DYLIB_SUFFIX)"
