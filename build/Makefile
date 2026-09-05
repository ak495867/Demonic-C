CXX ?= g++
CC ?= gcc
AR ?= ar
CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra -Werror
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Werror
LDFLAGS ?=
SOURCE := ../compiler/dmc.cpp
RUNTIME := ../runtime
TEST_DIR := ../test-files
BIN := ../bin

all: $(BIN)/dmc-native $(BIN)/libdmc_runtime.a

$(BIN)/dmc-native: $(SOURCE)
	mkdir -p $(BIN)
	$(CXX) $(CXXFLAGS) $(SOURCE) -o $@ $(LDFLAGS)

$(RUNTIME)/dmc_runtime.o: $(RUNTIME)/dmc_runtime.c $(RUNTIME)/dmc_runtime.h
	$(CC) $(CFLAGS) -I$(RUNTIME) -c $(RUNTIME)/dmc_runtime.c -o $@

$(BIN)/libdmc_runtime.a: $(RUNTIME)/dmc_runtime.o
	mkdir -p $(BIN)
	$(AR) rcs $@ $<

test: $(BIN)/dmc-native
	../dmc test $(TEST_DIR)

smoke: $(BIN)/libdmc_runtime.a $(RUNTIME)/smoke/dmc_runtime_smoke.c
	$(CC) $(CFLAGS) -w -I$(RUNTIME) $(RUNTIME)/smoke/dmc_runtime_smoke.c $(BIN)/libdmc_runtime.a -o $(BIN)/runtime_smoke $(LDFLAGS)
	$(BIN)/runtime_smoke

stage2: $(BIN)/dmc-native
	$(BIN)/dmc-native $(TEST_DIR)/emit.dmc -o $(TEST_DIR)/_stage_emit.c
	$(CC) -std=c11 -w $(TEST_DIR)/_stage_emit.c -o $(TEST_DIR)/_stage_emit $(LDFLAGS)
	cd $(TEST_DIR) && ./_stage_emit selflex.dmc selflex_stage_out.c
	$(CC) -std=c11 -w $(TEST_DIR)/selflex_stage_out.c -o $(TEST_DIR)/selflex_stage $(LDFLAGS)
	cd $(TEST_DIR) && ./selflex_stage

clean:
	rm -f $(BIN)/dmc-native $(BIN)/libdmc_runtime.a $(RUNTIME)/dmc_runtime.o $(BIN)/runtime_smoke
	rm -f $(TEST_DIR)/_stage_emit.c $(TEST_DIR)/_stage_emit $(TEST_DIR)/selflex_stage_out.c $(TEST_DIR)/selflex_stage

.PHONY: all test smoke stage2 clean
