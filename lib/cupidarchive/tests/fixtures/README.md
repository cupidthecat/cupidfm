# 7z fixtures

The Copy, LZMA1, LZMA2, Unicode, empty-file, directory, and empty-archive fixtures were generated with libarchive's 7z writer. Nonempty files contain `hello CupidArchive` followed by a newline, repeated 20 times. The Unicode filename contains an accented letter and a supplementary Unicode character.

The encoded-header fixture wraps the LZMA1 fixture's file table in a Python-generated raw LZMA2 stream. It was read back with libarchive to check the archive structure and payload independently of CupidArchive. Run `python3 tests/generate_7z_fixtures.py` on a system with libarchive and a UTF-8 locale to regenerate the fixtures; normal tests use the checked-in files and do not require libarchive.

The regression suite also mutates checksums, payload bytes, and filename property lengths. Failed extraction must preserve an existing destination file.
