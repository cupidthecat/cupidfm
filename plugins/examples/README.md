# Example Plugins

These example plugins are not loaded by default because CupidFM only loads `*.cs` directly
inside `./plugins` (it does not recurse into subfolders).

Examples in this folder:
- api_demo.cs: entries/search/navigation helpers
- async_ui_demo.cs: async prompt/confirm/menu
- toolkit.cs: common file ops menu
- weather_open_meteo.cs: HTTPS weather popup via Open-Meteo
- editor_find_replace_demo.cs: find and replace functionality using `fm.editor_replace_text` API
- editor_delete_operations_demo.cs: text deletion operations using `fm.editor_delete_range` API (word deletion, line deletion, etc.)
- editor_text_manipulation_demo.cs: timestamp insertion and uppercase conversion using `fm.editor_uppercase_selection` API
- editor_content_preview.cs: preview editor content
- editor_cursor_demo.cs: demonstrate cursor position API
- editor_get_lines_demo.cs: demonstrate line retrieval API
- editor_line_count_demo.cs: show line count
- editor_line_peek.cs: peek at specific lines
- editor_path_check.cs: check editor file path
- editor_selection_demo.cs: demonstrate selection API
- editor_state_check.cs: check if editor is active
- bulk_ops.cs: bulk file operations

To try one:

1) Copy the plugin (and its `lib/` folder if used) into `~/.cupidfm/plugins/`
2) Start `./cupidfm`