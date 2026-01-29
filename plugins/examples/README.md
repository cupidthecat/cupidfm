# Example Plugins

These example plugins are not loaded by default because CupidFM only loads `*.cs` directly
inside `./plugins` (it does not recurse into subfolders).

Examples in this folder:
- api_demo.cs: entries/search/navigation helpers
- async_ui_demo.cs: async prompt/confirm/menu
- toolkit.cs: common file ops menu
- weather_open_meteo.cs: HTTPS weather popup via Open-Meteo

To try one:

1) Copy the plugin (and its `lib/` folder if used) into `~/.cupidfm/plugins/`
2) Start `./cupidfm`