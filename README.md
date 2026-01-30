# cupidfm

cupidfm is a terminal-based file manager implemented in C. It uses the `ncurses` library for the user interface, providing features like directory navigation, directory tree preview, file preview, file editing, and file information display. 

![preview](img/preview2.png)

<video src="img/demo.mp4" width="320" height="240" controls></video>

## Prerequisites

### Terminal Requirements

For proper emoji display:
- Make sure your terminal emulator supports Unicode and emoji rendering
For proper emoji and icon display:

1. Install a Nerd Font (recommended):
```bash
# On Ubuntu/Debian:
mkdir -p ~/.local/share/fonts
cd ~/.local/share/fonts
curl -fLO https://github.com/ryanoasis/nerd-fonts/releases/download/v3.1.1/JetBrainsMono.zip
unzip JetBrainsMono.zip
fc-cache -fv
```

2. Configure your terminal:
- Set your terminal font to "JetBrainsMono Nerd Font" (or another Nerd Font)
- Ensure your terminal emulator supports Unicode and emoji rendering
- Set your locale to support UTF-8: `export LANG=en_US.UTF-8`

Alternative fonts:
- Noto Color Emoji (`sudo apt install fonts-noto-color-emoji`)
- Fira Code Nerd Font
- Hack Nerd Font

If emojis aren't displaying correctly:
1. Check your terminal supports Unicode: `echo -e "\xf0\x9f\x93\x81"`
2. Verify locale settings: `locale`
3. Try updating your terminal emulator to a newer version

Note: Some terminal emulators like Alacritty, iTerm2, Konsole, and Kitty are known to work better with unicode/emojis. 

## Libraries used

