#ifndef CMD_H
#define CMD_H

/*
 * Slash command handler for the REPL.
 *
 * Returns 1 if input was handled as a command, 0 if not a command.
 */
int cmd_dispatch(const char *input);

#endif
