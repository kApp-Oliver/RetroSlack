# RetroSlack top-level build
ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))

.PHONY: all host mac arduino test-protocol clean run bootstrap disk deploy help

all: host

host:
	@$(ROOT)/scripts/build-host.sh

mac:
	@$(ROOT)/scripts/build-mac.sh

arduino:
	@$(ROOT)/scripts/build-arduino.sh

test-protocol: host
	@$(ROOT)/build/host/test_rhttp_codec

run:
	@$(ROOT)/scripts/run.sh

# Clone HelloMacintosh, install Retro68/Snow, build NSE + hellomacintosh CLI.
bootstrap:
	@$(ROOT)/scripts/bootstrap.sh

disk:
	@$(ROOT)/scripts/make-disk.sh

# Stream RetroSlack.bin over serial (HelloMacintosh must be frontmost).
# Snow: make deploy SNOW=1
# Hardware: make deploy SERIAL=/dev/cu.usbserial-XXXX
deploy: mac
	@$(ROOT)/scripts/deploy.sh $(if $(filter 1,$(SNOW)),--snow) $(if $(SERIAL),--serial $(SERIAL))

clean:
	rm -rf "$(ROOT)/build"

help:
	@echo "Targets: bootstrap host mac arduino test-protocol disk run deploy clean"
	@echo "  NSE:     ./build/host/nse"
	@echo "  Mac app: build/mac/RetroSlack.bin"
	@echo "  Deploy:  make deploy   or   make deploy SNOW=1"
	@echo "  CLI:     ./scripts/hellomacintosh  (from cloned HelloMacintosh repo)"
