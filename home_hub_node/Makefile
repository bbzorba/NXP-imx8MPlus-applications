# Cross-compiles home_hub_node for the i.MX 8M Plus (aarch64 Linux).
#
#   make                 build with Ubuntu's aarch64-linux-gnu-gcc
#   make CROSS_COMPILE=  native build (for testing on the host)
#   make deploy          copy to the board and (re)start the service
#
# With a Yocto SDK, source its environment-setup-* script first and run
# `make CC="$CC"` so the SDK's compiler and sysroot are used.

CROSS_COMPILE ?= aarch64-linux-gnu-
# A CC=... given on the command line overrides this
CC      := $(CROSS_COMPILE)gcc
CFLAGS  ?= -O2 -Wall -Wextra
BOARD   ?= root@192.168.178.171
# Reflashed images get new host keys, so don't stop on a changed/unknown key
SSH_OPTS ?= -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR

BIN := build/home_hub_node

all: $(BIN)

$(BIN): src/home_hub_node.c
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ $<

deploy: $(BIN)
	ssh $(SSH_OPTS) $(BOARD) 'mkdir -p /opt/home-hub && systemctl stop home-hub-node 2>/dev/null; true'
	scp $(SSH_OPTS) $(BIN) $(BOARD):/opt/home-hub/home_hub_node
	scp $(SSH_OPTS) home-hub-node.service $(BOARD):/etc/systemd/system/
	ssh $(SSH_OPTS) $(BOARD) 'systemctl daemon-reload && systemctl enable --now home-hub-node && systemctl restart home-hub-node'

clean:
	rm -rf build

.PHONY: all deploy clean
