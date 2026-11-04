#!/usr/bin/env bash
# Evil Tuner XXL -- GEN2 (aarch64) build: the same pipeline as build.sh, retargeted
# for MPC Gen2 devices (Live II Gen2 / Live III / One+ / Force Gen2: RK3588, glibc 2.39).
#   helpers: aarch64-linux-musl, fully static (et_exec + ffmpeg; et_roc/shairport-sync via
#            helpers/build_helpers_gen2.sh when enabled) -- see helpers/build_helpers_gen2.sh
#   plugin : aarch64-linux-gnu (glibc 2.31 ceiling: loads on Gen2's glibc 2.39)
# Output: build/package-gen2/L'Cronx - VST - Evil Tuner XXL/  (skin + evil_tuner_xxl.so + bin/)
# No host deps beyond Docker (or a python3 with ziglang+pillow+numpy+scipy):
#   docker run --rm -v "$PWD":/w -w /w python:3.11-slim \
#     bash -c "pip install -q ziglang pillow numpy scipy && ./build-gen2.sh"
# Zig cross-compiles natively.
set -euo pipefail
cd "$(dirname "$0")"
MPC_VST="${MPC_VST:-$PWD/third_party/mpc-vst-plugins}"
ZIG="${ZIG:-python3 -m ziglang}"
NAME="L'Cronx - VST - Evil Tuner XXL"
. ./build_srcs.sh
python3 tools/gen_params.py
python3 tools/panel.py --check

mkdir -p build/tools build/arm64 build/helpers
TGTM="-target aarch64-linux-musl -mcpu=generic"
[ -x build/helpers/et_exec ] || $ZIG cc $TGTM -static -O2 -s helpers/et_exec.c -o build/helpers/et_exec
# ffmpeg (decode-only, pipe contract) if not built yet by helpers/build_helpers_gen2.sh
[ -x build/helpers/ffmpeg ] || helpers/build_helpers_gen2.sh ffmpeg

[ -x build/tools/shadow_art ] || gcc -O2 -w -I"$MPC_VST/tools/vendor/force-shadow/tools" -x c -o build/tools/shadow_art "$MPC_VST/tools/shadow_art.c" -lm
rm -rf build/skin
SHADOW_ART="$PWD/build/tools/shadow_art" python3 "$MPC_VST/tools/gen_vst.py" vst.json 2>&1 | grep -v -e "TOUCH" -e "EDGE" || true
[ -f build/params.h ] && [ -d "build/skin/$NAME/Plugin Skins" ]
python3 tools/panel_art.py "build/skin/$NAME/Plugin Skins"
python3 "$MPC_VST/tools/skin_check.py" "build/skin/$NAME"

TGT="-target aarch64-linux-gnu.2.31 -mcpu=generic -ffp-contract=off"
COMMON="-O2 -fPIC -fvisibility=hidden -ffunction-sections -fdata-sections -DNDEBUG"
OBJS=""
for f in $ENGINE_SRCS; do
  o="build/arm64/$(echo "$f" | tr / _).o"
  $ZIG c++ $TGT $COMMON -std=gnu++17 -Wall -Wno-unused-function $DEFS $INCS -Ibuild -I"$MPC_VST/wrapper" -c "$f" -o "$o"
  OBJS="$OBJS $o"
done
for f in $DF_SRCS $CL_SRCS; do
  o="build/arm64/$(echo "$f" | tr / _).o"
  [ "$o" -nt "$f" ] || $ZIG c++ $TGT $COMMON -std=gnu++11 -w -Wno-c++11-narrowing -Wno-narrowing $DEFS $INCS -c "$f" -o "$o"
  OBJS="$OBJS $o"
done
$ZIG cc $TGT $COMMON -std=gnu11 -w -Ibuild -I"$MPC_VST/wrapper" -c "$MPC_VST/wrapper/vst2_wrap.c" -o build/arm64/vst2_wrap.o
printf '{\n  global: VSTPluginMain;\n  local: *;\n};\n' > build/arm64/exports.map
$ZIG c++ $TGT -shared -fPIC -Wl,--no-undefined -Wl,--version-script=build/arm64/exports.map -Wl,--gc-sections -Wl,-s \
  $OBJS build/arm64/vst2_wrap.o -lm -ldl -lpthread -o build/arm64/evil_tuner_xxl.so
# the framework pipeline's post-link gate (tools/build_port.sh): exports + glibc ceiling
readelf --dyn-syms -W build/arm64/evil_tuner_xxl.so | grep -E ' GLOBAL .* [0-9]+ [A-Za-z]' | grep -v UND | awk '{printf "exported: %s ", $8} END {print ""}'
echo "highest glibc: $(readelf -V build/arm64/evil_tuner_xxl.so | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -1) (Gen1 MPC OS 2.x has 2.32, Gen2 3.x has 2.39)"
md5sum build/arm64/evil_tuner_xxl.so

PKG="build/package-gen2/$NAME"
rm -rf build/package-gen2 && mkdir -p "$PKG/bin"
cp -r "build/skin/$NAME/." "$PKG/"
cp build/arm64/evil_tuner_xxl.so "$PKG/"
cp build/helpers/et_exec build/helpers/ffmpeg "$PKG/bin/"
for h in et_roc shairport-sync; do [ -x "build/helpers/$h" ] && cp "build/helpers/$h" "$PKG/bin/"; done
chmod 755 "$PKG"/bin/*
echo "-> $PKG"
