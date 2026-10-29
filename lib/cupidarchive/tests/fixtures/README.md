# 7z fixtures

The Copy, LZMA1, LZMA2, Unicode, empty-file, directory, and empty-archive fixtures were generated with libarchive's 7z writer. Nonempty files contain `hello CupidArchive` followed by a newline, repeated 20 times. The Unicode filename contains an accented letter and a supplementary Unicode character.

The encoded-header fixture wraps the LZMA1 fixture's file table in a Python-generated raw LZMA2 stream. It was read back with libarchive to check the archive structure and payload independently of CupidArchive. Run `python3 tests/generate_7z_fixtures.py` on a system with libarchive and a UTF-8 locale to regenerate the fixtures; normal tests use the checked-in files and do not require libarchive.

The regression suite also mutates checksums, payload bytes, and filename property lengths. Failed extraction must preserve an existing destination file.


The six `multi-*-on/off.7z` fixtures were written and tested with the 7-Zip CLI. They contain three nonempty files, an empty file, a directory, a Unicode name, mode 0640, and file timestamps of 1700000000. `on` enables solid data; `off` uses separate folders. The methods are Copy, LZMA1, and LZMA2. Run `python3 tests/generate_7z_multi_fixtures.py` with `7z` on PATH to regenerate them.

`multi-libarchive.7z` was written and read back with system libarchive. Its encoded file table describes `first.txt` (`first contents` plus newline, repeated 100 times), `folder/second.txt` (`second contents` plus newline, repeated 100 times), an empty file, and a directory.

`sevenz_fixture.py` builds a small Copy archive with two folders and three file substreams for precise CRC, size, count, and decoded-limit mutations. It follows the public 7z layout; the independent CLI fixtures provide compatibility coverage.
