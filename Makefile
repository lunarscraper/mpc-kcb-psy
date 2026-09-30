CC     ?= cc
CFLAGS ?= -O2 -std=c99 -Wall -Wextra -D_DEFAULT_SOURCE
BUILD  := build

.PHONY: test clean
test: $(BUILD)/kcb_test
	./$(BUILD)/kcb_test $(BUILD)

$(BUILD)/kcb_test: src/kcb_engine.c src/kcb_engine.h test/kcb_test.c | $(BUILD)
	$(CC) $(CFLAGS) -Isrc src/kcb_engine.c test/kcb_test.c -o $@ -lm

$(BUILD):
	mkdir -p $(BUILD)

clean:
	rm -rf $(BUILD)
