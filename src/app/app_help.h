#ifndef APP_HELP_H
#define APP_HELP_H
const char *app_help_vim(void);
const char *app_help_setup(void);
const char *app_help_tabs(void);
const char *app_help_prefix(int key);
/* Case-insensitive subsequence match; smaller scores rank first, -1 means no match. */
int app_help_fuzzy_score(const char *query, const char *text);
#endif
