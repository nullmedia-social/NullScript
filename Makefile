CC      = gcc
CFLAGS  = -Wall -Wextra -std=c11 -Iinclude -g
LDFLAGS = -lm

SRCS = src/main.c src/token.c src/lexer.c src/ast.c src/parser.c \
       src/value.c src/builtins.c src/interp.c

TARGET = nullscript

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) $(SRCS) -o $(TARGET) $(LDFLAGS)

clean:
	rm -f $(TARGET) *.nsx

test: $(TARGET)
	@echo "--- running test_hello.ns ---"
	./$(TARGET) x tests/test_hello.ns
	@echo ""
	@echo "--- running test_types.ns ---"
	./$(TARGET) x tests/test_types.ns
	@echo ""
	@echo "--- running test_funcs.ns ---"
	./$(TARGET) x tests/test_funcs.ns
	@echo ""
	@echo "--- running test_loops.ns ---"
	./$(TARGET) x tests/test_loops.ns
	@echo ""
	@echo "--- running test_builtins.ns ---"
	./$(TARGET) x tests/test_builtins.ns
	@echo ""
	@echo "--- all tests done ---"

.PHONY: all clean test
