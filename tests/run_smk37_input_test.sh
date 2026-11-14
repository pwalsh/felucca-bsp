#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Paul Walsh
# The SMK-37's encoders and faders (boards/smk37/fm1_input.h) on the host: tests/smk37_input_test.c (Linux)
set -eu
cd "$(dirname "$0")/.."
SMK_INPUT_TEST_DIR=$(mktemp -d)
trap 'rm -rf "$SMK_INPUT_TEST_DIR"' EXIT HUP INT TERM
"${CC:-cc}" -std=c11 -D_GNU_SOURCE -O1 -Wall -Wextra -Werror -Wno-unused-function -Wno-int-to-pointer-cast \
    -fsanitize=address,undefined -Ichip -Iboards/smk37 tests/smk37_input_test.c -o "$SMK_INPUT_TEST_DIR/smk37_input_test"
"$SMK_INPUT_TEST_DIR/smk37_input_test"
