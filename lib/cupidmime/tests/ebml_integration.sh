#!/bin/sh
# Optional independent muxer checks; no encoder dependency in the core C tests.
set -eu
cli=${CUPIDMIME:-./build/cupidmime}
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
for kind in webm matroska; do
    codec=libopus
    if [ "$kind" = matroska ]; then codec=flac; fi
    ffmpeg -hide_banner -loglevel error -f lavfi -i anullsrc=r=48000:cl=mono \
        -t 0.1 -c:a "$codec" -f "$kind" "$tmp/audio-$kind.png"
    result=$("$cli" -v "$tmp/audio-$kind.png")
    printf '%s\n' "$result"
    printf '%s\n' "$result" | grep -Fq ": audio/$kind [evidence=3 flags=0 "
    codec=libvpx
    if [ "$kind" = matroska ]; then codec=ffv1; fi
    ffmpeg -hide_banner -loglevel error -f lavfi -i color=c=black:s=16x16:r=5 \
        -t 0.4 -threads 1 -c:v "$codec" -an -f "$kind" "$tmp/video-$kind.png"
    result=$("$cli" -v "$tmp/video-$kind.png")
    printf '%s\n' "$result"
    printf '%s\n' "$result" | grep -Fq ": video/$kind [evidence=3 flags=0 "
done
printf 'Independent EBML muxer checks passed\n'
