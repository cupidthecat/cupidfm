CC = clang
.DEFAULT_GOAL := all
AR = ar
CPPFLAGS = -Iinclude
CFLAGS = -O2 -std=c99 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
LDFLAGS =
PREFIX = /usr/local
BUILD = build
CLANG_FORMAT = clang-format-18
FORMAT_SOURCES = $(wildcard include/*.h src/*.c src/*.h tests/*.c tests/*.h)
FONT_SOURCE = /usr/share/fonts/truetype/dejavu/DejaVuSans.ttf

.PHONY: all test sanitize clean install fuzz-smoke integration-audio integration-ebml integration-fonts format format-check
format:
	$(CLANG_FORMAT) -i $(FORMAT_SOURCES)
format-check:
	$(CLANG_FORMAT) --dry-run --Werror $(FORMAT_SOURCES)
all: $(BUILD)/libcupidmime.a $(BUILD)/cupidmime
$(BUILD):
	mkdir -p $(BUILD)
$(BUILD)/cupidmime.o: src/cupidmime.c src/zip.h src/audio.h src/ebml.h src/font.h include/cupidmime.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/zip.o: src/zip.c src/zip.h include/cupidmime.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/audio.o: src/audio.c src/audio.h src/mpeg.h include/cupidmime.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/mpeg.o: src/mpeg.c src/mpeg.h include/cupidmime.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/ebml.o: src/ebml.c src/ebml.h include/cupidmime.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/font.o: src/font.c src/font.h include/cupidmime.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/libcupidmime.a: $(BUILD)/cupidmime.o $(BUILD)/zip.o $(BUILD)/audio.o $(BUILD)/mpeg.o $(BUILD)/ebml.o $(BUILD)/font.o
	$(AR) rcs $@ $^
$(BUILD)/cupidmime: src/cli.c $(BUILD)/libcupidmime.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(BUILD)/libcupidmime.a $(LDFLAGS) -o $@
$(BUILD)/test: tests/test.c $(BUILD)/libcupidmime.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(BUILD)/libcupidmime.a $(LDFLAGS) -o $@
$(BUILD)/test-zip: tests/zip.c tests/zip_fixtures.h $(BUILD)/libcupidmime.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(BUILD)/libcupidmime.a $(LDFLAGS) -o $@
$(BUILD)/test-audio: tests/audio.c $(BUILD)/libcupidmime.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(BUILD)/libcupidmime.a $(LDFLAGS) -o $@
$(BUILD)/test-mpeg: tests/mpeg.c $(BUILD)/libcupidmime.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(BUILD)/libcupidmime.a $(LDFLAGS) -o $@
$(BUILD)/test-ebml: tests/ebml.c $(BUILD)/libcupidmime.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(BUILD)/libcupidmime.a $(LDFLAGS) -o $@
$(BUILD)/test-font: tests/font.c $(BUILD)/libcupidmime.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(BUILD)/libcupidmime.a $(LDFLAGS) -o $@
test: all $(BUILD)/test $(BUILD)/test-zip $(BUILD)/test-audio $(BUILD)/test-mpeg $(BUILD)/test-ebml $(BUILD)/test-font
	./$(BUILD)/test
	./$(BUILD)/test-zip
	./$(BUILD)/test-audio
	./$(BUILD)/test-mpeg
	./$(BUILD)/test-ebml
	./$(BUILD)/test-font
	CUPIDMIME="$(abspath $(BUILD)/cupidmime)" sh tests/cli.sh
sanitize:
	$(MAKE) test BUILD=build/sanitize CC=clang CFLAGS='-O1 -g -std=c99 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror -fsanitize=address,undefined -fno-omit-frame-pointer' LDFLAGS='-fsanitize=address,undefined'
integration-audio: all
	CUPIDMIME="$(abspath $(BUILD)/cupidmime)" sh tests/audio_integration.sh
integration-ebml: all
	CUPIDMIME="$(abspath $(BUILD)/cupidmime)" sh tests/ebml_integration.sh
integration-fonts: all
	CUPIDMIME="$(abspath $(BUILD)/cupidmime)" python3 tests/font_integration.py "$(FONT_SOURCE)"
$(BUILD)/fuzz: tests/fuzz.c src/cupidmime.c src/zip.c src/audio.c src/mpeg.c src/ebml.c src/font.c src/zip.h src/audio.h src/mpeg.h src/ebml.h src/font.h include/cupidmime.h | $(BUILD)
	clang $(CPPFLAGS) -std=c99 -O1 -g -fsanitize=fuzzer,address,undefined tests/fuzz.c src/cupidmime.c src/zip.c src/audio.c src/mpeg.c src/ebml.c src/font.c -o $@
fuzz-smoke: $(BUILD)/fuzz $(BUILD)/test-zip $(BUILD)/test-audio $(BUILD)/test-mpeg $(BUILD)/test-ebml $(BUILD)/test-font
	BUILD_DIR="$(BUILD)" sh tests/fuzz.sh
install: all
	install -d $(DESTDIR)$(PREFIX)/bin $(DESTDIR)$(PREFIX)/lib $(DESTDIR)$(PREFIX)/include
	install -m 755 $(BUILD)/cupidmime $(DESTDIR)$(PREFIX)/bin/
	install -m 644 $(BUILD)/libcupidmime.a $(DESTDIR)$(PREFIX)/lib/
	install -m 644 include/cupidmime.h $(DESTDIR)$(PREFIX)/include/
clean:
	rm -rf build
