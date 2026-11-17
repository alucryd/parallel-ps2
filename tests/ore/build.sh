#!/bin/sh
# ORE images read as the images they were made from (main.c).
#
#   SMELT_DIR=<oxyromon>/crates/smelt-rs sh tests/ore/build.sh
#
# Writes a PlayStation 2 DVD ISO (xorriso) and a CD of it, MODE1/2352 with
# a cue sheet, makes OREs of them with oxyromon's orers, and compares each
# ORE with its source through cdvd_iso_open; an ORE of a NES ROM must be
# refused. Skips without SMELT_DIR, as the capture-backed suites do without
# their checkouts; fails when SMELT_DIR is set and cargo, python3 or
# xorriso is missing. SANITIZER is passed through.
set -e
DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$DIR/../.." && pwd)
if [ -z "${SMELT_DIR:-}" ]; then
	echo "ore: SMELT_DIR not set, skipped"
	exit 0
fi
for t in cargo python3 xorriso; do
	command -v $t >/dev/null || { echo "ore: $t not found" >&2; exit 1; }
done
SMELT_DIR=$(cd "$SMELT_DIR" && pwd)
OXYROMON=$(cd "$SMELT_DIR/../.." && pwd)
cargo build -q --release --manifest-path "$SMELT_DIR/Cargo.toml"
cargo build -q --release --manifest-path "$OXYROMON/Cargo.toml" -p ore-rs

LC="$ROOT/libretro/libretro-common"
CC=${CC:-cc}
SANFLAGS="${SANFLAGS:-}"
[ -n "${SANITIZER:-}" ] && SANFLAGS="-fsanitize=$SANITIZER"
INC="-I $ROOT -I $ROOT/pcsx2 -I $ROOT/common -I $LC/include -I $SMELT_DIR/include"
DEFS="-D__LIBRETRO__ -DHAVE_ORE -DHAVE_RCHD_DEFLATE -DHAVE_RCHD_LZMA -DHAVE_RCHD_FLAC -DHAVE_RCHD_ZSTD"
C89="-std=gnu89 -Wall -Werror=declaration-after-statement -Werror=implicit-function-declaration"
WORK=${TMPDIR:-/tmp}/ore_lrps2.$$
OBJ="$WORK/obj"
mkdir -p "$OBJ"
trap 'rm -rf "$WORK"' EXIT

for c in streams/file_stream vfs/vfs_implementation compat/fopen_utf8 \
         encodings/encoding_utf compat/compat_strl file/file_path \
         file/file_path_io file/retro_dirent time/rtime string/stdstring \
         string/rstrtod formats/chd/rchd formats/flac/rflac formats/yaml/ryaml \
         formats/7z/r7z_lzma encodings/encoding_rzstd encodings/encoding_xxh64 \
         encodings/encoding_huffman encodings/encoding_deflate \
         encodings/encoding_crc32 encodings/encoding_rlz4 streams/trans_stream \
         streams/trans_stream_deflate streams/trans_stream_pipe \
         features/features_cpu; do
	"$CC" -O1 -g -w $SANFLAGS $DEFS $INC -c "$LC/$c.c" -o "$OBJ/$(echo $c | tr / _).o"
done
for c in cdvd_reader cdvd_flat cdvd_cso cdvd_chd cdvd_gz cdvd_iso cdvd_ore CDVDisoReader; do
	"$CC" $C89 -O1 -g $SANFLAGS $DEFS $INC -c "$ROOT/pcsx2/CDVD/$c.c" -o "$OBJ/$c.o"
done
"$CC" $C89 -O1 -g $SANFLAGS $DEFS $INC -o "$WORK/ore_read" "$DIR/main.c" "$OBJ"/*.o \
	"$SMELT_DIR/target/release/libsmelt.a" -lpthread -ldl -lm -lrt -lutil

# A DVD: SYSTEM.CNF booting a BOOT2 executable, as orers recognises a
# PlayStation 2 disc by, and 3 MiB that do not compress, so many chunks
mkdir -p "$WORK/root"
printf 'BOOT2 = cdrom0:\\SLUS_000.00;1\r\nVER = 1.00\r\n' > "$WORK/root/SYSTEM.CNF"
head -c 3145728 /dev/urandom > "$WORK/root/SLUS_000.00"
xorriso -as mkisofs -quiet -V GAME -o "$WORK/Game.iso" "$WORK/root"

# The same as a CD, each 2048 bytes in a MODE2 form 1 frame, as a
# PlayStation 2 CD's (EDC/ECC left zero: the ORE is lossless either way)
python3 - "$WORK/Game.iso" "$WORK/Cd.bin" <<'EOF'
import sys
data = open(sys.argv[1], 'rb').read()
bcd = lambda v: (v // 10) << 4 | v % 10
with open(sys.argv[2], 'wb') as out:
    for lba in range(len(data) // 2048):
        a = lba + 150
        out.write(b'\x00' + b'\xff' * 10 + b'\x00'
                  + bytes([bcd(a // 4500), bcd(a // 75 % 60), bcd(a % 75), 2]) + bytes(8)
                  + data[lba * 2048:(lba + 1) * 2048] + bytes(280))
EOF
printf 'FILE "Cd.bin" BINARY\r\n  TRACK 01 MODE2/2352\r\n    INDEX 01 00:00:00\r\n' > "$WORK/Cd.cue"

head -c 40960 /dev/urandom > "$WORK/Rom.nes"

ORERS="$OXYROMON/target/release/orers"
"$ORERS" -o "$WORK" "$WORK/Game.iso" >/dev/null
"$ORERS" -o "$WORK" "$WORK/Cd.cue" >/dev/null
"$ORERS" --platform nes -o "$WORK" "$WORK/Rom.nes" >/dev/null

"$WORK/ore_read" "$WORK/Game.iso" "$WORK/Game.ore" "$WORK/Cd.bin" "$WORK/Cd.ore" "!$WORK/Rom.ore"
