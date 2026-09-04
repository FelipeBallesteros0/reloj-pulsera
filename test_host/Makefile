# Tests en host (gcc de WSL) de la lógica pura del firmware. Uso: make test
CC ?= gcc
CFLAGS ?= -Wall -Wextra -O1 -I../firmware/components/reloj_ble/include -I../firmware/components/reloj_notif/include -I../firmware/components/reloj_voz/include
BLE = ../firmware/components/reloj_ble
NOTIF = ../firmware/components/reloj_notif

VOZ = ../firmware/components/reloj_voz

test: test_cts test_ancs test_anillo
	./test_cts
	./test_ancs
	./test_anillo

test_anillo: test_anillo.c $(VOZ)/anillo.c
	$(CC) $(CFLAGS) -o $@ $^

test_cts: test_cts.c $(BLE)/cts_parse.c
	$(CC) $(CFLAGS) -o $@ $^

test_ancs: test_ancs.c $(NOTIF)/ancs_proto.c $(NOTIF)/notif_store.c $(NOTIF)/apps.c
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f test_cts test_ancs test_anillo
.PHONY: test clean
