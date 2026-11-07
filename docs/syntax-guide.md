# Syntax highlighting

CupidFM ships 158 commented CupidConf definitions. They cover filename and content detection,
ordered regex patterns, nested regions, escaped delimiters, delimiter colors, and embedded
languages. The editor and text previews use the same C engine and physical-line state.

## Installation and overrides

Running the binary from the checkout loads the adjacent syntax directory automatically.
install.sh installs the definitions and their license in /usr/share/cupidfm/syntax.
No first-run download or manual copy is needed.

Definitions in ~/.cupidfm/syntax take precedence. A definition with the same language name
replaces the bundled definition. Legacy keyword-list definitions remain supported.
CUPIDFM_SYNTAX_DIR adds a directory after the user directory and before bundled directories.

Search order is the user directory, the optional environment directory, the executable's
adjacent syntax directory, /usr/local/share/cupidfm/syntax, then /usr/share/cupidfm/syntax.
Filename matches take precedence over header and content detection. Content detection reads
at most 64 KiB from a regular file and does not read FIFOs or devices.

## Rules

Regex values use single quotes. Backslashes, comment characters, leading/trailing spaces,
and a leading tilde stay literal. Write two single quotes to include one quote.

    format=2
    name='example'

    # Detection.
    detect.filename='\.example$'

    # Region 0: comments continue until their closing delimiter.
    rule.0.parent=-1
    rule.0.group='comment'
    rule.0.start='/\*'
    rule.0.end='\*/'

    # Pattern 1: a nested reminder inside the comment.
    rule.1.parent=0
    rule.1.group='todo'
    rule.1.match='\b(TODO|FIXME)\b'

    rules=2

Parent -1 means the root. A region's children point to its rule number. Patterns have a
match expression; regions have start/end expressions and may add skip and limit-group.
Skip expressions mask escaped text when finding delimiters. Limit-group selects the
delimiter style. Later patterns override earlier patterns. Included language rules are
expanded after local rules, so installed files are self-contained.

The C engine carries region identity between lines, including text beyond a clipped viewport.
It uses GLib's UTF-8 regex matcher. Traversal is bounded for malformed or cyclic zero-width
rules; nesting beyond 128 simultaneous traversal calls is limited.

## Colors

The default foreground palette follows the bundled group styles. Groups inherit their parent
style, with overrides such as constant.string, constant.string.char, symbol.tag, and type.extended.
Explicit bold styles are retained where defined.

Highlighting keeps the terminal's default background. It does not redefine the terminal palette.
Direct-color terminals use RGB values when ncurses exposes direct colors; 256-color and basic
terminals use the nearest available foreground color.

## Examples and verification

examples/syntax contains C, Markdown, Python, JavaScript, HTML with embedded CSS/JavaScript,
shell, JSON, and YAML examples. Expected token colors were captured from an independent engine.
The shared corpus is checked against every bundled definition, including Markdown.

    make CC=clang
    make test CC=clang

The syntax tests compile all 158 definitions, compare their token-color output with the
checked-in expectations, check multiline and clipped rendering, and assert default backgrounds.
These examples are regression coverage, not proof that every possible input has identical output.

tools/convert_syntax.py converts a syntax source directory with PyYAML. It preserves rule
precedence, expands includes, copies the license, and records input hashes in syntax/manifest.json.
PyYAML is a conversion-time dependency; CupidFM does not need it at runtime.

    python3 tools/convert_syntax.py /path/to/syntax syntax --license-file /path/to/LICENSE

The theme-format catalog uses the neutral name colorscheme and the .theme extension; its rule
behavior is preserved. The converter accepts --style-definition to reproduce this adaptation.

The adapted engine and definitions retain their MIT notice in syntax/LICENSE.
