# Tests en host (gcc de WSL) de la lógica pura del firmware. Uso: make test
CC ?= gcc
CFLAGS ?= -Wall -Wextra -O1 -I../firmware/components/reloj_ble/include -I../firmware/components/reloj_notif/include
BLE = ../firmware/components/reloj_ble
NOTIF = ../firmware/components/reloj_notif

test: test_cts test_ancs
	./test_cts
	./test_ancs

test_cts: test_cts.c $(BLE)/cts_parse.c
	$(CC) $(CFLAGS) -o $@ $^

test_ancs: test_ancs.c $(NOTIF)/ancs_proto.c $(NOTIF)/notif_store.c $(NOTIF)/apps.c
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f test_cts test_ancs
.PHONY: test clean
