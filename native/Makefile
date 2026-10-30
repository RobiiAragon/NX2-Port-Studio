CXX = clang++
CXXFLAGS = -std=c++20 -O2 -Wall -Wextra -Wpedantic
BUILD_DIR ?= ../.nx2-build/native

.PHONY: all clean
all: $(BUILD_DIR)/nx2-ampere-lower

$(BUILD_DIR)/nx2-ampere-lower: ampere_lower.cpp include/common/common_types.h ../third_party/eden-nx2/lower_to_maxwell.h
	mkdir -p "$(BUILD_DIR)"
	$(CXX) $(CXXFLAGS) -Iinclude -I../third_party/eden-nx2 ampere_lower.cpp -o "$@"

clean:
	rm -f "$(BUILD_DIR)/nx2-ampere-lower"
