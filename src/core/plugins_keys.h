// plugins_keys.h
// Internal key name/code helpers extracted from plugins.c.
#ifndef PLUGINS_KEYS_H
#define PLUGINS_KEYS_H

// keycode_to_name: converts a keycode to CupidScript key name (e.g. "^C", "F1").
// buf must be at least 32 bytes.
const char *plugins_keycode_to_name_local(int keycode, char buf[32]);

// parse_key_name: parses key name strings like "^C", "^_S", "KEY_UP", "F1".
// Returns keycode, or -1 on failure.
int plugins_parse_key_name_local(const char *s);

#endif // PLUGINS_KEYS_H

