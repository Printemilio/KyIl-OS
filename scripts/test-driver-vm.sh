#!/usr/bin/env bash
set -euo pipefail
ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
cd "$ROOT"
mkdir -p build/driver-tests
gcc -O1 -g -fsanitize=address,undefined -idirafter kernel/include \
  kernel/src/driver_vm.c tests/driver_vm_test.c kernel/src/mbedtls/sha256.c \
  kernel/src/mbedtls/platform_util.c -o build/driver-tests/vm-test
build/driver-tests/vm-test drivers/cmos.driver
gcc -O1 -g -fsanitize=address,undefined -DHARDWARE_RESOURCE_TESTING -idirafter kernel/include \
  kernel/src/hardware_resources.c tests/hardware_resource_test.c -pthread -o build/driver-tests/broker-test
build/driver-tests/broker-test
gcc -O1 -g -Wall -Wextra -fsanitize=address,undefined -DORA_TESTING -idirafter kernel/include \
  kernel/src/driver_dma.c kernel/src/driver_vm.c kernel/src/ora.c tests/driver_dma_test.c \
  kernel/src/mbedtls/sha256.c kernel/src/mbedtls/platform_util.c -pthread -o build/driver-tests/dma-test
build/driver-tests/dma-test drivers/dma-memory-test.driver
gcc -O1 -g -ffunction-sections -fdata-sections -fsanitize=address,undefined -DVEX_NETWORK_HOST_TEST -idirafter kernel/include \
  kernel/src/udp.c tests/udp_destination_test.c -Wl,--gc-sections -o build/driver-tests/udp-destination-test
build/driver-tests/udp-destination-test
gcc -O1 -g -ffunction-sections -fdata-sections -fsanitize=address,undefined -idirafter kernel/include \
  kernel/src/dhcp.c tests/dhcp_events_test.c -Wl,--gc-sections -o build/driver-tests/dhcp-events-test
build/driver-tests/dhcp-events-test
