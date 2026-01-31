// plugins_editor_api.h
// Internal editor-related fm.* native registrations extracted from plugins.c.
#ifndef PLUGINS_EDITOR_API_H
#define PLUGINS_EDITOR_API_H

#include "plugins_internal.h"

typedef struct cs_vm cs_vm;

void plugins_register_editor_api(cs_vm *vm, PluginManager *pm);

#endif // PLUGINS_EDITOR_API_H

