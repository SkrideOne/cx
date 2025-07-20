CC=gcc
CFLAGS=-std=c11 -Wall -Wextra -O3 -flto -fprofile-use -march=native \
       -falign-functions=64 -Iinclude -Itest -Itest/bpf
TEST=test/test_xdp
LDFLAGS=$(shell pkg-config --libs cmocka)

.PHONY: all test clean format tidy lizard

all: $(TEST)

$(TEST): test/test_xdp.c src/xdp.c include/maps.h
	$(CC) $(CFLAGS) test/test_xdp.c -o $(TEST) $(LDFLAGS)

test: $(TEST)
	./$(TEST)

format:
	clang-format -i $(shell git ls-files '*.c' '*.h')

tidy:
	clang-tidy $(shell git ls-files '*.c') -- -Iinclude -Itest -Itest/bpf

lizard:
	lizard -C 2 -L 30 -T token_count=250 -a 3 $(shell git ls-files '*.c' '*.h')

clean:
	rm -f $(TEST)
