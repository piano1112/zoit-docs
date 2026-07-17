#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../libs/markdown.h"
#include "../libs/command.h"
#include "../libs/document.h"

// === Init and Free ===
document *markdown_init(void) {
    document *doc = document_create();
    if (doc) {
        doc->version = 0;
        doc->head = NULL;
        doc->commands = NULL;
    }
    return doc;
}

void markdown_free(document *doc) {
    document_destroy(doc);
}

// === Edit Commands ===
int markdown_insert(document *doc, uint64_t version, size_t pos, const char *content) {
    if (!doc || doc->version != version || !content) return -1;
    if (pos > document_length(doc)) return -1;
    command *cmd = command_init();
    cmd->type = INSERT;
    cmd->pos = pos;
    cmd->content = strdup(content);
    enqueue_command(doc, cmd);
    return 0;
}

int markdown_delete(document *doc, uint64_t version, size_t pos, size_t len) {
    if (!doc || doc->version != version) return -1;
    command *cmd = command_init();
    if (!cmd) return -1;
    cmd->type = DEL;
    cmd->pos = pos;
    cmd->no_char = len;
    enqueue_command(doc, cmd);
    return 0;
}

// === Formatting Commands ===
int markdown_newline(document *doc, size_t version, size_t pos) {
    if (!doc || doc->version != version) return -1;
    if (pos > document_length(doc)) return -1;
    command *cmd = command_init();
    cmd->type = NEWLINE;
    cmd->pos = pos;
    enqueue_command(doc, cmd);
    return 0;
}

int markdown_heading(document *doc, uint64_t version, size_t level, size_t pos) {
    if (!doc || doc->version != version || level < 1) return -1;
    command *cmd = command_init();
    cmd->type = HEADING;
    cmd->level = level;
    cmd->pos = pos;
    enqueue_command(doc, cmd);
    return 0;
}

int markdown_bold(document *doc, uint64_t version, size_t start, size_t end) {
    if (!doc || doc->version != version) return -1;
    command *cmd = command_init();
    cmd->type = BOLD;
    cmd->start_pos = start;
    cmd->end_pos = end;
    enqueue_command(doc, cmd);
    return 0;
}

int markdown_italic(document *doc, uint64_t version, size_t start, size_t end) {
    if (!doc || doc->version != version) return -1;
    command *cmd = command_init();
    cmd->type = ITALIC;
    cmd->start_pos = start;
    cmd->end_pos = end;
    enqueue_command(doc, cmd);
    return 0;
}

int markdown_blockquote(document *doc, uint64_t version, size_t pos) {
    if (!doc || doc->version != version) return -1;
    command *cmd = command_init();
    cmd->type = BLOCKQUOTE;
    cmd->pos = pos;
    enqueue_command(doc, cmd);
    return 0;
}

int markdown_ordered_list(document *doc, uint64_t version, size_t pos) {
    if (!doc || doc->version != version) return -1;
    command *cmd = command_init();
    cmd->type = ORDERED_LIST;
    cmd->pos = pos;
    enqueue_command(doc, cmd);
    return 0;
}

int markdown_unordered_list(document *doc, uint64_t version, size_t pos) {
    if (!doc || doc->version != version) return -1;
    command *cmd = command_init();
    cmd->type = UNORDERED_LIST;
    cmd->pos = pos;
    enqueue_command(doc, cmd);
    return 0;
}

int markdown_code(document *doc, uint64_t version, size_t start, size_t end) {
    if (!doc || doc->version != version) return -1;
    command *cmd = command_init();
    cmd->type = CODE;
    cmd->start_pos = start;
    cmd->end_pos = end;
    enqueue_command(doc, cmd);
    return 0;
}

int markdown_horizontal_rule(document *doc, uint64_t version, size_t pos) {
    if (!doc || doc->version != version) return -1;
    command *cmd = command_init();
    cmd->type = HORIZONTAL_RULE;
    cmd->pos = pos;
    enqueue_command(doc, cmd);
    return 0;
}

int markdown_link(document *doc, uint64_t version, size_t start, size_t end, const char *url) {
    if (!doc || doc->version != version || !url) return -1;
    command *cmd = command_init();
    cmd->type = LINK;
    cmd->start_pos = start;
    cmd->end_pos = end;
    cmd->link = strdup(url);
    enqueue_command(doc, cmd);
    return 0;
}

