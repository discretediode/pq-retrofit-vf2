# Paths to the two libraries (cloned next to the repo by the guide; this Makefile lives in hailo_prototype/).
DILITHIUM ?= ../../dilithium/ref
MBEDTLS   ?= ../../mbedtls-3.6.2

CC     ?= cc
CFLAGS += -O3 -Wall -Wextra -DDILITHIUM_MODE=2 -Isrc -I$(DILITHIUM) -I$(MBEDTLS)/include
LIBS    = -L$(MBEDTLS)/library -lmbedcrypto -lpthread -lm

# On the board: `make MARCH=rv64gc_zba_zbb` gives Keccak 1-instruction rotates.
ifdef MARCH
  CFLAGS += -march=$(MARCH)
endif

MLDSA = $(addprefix $(DILITHIUM)/, sign.c packing.c polyvec.c poly.c ntt.c \
          reduce.c rounding.c fips202.c symmetric-shake.c)
ACCEL = src/accel.c

# `make HAILO=1` adds the Hailo backend (only on the board, needs HailoRT).
ifdef HAILO
  CFLAGS += -DUSE_HAILO
  ACCEL  += src/accel_hailo.c
  LIBS   += -lhailort
endif

all: bin/keytool bin/bench_ref bin/bench_accel bin/test_accel

bin:
	mkdir -p bin

# Unmodified ML-DSA (normal CPU path).
bin/bench_ref: src/bench.c $(MLDSA) | bin
	$(CC) $(CFLAGS) -o $@ src/bench.c $(MLDSA) $(DILITHIUM)/randombytes.c $(LIBS)

# ML-DSA with the accelerator hook (-DACCEL switches the patched lines on).
bin/bench_accel: src/bench.c $(ACCEL) $(MLDSA) | bin
	$(CC) $(CFLAGS) -DACCEL -o $@ src/bench.c $(ACCEL) $(MLDSA) $(DILITHIUM)/randombytes.c $(LIBS)

bin/test_accel: src/test_accel.c $(ACCEL) $(MLDSA) | bin
	$(CC) $(CFLAGS) -DACCEL -o $@ src/test_accel.c $(ACCEL) $(MLDSA) $(DILITHIUM)/randombytes.c $(LIBS)

bin/keytool: src/keytool.c $(MLDSA) | bin
	$(CC) $(CFLAGS) -o $@ src/keytool.c $(MLDSA) $(DILITHIUM)/randombytes.c

# Proof the hook is correct: the official test vectors must give the
# identical output with and without -DACCEL (CPU backend).
check: | bin
	$(CC) $(CFLAGS) -o bin/vectors_ref $(DILITHIUM)/test/test_vectors.c $(MLDSA)
	$(CC) $(CFLAGS) -DACCEL -o bin/vectors_accel $(DILITHIUM)/test/test_vectors.c $(ACCEL) $(MLDSA) -lm
	./bin/vectors_ref > bin/vectors_ref.txt
	./bin/vectors_accel > bin/vectors_accel.txt
	cmp bin/vectors_ref.txt bin/vectors_accel.txt && echo "CHECK PASSED: patched ML-DSA matches reference"

clean:
	rm -rf bin

.PHONY: all check clean
