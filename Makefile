CC=gcc
CFLAGS_BASE=-std=c11 -Wall -Wextra -O3 -march=native -falign-functions=64 \
            -Iinclude -Itest -Itest/bpf
CFLAGS_PROFILE=-flto -fprofile-use
CFLAGS=$(CFLAGS_BASE)
TEST=test/test_xdp
LDFLAGS=$(shell pkg-config --libs cmocka)

.PHONY: all test clean format tidy lizard profile-gen profile-use

all: $(TEST)

$(TEST): test/test_xdp.c src/xdp.c include/maps.h
	$(CC) $(CFLAGS) test/test_xdp.c -o $(TEST) $(LDFLAGS)

test: $(TEST)
	./$(TEST)

# Profile-guided optimization targets
profile-gen: CFLAGS=$(CFLAGS_BASE) -fprofile-generate
profile-gen: clean $(TEST)
	./$(TEST)
	@echo "Profile data generated. Now run 'make profile-use' to build optimized version"

profile-use: CFLAGS=$(CFLAGS_BASE) $(CFLAGS_PROFILE)
profile-use: $(TEST)

format:
	clang-format -i $(shell git ls-files '*.c' '*.h')

tidy:
	clang-tidy $(shell git ls-files '*.c') -- -Iinclude -Itest -Itest/bpf

lizard:
	lizard -C 2 -L 30 -T token_count=250 -a 3 $(shell git ls-files '*.c' '*.h')

clean:
	rm -f $(TEST) *.gcda *.gcno