// === Utilities ===
void markdown_print(const document *doc, FILE *stream) {
    if (!doc || !stream) return;
    for (chunk *c = doc->head; c; c = c->next) {
        fputs(c->text, stream);
    }
}

char *markdown_flatten(const document *doc) {
    if (!doc) return NULL;
    // compute total length
    size_t total = document_length(doc);
    char *out = malloc(total + 1);
    if (!out) return NULL;
    char *p = out;
    for (chunk *c = doc->head; c; c = c->next) {
        memcpy(p, c->text, c->length);
        p += c->length;
    }
    out[total] = '\0';
    return out;
}

// === Versioning ===
void markdown_increment_version(document *doc) {
    if (!doc) return;
    document_apply_edit(doc);
    puts(markdown_flatten(doc));
    fflush(stdout);
}

int parse_command(document *doc, char *text) {
    // Duplicate input for tokenization
    char *buf = strdup(text);
    if (!buf) return -1;

    // Trim trailing newline or carriage return
    size_t len = strlen(buf);
    if (len > 0 && (buf[len-1] == '\n' || buf[len-1] == '\r'))
        buf[--len] = '\0';

    // Tokenize on whitespace
    char *saveptr;
    char *token = strtok_r(buf, " \t", &saveptr);
    if (!token) {
        free(buf);
        return -1;
    }
    char *arg1 = strtok_r(NULL, " \t", &saveptr);
    uint64_t i_arg1 = strtoull(arg1, NULL, 10);

    // Identify command keyword
    if (strcmp(token, "INSERT") == 0) {
        char *content = NULL;;
        if (saveptr && *saveptr) {
            content = strdup(saveptr);
        } else {
            content = strdup("");
        }
        markdown_insert(doc, doc->version, i_arg1, content);

    } else if (strcmp(token, "DEL") == 0) {
        char *arg2 = strtok_r(NULL, " \t", &saveptr);
        uint64_t no_char = strtoull(arg2, NULL, 10);
        markdown_delete(doc, doc->version, i_arg1, no_char);

    } else if (strcmp(token, "BOLD") == 0) {
        char *arg2 = strtok_r(NULL, " \t", &saveptr);
        uint64_t pos_end = strtoull(arg2, NULL, 10);
        markdown_bold(doc, doc->version, i_arg1, pos_end);

    } else if (strcmp(token, "ITALIC") == 0) {
        char *arg2 = strtok_r(NULL, " \t", &saveptr);
        uint64_t pos_end = strtoull(arg2, NULL, 10);
        markdown_italic(doc, doc->version, i_arg1, pos_end);

    } else if (strcmp(token, "NEWLINE") == 0) {
        markdown_newline(doc, doc->version, i_arg1);

    } else if (strcmp(token, "HEADING") == 0) {
        char *arg2 = strtok_r(NULL, " \t", &saveptr);
        uint64_t pos = strtoull(arg2, NULL, 10);
        markdown_heading(doc, doc->version, i_arg1, pos);

    } else if (strcmp(token, "UNORDERED_LIST") == 0) {
        markdown_unordered_list(doc, doc->version, i_arg1);

    } else if (strcmp(token, "BLOCKQUOTE") == 0) {
        markdown_blockquote(doc, doc->version, i_arg1);

    } else if (strcmp(token, "CODE") == 0) {
        char *arg2 = strtok_r(NULL, " \t", &saveptr);
        uint64_t pos_end = strtoull(arg2, NULL, 10);
        markdown_code(doc, doc->version, i_arg1, pos_end);

    } else if (strcmp(token, "HORIZONTAL_RULE") == 0) {
        markdown_horizontal_rule(doc, doc->version, i_arg1);

    } else if (strcmp(token, "ORDERED_LIST") == 0) {
        markdown_ordered_list(doc, doc->version, i_arg1);

    } else if (strcmp(token, "LINK") == 0) {
        char *arg2 = strtok_r(NULL, " \t", &saveptr);
        uint64_t pos_end = strtoull(arg2, NULL, 10);
        char *url = strdup(saveptr);
        markdown_link(doc, doc->version, i_arg1, pos_end, url);
        free(url);

    } else {
        free(buf);
        return -1;
    }

    free(buf);

    return 0;
}