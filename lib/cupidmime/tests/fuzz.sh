#!/bin/sh
set -eu
build_dir=${BUILD_DIR:-build}
mkdir -p "$build_dir/corpus"
"$build_dir/test-zip" "$build_dir/corpus/epub"
"$build_dir/test-audio" "$build_dir/corpus/flac" "$build_dir/corpus/ogg"
"$build_dir/test-mpeg" "$build_dir/corpus/mpeg"
"$build_dir/test-ebml" "$build_dir/corpus/webm"
"$build_dir/test-font" "$build_dir/corpus/ttf" "$build_dir/corpus/woff" "$build_dir/corpus/collection"
printf '{"a":[true,false,null,-1.25e3,"hello"]}' > "$build_dir/corpus/json"
printf '<!doctype html><html></html>' > "$build_dir/corpus/html"
printf '\037\213\010\002\000\000\000\000\000\003\247\167\003\000\000\000\000\000\000\000\000\000' > "$build_dir/corpus/gzip"
printf 'RIFF\016\000\000\000WAVEJUNK\001\000\000\000x\000' > "$build_dir/corpus/riff"
printf '\000\000\000\030ftypisom\000\000\000\000avifisom' > "$build_dir/corpus/bmff"
printf '\211PNG\015\012\032\012\000\000\000\015IHDR\000\000\000\001\000\000\000\001\010\006\000\000\000\000\000\000\000' > "$build_dir/corpus/png"
"$build_dir/fuzz" "$build_dir/corpus" -seed=12345 -runs=10000 -max_len=4096
