#!/usr/bin/env bash
set -e

# config
SIZE_MB=100
BASENAME=bench_data
WORKDIR=archive_bench

mkdir -p "$WORKDIR"
cd "$WORKDIR"

echo "[*] Creating ${SIZE_MB}MB raw file..."
dd if=/dev/zero of=${BASENAME}.bin bs=1M count=${SIZE_MB} status=progress

echo "[*] Creating TAR..."
tar cf ${BASENAME}.tar ${BASENAME}.bin

echo "[*] Creating TAR.GZ..."
tar czf ${BASENAME}.tar.gz ${BASENAME}.bin

echo "[*] Creating TAR.BZ2..."
tar cjf ${BASENAME}.tar.bz2 ${BASENAME}.bin

echo "[*] Creating TAR.XZ..."
tar cJf ${BASENAME}.tar.xz ${BASENAME}.bin

echo "[*] Creating ZIP..."
zip -q ${BASENAME}.zip ${BASENAME}.bin

echo "[*] Creating 7Z..."
7z a -bd ${BASENAME}.7z ${BASENAME}.bin > /dev/null

echo "[*] Creating GZ..."
cp ${BASENAME}.bin ${BASENAME}.gz.src
gzip -c ${BASENAME}.gz.src > ${BASENAME}.gz
rm ${BASENAME}.gz.src

echo "[*] Creating BZ2..."
cp ${BASENAME}.bin ${BASENAME}.bz2.src
bzip2 -c ${BASENAME}.bz2.src > ${BASENAME}.bz2
rm ${BASENAME}.bz2.src

echo "[*] Creating XZ..."
cp ${BASENAME}.bin ${BASENAME}.xz.src
xz -c ${BASENAME}.xz.src > ${BASENAME}.xz
rm ${BASENAME}.xz.src

echo
echo "=== Archive files created ==="
ls -lh
