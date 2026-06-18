#ifndef SKILLS_H
#define SKILLS_H

#include <stddef.h>

/* Ensure .agent/skills exists. */
int skills_init(void);

/* Build a compact prompt manifest of available skills. Caller frees. */
char *skills_build_prompt(void);

/* Load full .agent/skills/<name>/SKILL.md. Caller frees. */
char *skills_load(const char *name, char *err, size_t err_cap);

#endif
