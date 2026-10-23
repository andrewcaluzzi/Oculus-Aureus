#!/usr/bin/env bash
# Build and run the bot logic unit tests (32-bit, same flags as the port).
# Run from the repo root (or inside the docker image mounted at /home/dev/project).
set -e
mkdir -p build_tests
gcc -m32 -std=c11 -O2 -Wall -Werror -Iport/include \
    -o build_tests/botlogic_test \
    tests/botlogic_test.c port/src/port_botlogic.c
./build_tests/botlogic_test
gcc -m32 -std=c11 -O2 -Wall -Werror -Iport/include \
    -o build_tests/event_test \
    tests/event_test.c port/src/port_event.c
./build_tests/event_test
gcc -m32 -std=c11 -O2 -Wall -Werror -Iport/include \
    -o build_tests/modparse_test \
    tests/modparse_test.c port/src/port_modparse.c
./build_tests/modparse_test
gcc -m32 -std=c11 -O2 -Wall -Werror -Iport/include \
    -o build_tests/cooplogic_test \
    tests/cooplogic_test.c port/src/port_cooplogic.c
./build_tests/cooplogic_test
gcc -m32 -std=c11 -O2 -Wall -Werror -Iport/include \
    -o build_tests/netmsg_test \
    tests/netmsg_test.c port/src/port_netmsg.c
./build_tests/netmsg_test
gcc -m32 -std=c11 -O2 -Wall -Werror -Iport/include \
    -o build_tests/netlock_test \
    tests/netlock_test.c port/src/port_netlock.c port/src/port_netmsg.c
./build_tests/netlock_test
gcc -m32 -std=c11 -O2 -Wall -Werror -Iport/include \
    -o build_tests/nethash_test \
    tests/nethash_test.c port/src/port_nethash.c port/src/port_netmsg.c
./build_tests/nethash_test
gcc -m32 -std=c11 -O2 -Wall -Werror -Iport/include \
    -o build_tests/netstats_test \
    tests/netstats_test.c port/src/port_netstats.c
./build_tests/netstats_test
