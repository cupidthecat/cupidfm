#!/usr/bin/env bash
set -euo pipefail

OUTDIR="archives"
WORKDIR="work"
TXT="plain.txt"

mkdir -p "$OUTDIR" "$WORKDIR"

# ------------------------------------------------------------
# Base test file
# ------------------------------------------------------------
echo "Creating base test file"
{
  echo "This is a plain text file."
  echo "It exists to test archive parsing."
  echo "Unicode: café naïve jalapeño"
  printf "Binary-ish bytes: \x01\x02\x03\n"
} > "$WORKDIR/$TXT"

# ------------------------------------------------------------
# Helper: check command exists
# ------------------------------------------------------------
have() {
    command -v "$1" >/dev/null 2>&1
}

# ------------------------------------------------------------
# TAR variants
# ------------------------------------------------------------
echo "Creating TAR variants"

tar -cf "$OUTDIR/plain.tar" -C "$WORKDIR" "$TXT"

tar -czf "$OUTDIR/plain.tar.gz" -C "$WORKDIR" "$TXT"
tar -cjf "$OUTDIR/plain.tar.bz2" -C "$WORKDIR" "$TXT"

if have xz; then
    tar -cJf "$OUTDIR/plain.tar.xz" -C "$WORKDIR" "$TXT"
fi

# ------------------------------------------------------------
# PAX tar (long paths, big metadata)
# ------------------------------------------------------------
echo "Creating PAX tar (long path)"

LONGDIR="$WORKDIR/$(printf 'longdir_%.0s' {1..20})"
mkdir -p "$LONGDIR"
cp "$WORKDIR/$TXT" "$LONGDIR/$TXT"

tar --format=pax -cf "$OUTDIR/plain_pax.tar" -C "$WORKDIR" "$(basename "$LONGDIR")"

# ------------------------------------------------------------
# GNU longname / longlink tar
# ------------------------------------------------------------
echo "Creating GNU longname tar"

LN="$(printf 'very_long_filename_%.0s' {1..8})plain.txt"
cp "$WORKDIR/$TXT" "$WORKDIR/$LN"

tar --format=gnu -cf "$OUTDIR/plain_gnu_longname.tar" -C "$WORKDIR" "$LN"

# ------------------------------------------------------------
# Sparse file tar (important for VM images / backups)
# ------------------------------------------------------------
echo "Creating sparse file tar"

SPARSE="$WORKDIR/sparse.bin"
dd if=/dev/zero of="$SPARSE" bs=1 count=0 seek=100M

# write a few real blocks
echo "DATA1" | dd of="$SPARSE" bs=1 seek=0 conv=notrunc
echo "DATA2" | dd of="$SPARSE" bs=1 seek=50M conv=notrunc

tar --sparse -cf "$OUTDIR/plain_sparse.tar" -C "$WORKDIR" "$(basename "$SPARSE")"

# ------------------------------------------------------------
# Hardlink tar
# ------------------------------------------------------------
echo "Creating hardlink tar"

# Remove any existing hardlink file first
rm -f "$WORKDIR/plain_hardlink.txt"
ln "$WORKDIR/$TXT" "$WORKDIR/plain_hardlink.txt"
tar -cf "$OUTDIR/plain_hardlink.tar" -C "$WORKDIR" "$TXT" plain_hardlink.txt

# ------------------------------------------------------------
# ZIP variants
# ------------------------------------------------------------
echo "Creating ZIP variants"

(cd "$WORKDIR" && zip -q "../$OUTDIR/plain.zip" "$TXT")

# Streaming ZIP (forces data descriptor bit 3)
(cd "$WORKDIR" && zip -q -0 - "$TXT" > "../$OUTDIR/plain_streamed.zip")


# ------------------------------------------------------------
# 7z archives
# ------------------------------------------------------------
if have 7z; then
    echo "Creating 7z archives"
    (cd "$WORKDIR" && 7z a -bd "../$OUTDIR/plain.7z" "$TXT" >/dev/null)
fi

# ------------------------------------------------------------
# GZ / BZ2 single-file compression
# ------------------------------------------------------------
echo "Creating single-file compressed formats"

gzip -c "$WORKDIR/$TXT" > "$OUTDIR/plain.txt.gz"
bzip2 -c "$WORKDIR/$TXT" > "$OUTDIR/plain.txt.bz2"
if have xz; then
    xz -c "$WORKDIR/$TXT" > "$OUTDIR/plain.txt.xz"
fi

# ------------------------------------------------------------
# AR archive (for DEB groundwork)
# ------------------------------------------------------------
echo "Creating ar archive"

echo "1.0" > "$WORKDIR/debian-binary"
ar rcs "$OUTDIR/plain.ar" "$WORKDIR/debian-binary" "$WORKDIR/$TXT"

# ------------------------------------------------------------
# Cleanup
# ------------------------------------------------------------
rm -rf "$WORKDIR"

echo
echo "Archive corpus created in ./$OUTDIR"
ls -lh "$OUTDIR"
