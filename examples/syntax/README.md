# Syntax examples

Open these files in CupidFM to inspect foreground colors, multiline regions, escapes, and
embedded languages. Highlighting uses the terminal background.

Each language example has expected token colors. corpus.txt combines the examples; its
expectations cover all 158 definitions. Run the checks with make test from the repository root.
The JSON files record one color-pair ID per Unicode character. They test highlighting behavior,
rather than terminal screenshots or font rendering.
