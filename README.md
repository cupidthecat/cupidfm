# CupidFM

CupidFM is a terminal-based file manager implemented in C, using the `ncurses` library for the user interface. It provides features like directory navigation, file preview, and displaying file information.

## Features

- Navigate directories using arrow keys
- View file details and preview supported file types
- Command-line interface with basic file operations

## Prerequisites

To build and run CupidFM, you need the following dependencies installed:

- `gcc` (GNU Compiler Collection)
- `make` (build automation tool)
- `ncurses` library for terminal handling

### Installing Dependencies on Ubuntu

Run the following command to install the necessary packages:

```bash
sudo apt update
sudo apt install build-essential libncurses-dev
```

## Building the Project

To compile the project, run:

```bash
./dev.sh
```

This script will use `make` with predefined flags to compile the source code and produce an executable named `cupidfm`.

### Compilation Flags

The script uses several compilation flags:

- `-Wall -Wextra -pedantic`: Enable warnings
- `-Wshadow -Werror -Wstrict-overflow`: Additional strictness for code
- `-fsanitize=address -fsanitize=undefined`: Enable sanitization for debugging

## Running the Program

After compilation, you can run the program:

```bash
./cupidfm
```

This will start CupidFM. Error logs will be saved in `log.txt`.

## File Structure

- `src/`: Contains the source code files
- `dev.sh`: Script for compiling the project
- `Makefile`: Used by `make` for the build process
- `LICENSE`, `README.md`: Documentation and license information

## Usage

Use the arrow keys to navigate the directory structure:
- **Up/Down**: Move between files
- **Left/Right**: Navigate to parent/child directories
- **F1**: Exit the application
- **TAB**: Change between preview and directory window
  - **CONTROL+E**: While in the preview window you can edit within the file and save with
  - **CONTROL+S**: Save files you are editing within the terminal 

## Contributing

Contributions are welcome! Please submit a pull request or open an issue for any changes.

## License

This project is licensed under the GNU General Public License v3.0 terms.
