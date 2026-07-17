#include <stdint.h>

#ifndef COMMAND_H
#define COMMAND_H

// Enumeration of all supported command types
typedef enum {
    INSERT,
    DEL,
    NEWLINE,
    HEADING,
    BOLD,
    ITALIC,
    BLOCKQUOTE,
    ORDERED_LIST,
    UNORDERED_LIST,
    CODE,
    HORIZONTAL_RULE,
    LINK
} CommandType;

// Node for lists
typedef struct command {
    CommandType type;    
    uint64_t pos;        // Single cursor position 
    uint64_t no_char;    // DEL
    uint64_t level;      // HEADING
    uint64_t start_pos;  // BOLD, ITALIC, CODE, LINK
    uint64_t end_pos;    // BOLD, ITALIC, CODE, LINK
    char    *content;    // INSERT
    char    *link;       // LINK
    char    *username;   
    char    *reject_msg;
    struct command *next; 
} command;

command *command_init(void);
void command_free(command *cmd);
void command_print(command *cmd);
void command_set_username(command *cmd, const char *username, const char *role);

#endif // COMMAND_H
