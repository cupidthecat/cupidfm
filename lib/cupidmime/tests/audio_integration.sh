#!/bin/sh
# Optional independent-encoder integration check; not part of make test.
set -eu
cli=${CUPIDMIME:-./build/cupidmime}
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
for codec in flac libopus libvorbis; do
    format=ogg
    expected=audio/ogg
    if [ "$codec" = flac ]; then format=flac; expected=audio/flac; fi
    ffmpeg -hide_banner -loglevel error -f lavfi -i anullsrc=r=48000:cl=mono \
        -t 0.1 -c:a "$codec" -metadata_header_padding 0 -f "$format" "$tmp/$codec.png"
    result=$("$cli" -v "$tmp/$codec.png")
    printf '%s\n' "$result"
    printf '%s\n' "$result" | grep -Fq ": $expected [evidence=3 flags=0 "
done
for rate in 44100 22050 11025; do
    for tag in 0 3 4; do
        output="$tmp/mp3-$rate-id3-$tag.png"
        ffmpeg -hide_banner -loglevel error -f lavfi -i "anullsrc=r=$rate:cl=mono" \
            -t 0.25 -c:a libmp3lame -q:a 4 -write_xing 0 -id3v2_version "$tag" \
            -f mp3 "$output"
        result=$("$cli" -v "$output")
        printf '%s\n' "$result"
        printf '%s\n' "$result" | grep -Fq ': audio/mpeg [evidence=3 flags=0 '
    done
done
printf 'Independent audio encoder checks passed\n'
