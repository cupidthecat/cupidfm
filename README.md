# cupidfm - file editing

cupidfm is a terminal-based file manager implemented in C. It uses the `ncurses` library for the user interface, providing features like directory navigation, directory tree preview, file preview, file editing, and file information display. 

![preview](img/preview2.png)

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

## Prerequisites

To build and run cupidfm, you must have the following packages installed:

- **A C Compiler & Build Tools** (e.g. `gcc`, `make`)
- **ncurses** development libraries (for terminal handling)
- **libmagic** development libraries (for MIME type detection)
- **xclip** (for clipboard support)

### Installing Dependencies on Ubuntu/Debian

Open a terminal and run:

```bash
sudo apt update
sudo apt install build-essential libncurses-dev libmagic-dev xclip
```

### Installing Dependencies on Arch Linux

Open a terminal and run:

```bash
sudo pacman -Syu
sudo pacman -S base-devel ncurses file xclip
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
- Scrollable preview window
- Tab-based window switching between directory and preview panes
- Configure keybinds

## Configuration

### Default Keybindings

cupidfm comes with a set of **default keybindings**. On **first run**, if cupidfm cannot find a user configuration file, it will **auto-generate** one at:

```
~/.cupidfmrc
```

Below is a screenshot showing the start up

![preview](img/startup.png)

This auto-generated config file includes default bindings, for example:

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

edit_up=KEY_UP
edit_down=KEY_DOWN
edit_left=KEY_LEFT
edit_right=KEY_RIGHT
edit_save=^G
edit_quit=^Q
edit_backspace=KEY_BACKSPACE
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
## Todo

### High Priority
- [X] Fix directory preview not scrolling 
- [ ] Write custom magic library for in-house MIME type detection
- [ ] Implement proper memory management and cleanup for file attributes and vectors
- [ ] Add error handling for failed memory allocations
- [ ] Optimize file loading performance for large directories
- [ ] Optimize scrolling
- [ ] Use tree command to rewrite tree preview
- [ ] Use YSAP make-diagram program to learn more about files

### Edit Mode Issues
- [ ] Banner marquee not rotating correctly when rotating in edit mode
- [ ] Fix banner not rotating when prompted eg. (new file or dir)
- [ ] Fix sig winch handling breaking while in edit mode
- [ ] Fix cursor showing up at the bottom of the text editing buffer
- [ ] Fix text buffer not scrolling to the right when typing and hitting the border of the window

### Features
- [ ] Fix dir size calc not working (wont calc files inside)
- [ ] Enable scrolling for tree preview in the preview window when tabbed over
- [ ] Add preview support for `.zip` and `.tar` files
- [ ] Implement syntax highlighting for supported file types
- [ ] Display symbolic links with correct arrow notation (e.g., `->` showing the target path)
- [ ] Implement text editing shortcuts:
  - [ ] Shift+arrow for selection
  - [ ] Ctrl+arrow for faster cursor movement
  - [ ] Standard shortcuts (Ctrl+X, Ctrl+C, Ctrl+V)
  - [ ] Add undo/redo functionality in edit mode
  - [ ] Implement proper text selection in edit mode
- [X] Add file operations:
  - [X] Copy/paste files and directories
  - [X] Create new file/directory
  - [X] Delete file/directory
  - [X] Rename file/directory
- [ ] Add a quick select feature for selecting file names, dir names, and current directory
- [ ] Implement file search functionality
- [ ] Add file filtering options
- [ ] Implement file/directory permissions editing
- [X] Add configuration file support for customizing:
  - [X] Key bindings
  - [ ] Color schemes
  - [ ] Default text editor (using in house editor)
  - [ ] File associations
  - [ ] Change default text preview files
- [ ] Add image preview
- [ ] Basic file dialog for web and other applications
- [ ] Basic install script for building, installing nerd fonts and other dependencies, and then moving the executable to /usr/bin

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

### Usage Examples

#### 1. **Navigating Directories**
```bash
> cd ~/Documents
```
- Changes the current working directory to `~/Documents`.

#### 2. **Listing Files**
```bash
> ls
```
- Lists all files and directories in the current working directory.

#### 3. **Opening a File**
```bash
> open notes.txt
```
- Opens `notes.txt` in the **preview window** or the **edit mode**.

#### 4. **Copying Files**
```bash
> copy file1.txt ~/Documents/backup/
```
- Copies `file1.txt` to the `~/Documents/backup/` directory.

#### 5. **Moving Files**
```bash
> move file1.txt ~/Downloads/
```
- Moves `file1.txt` to the `~/Downloads/` directory.

#### 6. **Deleting a File**
```bash
> delete file1.txt
```
- Deletes `file1.txt` with a confirmation prompt.

#### 7. **Directory Tree View**
```bash
> tree
```
- Displays the directory structure of the current folder in the **preview window**.

#### 8. **Searching for Files**
```bash
> find "*.c"
```
- Finds all `.c` files in the current directory and its subdirectories.

#### 9. **Checking File Information**
```bash
> info file1.txt
```
- Displays details like size, permissions, and modification time for `file1.txt`.

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

- [ ] **Create New Directory (Shift+N)**  
  - Create a new directory in the current directory.

- [ ] **Select All (Ctrl+A)**  
  - Select all files and directories in the current view.

- [ ] **File Search (Ctrl+F)**  
  - Search for files or directories by name or pattern.

- [ ] **Quick File Info (Ctrl+I)**  
  - Display detailed information about the selected file or directory.

- [ ] **Undo/Redo (Ctrl+Z / Ctrl+Y)**  
  - Undo or redo the last file operation.

- [ ] **File Permissions (Ctrl+P)**  
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

### Todo List for Command Line Feature

- [ ] Design and implement the command bar UI.
- [ ] Add a command parser to interpret user input.
- [ ] Implement core file operations (`cd`, `ls`, `open`, etc.).
- [ ] Add error handling and feedback messages.
- [ ] Support command history with Up/Down arrow keys.
- [ ] Implement tab-based auto-completion for file and directory names.
- [ ] Develop custom cupidfm commands (`tree`, `info`, etc.).
- [ ] Integrate with system shell commands.
- [ ] Allow user-defined aliases in a configuration file.

### Performance Improvements
- [ ] Implement lazy loading for large directories
- [ ] Optimize memory usage for file preview
- [ ] Cache directory contents for faster navigation
- [ ] Improve MIME type detection performance
- [ ] Implement background loading for directory contents

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