System/third-party dependencies:
- [ncurses](https://invisible-island.net/ncurses/) (terminal UI)
- [libmagic](https://github.com/file/file) (MIME/type detection)
- [OpenSSL](https://www.openssl.org/) (TLS/HTTPS support for cupidscript)
- [zlib](https://zlib.net/), [bzip2](https://sourceware.org/bzip2/), and [xz/liblzma](https://tukaani.org/xz/) (compression backends used by archive preview)
- [xclip](https://github.com/astrand/xclip) (clipboard integration)

Project libraries (customized/updated versions):
- [cupidconf](https://github.com/cupidthecat/cupidconf) (config loader; this repo vendors a customized version in `lib/cupidconf.c`)
- [cupidscript](https://github.com/cupidthecat/cupidscript) (plugin scripting; linked as `lib/libcupidscript.a`)
- [cupidarchive](https://github.com/cupidthecat/cupidarchive) (archive preview; linked as `lib/libcupidarchive.a`)

To build and run cupidfm, you must have the following packages installed:

- **A C Compiler & Build Tools** (e.g. `gcc`, `make`)
- **ncurses** development libraries (for terminal handling)
- **libmagic** development libraries (for MIME type detection)
- **OpenSSL** development libraries (for HTTPS support in cupidscript)
- **zlib + bzip2 + xz** development libraries (archive preview via `cupidarchive`)
- **xclip** (for clipboard support)

### Installing Dependencies on Ubuntu/Debian

Open a terminal and run:

```bash
sudo apt update
sudo apt install build-essential libncurses-dev libmagic-dev libssl-dev zlib1g-dev libbz2-dev liblzma-dev xclip
```

### Installing Dependencies on Arch Linux

Open a terminal and run:

```bash
sudo pacman -Syu
sudo pacman -S base-devel ncurses file openssl zlib bzip2 xz xclip
```

*Notes:*
- On Arch, the package named **file** provides libmagic.
- The package **base-devel** installs gcc, make, and other essential build tools.

---

## Building the Project

To compile the project, run the provided build script:

```bash
./dev.sh
```

This script invokes `make` (with predefined flags) to compile the source code and produce an executable named `cupidfm`.

### Compilation Flags

The build script uses flags such as:
- `-Wall -Wextra -pedantic` to enable warnings
- Additional warnings (`-Wshadow -Werror -Wstrict-overflow`)
- Sanitizers (`-fsanitize=address -fsanitize=undefined`) for debugging

---

## Running the Program

After building, start cupidfm with:

```bash
./cupidfm
```

Error logs (if any) will be saved to `log.txt`.

# Features

- Navigate directories using arrow keys
- View file details and preview supported file types
- Display MIME types based on file content using `libmagic`
- Archive preview for common formats (`.zip`, `.tar`, `.tar.gz`, `.7z`, etc.) via `cupidarchive`
- File type indicators with emoji icons:
  - 📄 Text files
  - 📝 C source files
  - 🔣 JSON files
  - 📑 XML files
  - 🐍 Python files
  - 🌐 HTML files
  - 🎨 CSS files
  - ☕ Java files
  - 💻 Shell scripts
  - 🦀 Rust files
  - 📘 Markdown files
  - 📊 CSV files
  - 🐪 Perl files
  - 💎 Ruby files
  - 🐘 PHP files
  - 🐹 Go files
  - 🦅 Swift files
  - 🎯 Kotlin files
  - ⚡ Scala files
  - 🌙 Lua files
  - 📦 Archive files
- Text editing capabilities within the terminal
- Directory tree visualization with permissions
- File information display (size, permissions, modification time)
- Background directory size calculation with a live "Calculating... <size so far>" progress display
- Scrollable preview window
- Tab-based window switching between directory and preview panes
- Configure keybinds

## Performance

CupidFM is optimized for speed and efficiency. Our comprehensive test suite (63 tests across 8 suites) and performance benchmarks demonstrate excellent performance characteristics:

### Core Data Structure Performance

- **Vector Operations:**
  - Add 100 elements: **0.645 μs** (645 ns)
  - Element access: **1.95 ns**
  - Capacity management: **651-749 ns**

- **VecStack Operations:**
  - Push/pop: **0.167 μs** (166.5 ns) - **50% faster** after optimization
  - Peek: **2.03 ns** (100x faster than push/pop, as expected)
  - Large stack (1k elements): **15.6 μs**

### File System Operations

- **Path Join:** **38-138 ns** depending on complexity
- **Directory Reading:**
  - Small directories (`/tmp`): **42.6 μs** (hot cache)
  - Medium directories (`/usr/lib`, 99 entries): **10.5 μs** (hot cache)
  - Large directories (`/usr/bin`, 2,254 entries): **325.4 μs** (hot cache)
- **Cold Cache Performance** (realistic browsing scenario):
  - First read is **1.75-3.8x slower** than hot cache, demonstrating the importance of OS page caching
  - Warm steady-state matches hot cache performance

### Optimizations Applied

1. **VecStack Optimizations** - Achieved **32% performance improvement**:
   - Cached Vector length to eliminate redundant function calls
   - Used `Vector_set_len_no_free` for push operations
   - Pre-allocated capacity (10 elements) to reduce reallocations

2. **Memory Safety** - All critical memory issues fixed:
   - Safe `realloc` usage with temporary pointers
   - Proper memory cleanup in all data structures
   - Zero memory leaks (validated with AddressSanitizer and Valgrind)

3. **String Operations:**
   - `strlen`: **1.90 ns**
   - `strncpy`: **7.34 ns**
   - `snprintf`: **54.52 ns**

For detailed performance analysis and test suite documentation, see [`TESTING_AND_PERFORMANCE.md`](TESTING_AND_PERFORMANCE.md).

## Configuration

### Keybinds (Quick Reference)

All keybinds are configurable via `~/.cupidfmrc`. These are the defaults.

### Browser Mode (Directory/Preview)

| Action | Default |
| --- | --- |
| Move up | `KEY_UP` |
| Move down | `KEY_DOWN` |
| Go to parent directory | `KEY_LEFT` |
| Enter directory | `KEY_RIGHT` |
| Switch Directory/Preview pane | `Tab` |
| Exit | `F1` |
| Edit file (from Preview pane) | `^E` |
| Copy | `^C` |
| Paste | `^V` |
| Cut | `^X` |
| Delete | `^D` |
| Rename | `^R` |
| New file | `^N` |
| New directory | `Shift+N` |
| Fuzzy search | `^F` |
| Select all (current view) | `^A` |
| Open console | `^O` |

### Search Prompt

| Action | Key |
| --- | --- |
| Move selection | `KEY_UP` / `KEY_DOWN` |
| Page | `PageUp` / `PageDown` |
| Accept (keep filtered list) | `Enter` |
| Cancel (restore previous selection) | `Esc` |

### Edit Mode

| Action | Default |
| --- | --- |
| Move cursor | `KEY_UP` / `KEY_DOWN` / `KEY_LEFT` / `KEY_RIGHT` |
| Save | `^S` |
| Quit | `^Q` |
| Backspace | `KEY_BACKSPACE` |

### Default Keybindings

cupidfm comes with a set of **default keybindings**. On **first run**, if cupidfm cannot find a user configuration file, it will **auto-generate** one at:

```
~/.cupidfmrc
```

Below is a screenshot showing the start up

![preview](img/startup.png)

This auto-generated config file includes default bindings, for example:
The default includes `key_search=^F` (Ctrl+F) for fuzzy search and `key_new_dir=Shift+N` for creating directories, both of which you can change by editing `~/.cupidfmrc` and restarting CupidFM.

```
key_up=KEY_UP
key_down=KEY_DOWN
key_left=KEY_LEFT
key_right=KEY_RIGHT
key_tab=Tab
key_exit=F1

key_edit=^E
key_copy=^C
key_paste=^V
key_cut=^X
key_delete=^D
key_rename=^R
key_new=^N
key_search=^F
key_new_dir=Shift+N
key_select_all=^A
key_undo=^Z
key_redo=^Y
key_permissions=^P
key_console=^O

edit_up=KEY_UP
edit_down=KEY_DOWN
edit_left=KEY_LEFT
edit_right=KEY_RIGHT
edit_save=^S
edit_quit=^Q
edit_backspace=KEY_BACKSPACE
edit_copy=^C
edit_cut=^X
edit_paste=^V
edit_select_all=^A
edit_undo=^Z
edit_redo=^Y
```

**Immediately after creating** `~/.cupidfmrc` for the first time, CupidFM will display a **popup** in the interface letting you know where it wrote your new config.

### Editing the Config File

After CupidFM creates this file, you are free to **edit** it to customize keybindings or add new mappings. Here are some rules/notes:

1. **Valid Formats**

   - You may use special ncurses names like `KEY_UP`, `KEY_DOWN`, `KEY_LEFT`, etc.
   - You can assign **Ctrl**+**key** by using a caret, e.g. `^C`.
   - Single characters (`a`, `b`, `x`) are also valid.

2. **Commenting and Whitespace**

   - Lines beginning with `#` are treated as comments and ignored.
   - Blank or whitespace-only lines are also ignored.

3. **Sample**

   If you only want to change the exit key from **F1** to **Esc**, you might do:
   ```text
   key_exit=27
   ```
   since **ASCII 27** is **Esc** in decimal form.

4. **Restart Required**

   - Changes to `~/.cupidfmrc` take effect **next time** you launch CupidFM.

### Where CupidFM Searches for the Config

1. **`~/.cupidfmrc`**  
   By default, CupidFM looks for this file in your home directory.

2. **No config found?**  
   - CupidFM loads **hard-coded defaults** (arrow keys, F1, etc.) 
   - Automatically **writes** a new file to `~/.cupidfmrc`, which you can later edit.

3. **Config exists but can’t be loaded?**  
   - CupidFM keeps defaults and shows a **Configuration Errors** popup instead of overwriting your config.

### Common Changes to the Config

- **Changing the Exit Key**  
  ```text
  key_exit=F10
  ```
  or
  ```text
  key_exit=27  # 27 = ESC
  ```
- **Using Emacs-like Keys**  
  If you prefer `Ctrl+P` for up and `Ctrl+N` for down:
  ```text
  key_up=^P
  key_down=^N
  ```
- **Remapping Left/Right**  
  ```text
  key_left=KEY_BACKSPACE
  key_right=KEY_ENTER
  ```

### Troubleshooting

- **Config Not Created**:  
  Make sure you have a valid `$HOME` environment variable set. If `$HOME` is missing or empty, CupidFM will try to create the config in the current directory instead.
- **Invalid or Unknown Key**:  
  If you enter an invalid key name, it will be ignored and remain at default. Check the logs or run from a terminal to see error messages.
- **Changing Keybindings**:
  - If something stops working after changes, revert the line or remove it to fall back to the default.
  - You can always delete `~/.cupidfmrc` and relaunch to regenerate a fresh config.

With these steps, you can **fully customize** your keybindings in `~/.cupidfmrc`. If you ever lose or remove it, CupidFM will rewrite the default file and let you know on the next run!

## Syntax Highlighting

CupidFM features a powerful and extensible syntax highlighting system for the built-in text editor. Syntax highlighting enhances code readability by applying different colors to keywords, types, strings, comments, operators, and more.

### Built-in Language Support

Out of the box, CupidFM includes syntax highlighting for:
- **C/C++** (`.c`, `.h`, `.cpp`, `.hpp`, `.cc`, `.cxx`, etc.)
- **Bash/Shell** (`.sh`, `.bash`)
- **Markdown** (`.md`, `.markdown`)

### Installing Syntax Files

Syntax highlighting configuration files use the `.cupidconf` extension and are stored in:

```
~/.cupidfm/syntax/
```

To install a syntax file:

1. **Create the syntax directory** (if it doesn't exist):
   ```bash
   mkdir -p ~/.cupidfm/syntax
   ```

2. **Copy syntax files** to the directory:
   ```bash
   cp c.cupidconf ~/.cupidfm/syntax/
   cp bash.cupidconf ~/.cupidfm/syntax/
   cp md.cupidconf ~/.cupidfm/syntax/
   ```

3. **Restart CupidFM** to load the new syntax definitions.

### Writing Custom Syntax Files

Syntax files use a simple key-value format. Here's how to create your own:

#### File Naming

Name your file `<language>.cupidconf`, for example:
- `python.cupidconf` for Python
- `javascript.cupidconf` for JavaScript
- `rust.cupidconf` for Rust

#### Basic Structure

```conf
# Language name
language = python

# File extensions (comma-separated)
extensions = .py, .pyw, .pyi

# Keywords - control flow and main language keywords
keywords = if, else, elif, for, while, def, class, return, break, continue

# Statements - modifiers and declarations
statements = import, from, as, with, pass, yield, async, await, lambda

# Types - built-in types
types = int, str, float, bool, list, dict, tuple, set, None

# Constants - built-in constants
constants = True, False, None, __name__, __main__

# Preprocessor directives (language-specific)
preprocessor = 

# Comments
line_comment = #
block_comment_start = """
block_comment_end = """

# String delimiters
string_delim = "
char_delim = '

# Preprocessor character (e.g., # for C preprocessor)
preprocessor_char = 

# Colors (optional - uses Monokai theme by default)
color_keyword = #e87d3e
color_type = #b4d273
color_string = #e5b567
color_comment = #797979
color_number = #9e86c8
color_preprocessor = #b05279
color_operator = #e87d3e
color_function = #6c99bb
```

#### Configuration Options

| Option | Description | Example |
|--------|-------------|---------|
| `language` | Language name (informational) | `python` |
| `extensions` | File extensions to match (comma-separated) | `.py, .pyw` |
| `keywords` | Control flow keywords (highlighted bold) | `if, else, for` |
| `statements` | Statement keywords and modifiers | `import, export, const` |
| `types` | Type keywords and built-in types | `int, str, void` |
| `constants` | Built-in constants | `true, false, NULL` |
| `preprocessor` | Preprocessor directive names | `define, include` |
| `line_comment` | Single-line comment delimiter | `//` or `#` |
| `block_comment_start` | Block comment start | `/*` or `"""` |
| `block_comment_end` | Block comment end | `*/` or `"""` |
| `string_delim` | String quote character | `"` |
| `char_delim` | Character quote | `'` |
| `preprocessor_char` | Preprocessor trigger character | `#` |

#### Color Customization (Optional)

You can customize colors using hex format (`#RRGGBB`) or RGB format (`R,G,B`):

```conf
# Hex format
color_keyword = #e87d3e
color_type = #b4d273

# RGB format (alternative)
color_keyword = 232,125,62
color_type = 180,210,115
```

Available color options:
- `color_keyword` - Control flow keywords
- `color_type` - Type names and type keywords
- `color_string` - String and character literals
- `color_comment` - Comments
- `color_number` - Numbers and constants
- `color_preprocessor` - Preprocessor directives
- `color_operator` - Operators (+, -, *, etc.)
- `color_function` - Function calls

If colors are not specified, CupidFM uses the default Monokai theme.

### Syntax Features

The syntax highlighter automatically detects and highlights:

- **Keywords** - Control flow statements (bold orange)
- **Types** - Type declarations and built-in types (green)
- **Strings** - String and character literals (yellow)
  - **Escape sequences** - Special highlighting for `\n`, `\t`, `\xHH`, etc. (bold purple)
- **Comments** - Single-line and block comments (gray)
- **Numbers** - Integer, floating-point, hex, binary, octal (purple)
- **Operators** - Arithmetic and logical operators (orange)
- **Function calls** - Functions followed by `(` (blue)
- **Labels** - Goto labels at line start (bold pink)
- **Constants** - ALL_CAPS identifiers (purple)
- **Type suffixes** - Identifiers ending with `_t` or `_T` (green)
- **Preprocessor** - Directives like `#include`, `#define` (pink)
- **Block comments across scrolling** - Maintains comment state when scrolling

### Tips for Writing Syntax Files

1. **Keep keyword lists manageable** - Very long comma-separated lists can cause performance issues. Keep each category under 50 items.

2. **Test with sample files** - Create a test file with various language constructs to verify highlighting works correctly.

3. **Order matters** - More specific patterns should come before general ones in your lists.

4. **Use comments** - Document your syntax file with comments starting with `#`.

5. **File extensions** - Include all common extensions (e.g., `.cpp`, `.cc`, `.cxx` for C++).

### Example: Simple Python Syntax

```conf
# Python Syntax Highlighting
language = python
extensions = .py, .pyw

keywords = if, else, elif, for, while, def, class, return
statements = import, from, with, as, pass
types = int, str, float, bool, list, dict, None
constants = True, False, None

line_comment = #
string_delim = "
char_delim = '

color_keyword = #e87d3e
color_type = #b4d273
color_string = #e5b567
color_comment = #797979
```

### Troubleshooting

- **Syntax file not loaded**: Ensure the file is in `~/.cupidfm/syntax/` and has the `.cupidconf` extension.
- **Colors not showing**: Check that your terminal supports 256 colors (`echo $TERM` should show `xterm-256color` or similar).
- **Highlighting incomplete**: Make sure all required fields are present (language, extensions, at least one keyword/type/statement).
- **Performance issues**: Reduce the number of keywords/types in your syntax file if hovering over files causes lag.

## Plugins (CupidScript)

CupidFM can load Cupidscript plugins (`.cs`) on startup.

By default it loads from your home directory:

1. `~/.cupidfm/plugins`
2. `~/.cupidfm/plugin`

Local/repo plugin folders are supported, but are disabled by default (to avoid accidentally executing repo scripts):

- Enable local plugin loading with: `CUPIDFM_LOAD_LOCAL_PLUGINS=1`
- Then CupidFM will also search:
  - `./cupidfm/plugins` and `./cupidfm/plugin`
  - `./plugins` and `./plugin`

### Plugin Hooks

- `fn on_load()`
- `fn on_key(key)` -> return `true` to consume the keypress
- `fn on_dir_change(new_cwd, old_cwd)`
- `fn on_selection_change(new_name, old_name)`

### CupidFM Script API

- `fm.notify(msg)` / `fm.status(msg)` - show a notification
- `fm.popup(title, msg)` - show a popup
- `fm.console_print(msg)` / `fm.console(msg)` - append to the in-app console (`^O` by default)
- `fm.prompt(title, initial)` -> `string|nil`
- `fm.confirm(title, msg)` -> `bool`
- `fm.menu(title, items[])` -> `index|-1`
- `fm.cwd()` - current directory
- `fm.selected_name()` / `fm.selected_path()` - current selection
- `fm.cursor()` / `fm.count()` - cursor index + list size
- `fm.search_active()` / `fm.search_query()` - fuzzy search state
- `fm.pane()` - `"directory"` or `"preview"`
- `fm.bind(key, func_name)` - bind a key to a function (key can be `"^T"`, `"F5"`, `"KEY_UP"`, or a numeric keycode)
- File operations (integrated with undo/redo):
  - `fm.copy(path_or_paths, dst_dir)`
  - `fm.move(path_or_paths, dst_dir)`
  - `fm.rename(path, new_name)`
  - `fm.delete(path_or_paths)` (trash)
  - `fm.mkdir(name_or_path)`
  - `fm.touch(name_or_path)`
  - `fm.undo()` / `fm.redo()`
- `fm.reload()` - request a directory reload
- `fm.exit()` - request CupidFM to quit
- `fm.cd(path)` - change directory (absolute or relative)
- `fm.select(name)` / `fm.select_index(i)` - move selection (best effort)
- `fm.key_name(code)` / `fm.key_code(name)` - convert between keycodes and names

See `plugins/examples/` for example scripts (not auto-loaded) and `CUPIDFM_CUPIDSCRIPT_API.md` for the full API reference.

## Todo

### High Priority
- [ ] Add file filtering options
- [ ] Add image preview (in house lib?)
- [ ] Write custom magic library for in-house MIME type detection

### Features
- [ ] Add a quick select feature for selecting file names, dir names, and current directory
- [X] Add configuration file support for customizing:
  - [X] Key bindings
  - [ ] Color schemes
  - [ ] Default text editor (using in house editor)
  - [ ] File associations
  - [ ] Change default text preview files
- [ ] Basic file dialog for web and other applications
- [ ] Use YSAP make-diagram program to learn more about files

### Todo List for Command Line Feature

- [ ] Design and implement the command bar UI. (Most likely in the same spot of notify)
- [ ] Design API functions for cupidscript to use with the commandline.
- [ ] Add a command parser to interpret user input.
- [ ] Implement core file operations (`cd`, `ls`, `open`, etc.).
- [ ] Add error handling and feedback messages.
- [ ] Support command history with Up/Down arrow keys.
- [ ] Implement tab-based auto-completion for file and directory names.
- [ ] Develop custom cupidfm commands (`tree`, `info`, etc.).
- [ ] Integrate with system shell commands.
- [ ] Allow user-defined aliases in a configuration file.

### Completed
- [X] Fallback to extension-based detection instead of MIME type when detection fails
- [X] Fix directory list not staying within the border
- [X] Implement directory tree preview for directories
- [X] Fix weird crash on different window resize
- [X] Fix text buffer from breaking the preview win border
- [X] Fix issue with title banner notif rotating showing char when rotating from left side to right
- [X] Fix inputs being overloaded and taking awhile to execute
- [X] Add build version and name display
- [X] Add cursor highlighting to text editing buffer
- [X] Add line numbers to text editing buffer
- [X] Fix preview window not updating on directory enter and leave
- [X] Implement proper file item list
- [X] Fix directory list being too big and getting cut off
- [X] Fix crashing when trying to edit too small of a file
- [X] Add support for sig winch handling
- [X] Fix being able to enter directory before calculation is done
- [X] Add directory window scrolling
- [X] Add tree structure visualization with proper icons and indentation
- [X] File info not using emojis
- [X] Add text display on tree preview when user enters an empty dir and on dir preview
- [X] Enable scrolling for tree preview in the preview window when tabbed over
- [X] Add preview support for `.zip` and `.tar` files - implemented via cupidarchive
- [X] Fix directory preview not scrolling 
- [X] Implement proper memory management and cleanup for file attributes and vectors
- [X] Add error handling for failed memory allocations
- [X] Optimize file loading performance for large directories
- [X] Optimize scrolling, also make sure tree preview is optimized 
- [?] Use tree command to rewrite tree preview
- [X] Fixed cursor issue in directory window scroll
- [X] Fix dir size calc not working (wont calc files inside)
- [X] Fix long preview file names
- [X] Add file operations:
  - [X] Copy/paste files and directories
  - [X] Create new file/directory
  - [X] Delete file/directory
  - [X] Rename file/directory
- [X] Display symbolic links with correct arrow notation (e.g., `->` showing the target path)
- [X] Basic install script for building, installing nerd fonts and other dependencies, and then moving the executable to /usr/bin
- [X] Implement file search functionality (fuzzy search)
- [X] Implement lazy loading for large directories
- [X] Optimize memory usage for file preview
- [X] Cache directory contents for faster navigation
- [X] Improve MIME type detection performance
- [X] Implement background loading for directory contents
- [X] Banner bug when its going lefct the fisrst tick it goes in it goes to the right one tick then back like normal 
- [X] Implement text editing shortcuts:
  - [X] Shift+arrow for selection
  - [X] Ctrl+arrow for faster cursor movement
  - [X] Standard shortcuts (Ctrl+X, Ctrl+C, Ctrl+V)
  - [X] Add undo (control Z) /redo (contriol Y) functionality in edit mode
  - [X] Implement proper text selection in edit mode (Currnety mouse selecting, will select line numbers )
- [X] Custom plugin system with cupidscript a custom scripting lang
- [X] Implement file/directory permissions editing
- [X] Implement syntax highlighting for supported file types (use config system like micro)
### Edit Mode Issues
- [X] Banner marquee not rotating correctly when rotating in edit mode
  - [X] Fix issue casued by patch, they are in seperate locations dpeedning on timing 
- [X] Fix banner not rotating when prompted eg. (new file or dir)
  - [X] Fix issue casued by patch, they are in seperate locations dpeedning on timing 
- [X] Fix sig winch handling breaking while in edit mode
- [X] Fix cursor showing up at the bottom of the text editing buffer
- [X] Fix text buffer not scrolling to the right when typing and hitting the border of the window

### Key Features to Implement

## Command Line Interface (CLI) Feature

### Overview

The **Command Line Interface (CLI)** for **cupidfm** will introduce a powerful way for users to perform common file operations directly from the application, similar to a terminal within the file manager. This feature will enable users to execute commands like navigating directories, opening files, copying/moving files, and even running system commands without leaving the **cupidfm** interface.

### Planned Features for the CLI

- **Command Input**: 
  - Users will have access to a bottom command bar where commands can be typed.
  - Basic commands like `cd`, `ls`, `open`, `copy`, `move`, `delete` will be supported.

- **Command History**:
  - Pressing the **Up/Down arrow keys** will cycle through previously executed commands, similar to a traditional terminal.

- **Tab Completion**:
  - Auto-complete file and directory names by pressing **TAB** while typing a command.

- **Error Handling**:
  - Clear and descriptive error messages will be displayed in the command bar when commands fail (e.g., "File not found" or "Permission denied").

- **Custom cupidfm Commands**:
  - Extend the functionality of traditional file operations with cupidfm-specific commands, such as:
    - `tree`: Display the directory tree structure.
    - `preview [file]`: Quickly open a file in the preview window.
    - `info [file/dir]`: Show detailed information about a file or directory.

- **System Command Integration**:
  - Run standard shell commands like `grep`, `find`, `chmod`, and others directly from the cupidfm command bar.

---

### Future Plans for File Operations Shortcuts 
- [X] **Notification on shortcut**
  - [ ] Convert the notfication bar to work with the command line
  - Ex. When a user enters command mode it will show up where the notifications does.

- [X] **Copy and Paste (Ctrl+C, Ctrl+V)**  
  - Copy selected file or directory.
  - Paste copied item into the current directory.

- [X] **Cut and Paste (Ctrl+X, Ctrl+V)**  
  - Move selected file or directory.
  - Paste cut item into the current directory.

- [X] **Delete (Ctrl+D)**
  - [X] Delete selected file or dir with no prompt
  - [X] Delete selected file or directory with a confirmation prompt.

- [X] **Rename (Ctrl+R)**  
  - Rename the selected file or directory.

- [X] **Create New File (Ctrl+N)**  
  - Create a new, empty file in the current directory.

- [X] **Create New Directory (Shift+N)**  
  - Create a new directory in the current directory.

- [X] **Select All (Ctrl+A)**  
  - Select all files and directories in the current view.

- [X] **File Search (Ctrl+F)**  
  - Search for files or directories by name or pattern.

- [X] **Quick File Info (Ctrl+T)**  
  - Display detailed information about the selected file or directory.

- [X] **Undo/Redo (Ctrl+Z / Ctrl+Y)**  
  - Undo or redo the last file operation.

- [X] **File Permissions (Ctrl+P)**  
  - Edit permissions of the selected file or directory.

- [ ] **Quick Move (F2)**  
  - Open a prompt to quickly move the selected file or directory to a specified path.

- [ ] **Batch Operations (Ctrl+Shift+B)**  
  - Perform batch operations like copying, moving, or deleting multiple selected files.

- [ ] **Symbolic Link Creation (Ctrl+L)**  
  - Create a symbolic link for the selected file or directory.

- [ ] **File Filtering (Ctrl+Shift+F)**  
  - Apply filters to display files by type, size, or modification date.

- [ ] **Open command bar (Ctrl+Shift+C)**
  - Lets users type in command bar
    
#### **1. Command Bar Design**
- [ ] Add a command bar at the bottom of the **cupidfm** interface.
- [ ] Display typed commands dynamically and update the UI to show results or error messages.

#### **2. Command Execution**
- [ ] Parse and interpret user input.
- [ ] Support basic file operations (`open`, `cd`, `ls`, `copy`, `move`, `delete`, etc.).
- [ ] Integrate with system utilities for advanced commands.

#### **3. Real-Time Feedback**
- [ ] Display real-time feedback or results in the command bar.
- [ ] Handle errors gracefully and inform users of invalid commands or paths.

#### **4. Custom Commands**
- [ ] Introduce cupidfm-specific commands for enhanced functionality, like:
  - `tree`
  - `preview`
  - `info`

#### **5. System Command Integration**
- [ ] Allow users to run basic shell commands without leaving the application.
- [ ] Commands like `grep` and `chmod` should work seamlessly.

#### **6. Configurable Aliases**
- [ ] Allow users to create command aliases for frequently used commands (e.g., alias `ls` to `list`).

---

## Usage

- **Navigation**:
  - **Up/Down**: Move between files
  - **Left/Right**: Navigate to parent/child directories
  - **F1**: Exit the application
  - **TAB**: Switch between directory and preview windows
  - **CONTROL+E**: Edit file in preview window
  - **CONTROL+G**: Save file while editing
  - **CONTROL+C**: Copy selected file to clipboard
  - **CONTORL+V**: Paste selected file to current location

## Contributing

Contributions are welcome! Please submit a pull request or open an issue for any changes.

## License

This project is licensed under the GNU General Public License v3.0 terms.
