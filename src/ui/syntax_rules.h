#ifndef SYNTAX_RULES_H
#define SYNTAX_RULES_H
#include <stdbool.h>
typedef struct SyntaxRules SyntaxRules;
struct cupidconf;
// Compile from a borrowed configuration; the caller retains ownership.
SyntaxRules *syntax_rules_from_config(struct cupidconf *conf);
SyntaxRules *syntax_rules_load(const char *path);
void syntax_rules_free(SyntaxRules *rules);
const char *syntax_rules_name(const SyntaxRules *rules);
bool syntax_rules_detect(const SyntaxRules *rules, const char *kind, const char *text);
int syntax_rules_line(const SyntaxRules *rules, const char *line, int state, unsigned char *colors);
#endif
