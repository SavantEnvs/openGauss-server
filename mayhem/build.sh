#!/usr/bin/env bash
#
# mayhem/build.sh — builds libog_query's pre-extracted openGauss SQL parser (src/bin/libog_query) and
#   * two sanitized libFuzzer harnesses (+ -standalone reproducers): the B-compatibility (dolphin) parser
#     and the A-compatibility (server) parser,
#   * the oracle: an unsanitized build of the dolphin parser plus mayhem/test_parser.cpp (run by test.sh).
# Everything resolves offline: apt -dev packages and libsecurec (/opt/toolchains/deps) come from the image.
set -euo pipefail

[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH
: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${MAYHEM_JOBS:=$(nproc)}"
: "${SRC:=/mayhem}"
: "${STANDALONE_FUZZ_MAIN:=/opt/mayhem/StandaloneFuzzTargetMain.c}"
: "${COVERAGE_FLAGS=}"
export SANITIZER_FLAGS DEBUG_FLAGS CC CXX LIB_FUZZING_ENGINE MAYHEM_JOBS COVERAGE_FLAGS

# Two UBSan checks are relaxed, ASan and the rest of UBSan stay on and halting:
#  * function: upstream walks trees through casted function pointers (raw_expression_tree_walker callbacks);
#    the check fires on nearly every statement.
#  * bool: the mock session in src_mock.cpp never initializes the GUC flags the grammar reads (e.g.
#    ENABLE_NULLS_MINIMAL_POLICY_MODE in ORDER BY), so a plain `ORDER BY 1` loads the allocator's 0xBE fill.
RELAX="-fno-sanitize=function,bool"
SECUREC=/opt/toolchains/deps/libboundscheck-1.1.16
OGQ="$SRC/src/bin/libog_query"
BUILD="$SRC/mayhem/build"
mkdir -p "$BUILD"

# src_mock.cpp carries an empty main(); renamed so it cannot clash with the fuzzer/test main.
# Per-flavor extraction + compile of every parser translation unit.
#   $1 name (dolphin|server)  $2 tarball  $3 extra defines  $4 flavor (fuzz|test)
build_objs() {
    local name=$1 tarball=$2 defs=$3 flavor=$4
    local dir="$BUILD/$name" flags
    if [ "$flavor" = fuzz ]; then
        flags="$SANITIZER_FLAGS $DEBUG_FLAGS -fsanitize=fuzzer-no-link $RELAX -O1"
    else
        flags="-O1 $COVERAGE_FLAGS"
    fi
    flags="$flags -std=gnu++14 -fno-strict-aliasing -fwrapv -fPIC -fno-exceptions -w -DPGXC -DENABLE_MOT -DENABLE_HTAP $defs"

    if [ ! -d "$dir/source" ]; then
        mkdir -p "$dir"
        tar -zxf "$OGQ/$tarball" -C "$dir"
        cp "$OGQ/src_mock.cpp" "$dir/source/src_mock.cpp"
        # clang rejects static_cast<char*>(<const char*>) that g++ tolerates; the extracted copy is a build artifact.
        grep -rl 'static_cast<char\*>(' "$dir/source" | xargs -r sed -i 's/static_cast<char\*>(/(char*)(/g'
    fi
    cmp -s "$SRC/mayhem/og_state.cpp" "$dir/source/src_og_state.cpp" || cp "$SRC/mayhem/og_state.cpp" "$dir/source/src_og_state.cpp"
    local objdir="$dir/obj-$flavor"
    if [ "$(cat "$objdir/.flags" 2>/dev/null)" != "$CXX $flags" ]; then
        rm -rf "$objdir"
    fi
    mkdir -p "$objdir"
    echo "$CXX $flags" > "$objdir/.flags"
    ( cd "$dir/source"
      for f in src*.cpp contrib*.cpp; do
          [ -f "$f" ] || continue
          [ "$objdir/$f.o" -nt "$f" ] || echo "$f"
      done | xargs -r -P"$MAYHEM_JOBS" -I{} sh -c \
          "extra=; [ {} = src_mock.cpp ] && extra=-Dmain=og_query_unused_main; \
           $CXX $flags \$extra -I. -Iinclude -Iinclude/plugin_parser -c {} -o '$objdir/{}.o'" )
    ( cd "$objdir" && rm -f libogq.a && shopt -s nullglob && ar rcs libogq.a ./src*.o ./contrib*.o )
}

# Sanitized fuzz binaries: lib (instrumented) + harness + standalone driver.
fuzz_target() {
    local name=$1 tarball=$2 defs=$3
    build_objs "$name" "$tarball" "$defs" fuzz
    local objdir="$BUILD/$name/obj-fuzz"
    local libs="$objdir/libogq.a -L$SECUREC/lib -lsecurec -lcjson -lpthread -ldl -lm"
    local hflags="$SANITIZER_FLAGS $DEBUG_FLAGS -fsanitize=fuzzer-no-link $RELAX -O1 -std=gnu++14"

    $CXX $hflags -c "$SRC/mayhem/fuzz_og_query.cpp" -o "$objdir/harness.o"
    $CXX $hflags -c "$SRC/mayhem/lsan_off.cc" -o "$objdir/lsan_off.o"
    $CC $SANITIZER_FLAGS $DEBUG_FLAGS -x c -c "$STANDALONE_FUZZ_MAIN" -o "$objdir/standalone_main.o"

    $CXX $SANITIZER_FLAGS $DEBUG_FLAGS $LIB_FUZZING_ENGINE "$objdir/harness.o" "$objdir/lsan_off.o" \
        $libs -o "$SRC/fuzz_og_query_$name"
    $CXX $SANITIZER_FLAGS $DEBUG_FLAGS "$objdir/standalone_main.o" "$objdir/harness.o" "$objdir/lsan_off.o" \
        $libs -o "$SRC/fuzz_og_query_$name-standalone"
}

fuzz_target dolphin source_dolphin.tar.gz "-DDOLPHIN"
fuzz_target server source_server.tar.gz ""

# Oracle: the same dolphin parser with NORMAL flags (no sanitizers, no DWARF pin) + the known-answer test.
build_objs dolphin source_dolphin.tar.gz "-DDOLPHIN" test
$CXX -O1 -std=gnu++14 $COVERAGE_FLAGS -c "$SRC/mayhem/test_parser.cpp" -o "$BUILD/dolphin/obj-test/test_parser.o"
$CXX -O1 $COVERAGE_FLAGS "$BUILD/dolphin/obj-test/test_parser.o" "$BUILD/dolphin/obj-test/libogq.a" \
    -L"$SECUREC/lib" -lsecurec -lcjson -lpthread -ldl -lm -o "$SRC/test_parser"
