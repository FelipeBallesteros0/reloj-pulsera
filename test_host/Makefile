# Tests en host (gcc de WSL) de la lógica pura del firmware. Uso: make test
CC ?= gcc
CFLAGS ?= -Wall -Wextra -O1 -I../firmware/components/reloj_ble/include
BLE = ../firmware/components/reloj_ble

test: test_cts
	./test_cts

test_cts: test_cts.c $(BLE)/cts_parse.c
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f test_cts
.PHONY: test clean
