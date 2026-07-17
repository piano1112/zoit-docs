#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "../libs/command.h"

command *command_init(void){
    command *cmd = malloc(sizeof(command));
    if (!cmd) return NULL;
    cmd->content = NULL;
    cmd->link = NULL;
    cmd->next = NULL;
    cmd->username = NULL;
    cmd->reject_msg = NULL;
    return cmd;
}
    
void command_free(command *cmd) {
    if (!cmd) return;
    if (cmd->content) free(cmd->content);
    if (cmd->link) free(cmd->link);
    if (cmd->username) free(cmd->username);
    if (cmd->reject_msg) free(cmd->reject_msg);
    free(cmd);
}

void command_print(command *cmd) {
    if (!cmd) return;
    printf("EDIT %s ", cmd->username);
    switch (cmd->type) {
        case INSERT:
            printf("INSERT %lu %s", cmd->pos, cmd->content);
            break;
        case DEL:
            printf("DEL %lu %lu", cmd->pos, cmd->no_char);
            break;
        case NEWLINE:
            printf("NEWLINE %lu", cmd->pos);
            break;
        case HEADING:
            printf("HEADING %lu %lu", cmd->level, cmd->pos);
            break;
        case BOLD:
            printf("BOLD %lu %lu", cmd->start_pos, cmd->end_pos);
            break;
        case ITALIC:
            printf("ITALIC %lu %lu", cmd->start_pos, cmd->end_pos);
            break;
        case UNORDERED_LIST:
            printf("UNORDERED_LIST %lu", cmd->pos);
            break;
        case BLOCKQUOTE:
            printf("BLOCKQUOTE %lu", cmd->pos);
            break;
        case CODE:
            printf("CODE %lu %lu", cmd->start_pos, cmd->end_pos);
            break;
        case HORIZONTAL_RULE:
            printf("HORIZONTAL_RULE %lu", cmd->pos);
            break;
        case ORDERED_LIST:
            printf("ORDERED_LIST %lu", cmd->pos);
            break;
        case LINK:
            printf("LINK %lu %lu %s", cmd->start_pos, cmd->end_pos, cmd->link);
            break;
        default:
            break;
    }
    if (cmd->reject_msg) {
        printf(" %s\n", cmd->reject_msg);
    } else {
        printf(" SUCCESS\n");
    }
    fflush(stdout);
}

void command_set_username(command *cmd, const char *username, const char *reject_msg) {
    if (!cmd || !username) return;
    // set username of last command in the list
    command *last = cmd;
    while (last->next) {
        last = last->next;
    }
    if (last->username) free(last->username);
    last->username = strdup(username);
    if (!last->username) {
        perror("strdup");
        return;
    }
    if (reject_msg) {
        last->reject_msg = strdup(reject_msg);
        if (!last->reject_msg) {
            perror("strdup");
            return;
        }
    } 
}