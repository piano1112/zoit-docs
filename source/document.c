#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "../libs/document.h"

chunk *chunk_create(const char *text, size_t length) {
    chunk *c = malloc(sizeof(chunk));
    if (!c) return NULL;
    c->text = malloc(length + 1);
    if (!c->text) {
        free(c);
        return NULL;
    }
    if (text && length > 0) {
        memcpy(c->text, text, length);
    }
    c->text[length] = '\0';
    c->length = length;
    c->next = NULL;
    c->marked_for_deletion = 0;
    return c;
}

void chunk_free(chunk *c) {
    if (!c) return;
    free(c->text);
    free(c);
}

document *document_create(void) {
    document *doc = malloc(sizeof(document));
    if (!doc) return NULL;
    doc->head = NULL;
    doc->version = 0;
    return doc;
}

void document_destroy(document *doc) {
    if (!doc) return;
    chunk *c = doc->head;
    while (c) {
        chunk *next = c->next;
        chunk_free(c);
        c = next;
    }
    free(doc);
}

size_t document_length(const document *doc) {
    size_t len = 0;
    for (chunk *c = doc->head; c; c = c->next) {
        len += c->length;
    }
    return len;
}

chunk *chunk_split(chunk *c, size_t local_offset) {
    if (!c || local_offset == 0 || local_offset >= c->length) {
        // nothing to split or split at ends
        return c->next;
    }
    size_t new_len = c->length - local_offset;
    // create new chunk for tail part
    chunk *newc = chunk_create(c->text + local_offset, new_len);
    if (!newc) return NULL;
    // shrink original chunk text
    char *old_text = malloc(local_offset + 1);
    if (!old_text) {
        chunk_free(newc);
        return NULL;
    }
    memcpy(old_text, c->text, local_offset);
    old_text[local_offset] = '\0';
    free(c->text);
    c->text = old_text;
    c->length = local_offset;
    // insert newc after c
    newc->next = c->next;
    c->next = newc;
    if (c->marked_for_deletion) {
        newc->marked_for_deletion = 1;
    }
    return newc;
}

void document_append_chunk(document *doc, chunk *c) {
    if (!doc || !c) return;
    if (!doc->head) {
        doc->head = c;
    } else {
        chunk *last = doc->head;
        while (last->next) {
            last = last->next;
        }
        last->next = c;
    }
}

int document_insert_text(document *doc, size_t pos, const char *text) {
    if (!doc || !text) return -1;

    size_t text_len = strlen(text);
    size_t idx = 0;
    chunk *prev = NULL;
    chunk *c = doc->head;

    // find insertion point
    while (c && idx + c->length <= pos) {
        idx += c->length;
        prev = c;
        c = c->next;
    }
    size_t local = pos > idx ? pos - idx : 0;

    // create new chunk for text
    chunk *newc = chunk_create(text, text_len);
    if (!newc) return -1;

    // insert at end
    if (!c) {
        document_append_chunk(doc, newc);
        return 0;
    }

    // insert before chunk start
    if (local == 0) {
        if (prev) {
            prev->next = newc;
        } else {
            doc->head = newc;
        }
        newc->next = c;
    }

    // insert in middle via split
    else {
        chunk *second = chunk_split(c, local);
        if (!second && local < c->length) {
            // split failed
            chunk_free(newc);
            return -1;
        }
        newc->next = second;
        c->next = newc;
    }

    return 0;
}

int document_delete_range(document *doc, size_t pos, size_t len) {
    if (!doc || len == 0) return 0;

    size_t total = document_length(doc);
    if (pos > total) return -1;
    size_t end_pos = pos + len;
    if (end_pos > total) end_pos = total;
    chunk *start = doc->head;
    size_t idx = 0;

    // locate start
    while (start && idx + start->length <= pos) {
        idx += start->length;
        start = start->next;
    }
    if (!start) return 0; // nothing to delete
    size_t local_start = pos > idx ? pos - idx : 0;

    // locate end boundary
    chunk *end = doc->head;
    size_t idx2 = 0;
    while (end && idx2 + end->length <= end_pos) {
        idx2 += end->length;
        end = end->next;
    }
    size_t local_end = end_pos > idx2 ? end_pos - idx2 : 0;

    // split at start
    if (local_start > 0) {
        chunk_split(start, local_start);
        if (start == end) {
            end = start->next;
            local_end = local_end - local_start;
        }
        start = start->next;
    }

    // split at end
    chunk *after = NULL;
    if (end) {
        if (local_end > 0) {
            chunk_split(end, local_end);
            after = end->next;
        } else {
            after = end;
        }
    } 

    // mark chunks from start up to before after
    chunk *c = start;
    while (c && c != after) {
        chunk *next = c->next;
        c->marked_for_deletion = 1;
        c = next;
    }
    return 0;
}

void enqueue_command(document *doc, command *cmd) {
    if (!doc->commands) {
        doc->commands = cmd;
    } else {
        command *last = doc->commands;
        while (last->next) {
            last = last->next;
        }
        last->next = cmd;
    }
}

void document_apply_edit(document *doc) {
    // Apply the edit command to the document
    for (command *cmd = doc->commands; cmd; cmd = cmd->next) {
        if (cmd->reject_msg) {
            // Skip rejected commands
            continue;
        }
        char *read = NULL;
        switch (cmd->type) {
            case INSERT:
                document_insert_text(doc, cmd->pos, cmd->content);
                break;
            case DEL:
                document_delete_range(doc, cmd->pos, cmd->no_char);
                break;
            case BOLD:
                document_insert_text(doc, cmd->start_pos, "**");
                document_insert_text(doc, cmd->end_pos + 2, "**");
                break;
            case ITALIC:
                document_insert_text(doc, cmd->start_pos, "*");
                document_insert_text(doc, cmd->end_pos + 1, "*");
                break;
            case NEWLINE:
                document_insert_text(doc, cmd->pos, "\n");
                break;
            case HEADING:
                // ensure preceding newline
                read = NULL;
                if (cmd->pos > 0) read = document_read(doc, cmd->pos - 1, 1);
                if (read && read[0] != '\n') {
                    document_insert_text(doc, cmd->pos, "\n");
                    cmd->pos++;
                }
                // insert heading
                if (cmd->level == 1) {
                    document_insert_text(doc, cmd->pos, "# ");
                } else if (cmd->level == 2) {
                    document_insert_text(doc, cmd->pos, "## ");
                } else if (cmd->level == 3) {
                    document_insert_text(doc, cmd->pos, "### ");
                }
                break;
            case UNORDERED_LIST:
                // ensure preceding newline
                read = NULL;
                if (cmd->pos > 0) read = document_read(doc, cmd->pos - 1, 1);
                if (read && read[0] != '\n') {
                    document_insert_text(doc, cmd->pos, "\n");
                    cmd->pos++;
                }
                // insert unordered list
                document_insert_text(doc, cmd->pos, "- ");
                break;
            case ORDERED_LIST:
                // ensure preceding newline
                read = NULL;
                if (cmd->pos > 0) read = document_read(doc, cmd->pos - 1, 1);
                if (read && read[0] != '\n') {
                    document_insert_text(doc, cmd->pos, "\n");
                    cmd->pos++;
                }
                // insert ordered list
                // format "n. "
                int order = determine_order(doc, cmd->pos);
                if (order <= 0) {
                    order = 1;
                }
                char order_str[16];
                snprintf(order_str, sizeof(order_str), "%d. ", order);
                document_insert_text(doc, cmd->pos, order_str);
                break;
            case CODE:
                // insert code block
                document_insert_text(doc, cmd->start_pos, "`");
                document_insert_text(doc, cmd->end_pos + 1, "`");
                break;
            case HORIZONTAL_RULE:
                // ensure preceding newline
                read = NULL;
                if (cmd->pos > 0) read = document_read(doc, cmd->pos - 1, 1);
                if (read && read[0] != '\n') {
                    document_insert_text(doc, cmd->pos, "\n");
                    cmd->pos++;
                }
                // insert horizontal rule
                document_insert_text(doc, cmd->pos, "---\n");
                break;
            case BLOCKQUOTE:
                // ensure preceding newline
                read = NULL;
                if (cmd->pos > 0) read = document_read(doc, cmd->pos - 1, 1);
                if (read && read[0] != '\n') {
                    document_insert_text(doc, cmd->pos, "\n");
                    cmd->pos++;
                }
                // insert blockquote
                document_insert_text(doc, cmd->pos, "> ");
                break;
            case LINK:
                // insert link
                document_insert_text(doc, cmd->start_pos, "[");
                document_insert_text(doc, cmd->end_pos + 1, "](");
                // insert link URL
                document_insert_text(doc, cmd->end_pos + 3, cmd->link);
                document_insert_text(doc, cmd->end_pos + 3 + strlen(cmd->link), ")");
                break;
            default:
                break;
        }
    }
    document_delete_marked(doc);
    // Free the command after applying
    command *next_cmd;
    for (command *cmd = doc->commands; cmd; cmd = next_cmd) {
        next_cmd = cmd->next;
        command_print(cmd);
        command_free(cmd);
    }
    doc->commands = NULL;
    doc->version++;
}

char *document_read(document *doc, size_t pos, size_t len) {
    if (!doc) return NULL;
    char *out = malloc(len + 1);
    if (!out) return NULL;
    size_t idx = 0;
    size_t total = document_length(doc);
    if (pos > total) {
        free(out);
        return NULL;
    }
    chunk *c = doc->head;
    while (c && idx + c->length <= pos) {
        idx += c->length;
        c = c->next;
    }
    if (!c) {
        free(out);
        return NULL;
    }
    size_t local = pos > idx ? pos - idx : 0;
    size_t read_len = 0;
    while (c && read_len < len) {
        size_t to_copy = c->length - local;
        if (read_len + to_copy > len) {
            to_copy = len - read_len;
        }
        memcpy(out + read_len, c->text + local, to_copy);
        read_len += to_copy;
        local = 0;
        c = c->next;
    }
    out[read_len] = '\0';
    return out;
}

void document_delete_marked(document *doc) {
    if (!doc) return;
    // unlink marked chunks and free them
    chunk *prev = NULL;
    chunk *c = doc->head;
    while (c) {
        if (c->marked_for_deletion) {
            chunk *next = c->next;
            if (prev) {
                prev->next = next;
            } else {
                doc->head = next;
            }
            chunk_free(c);
            c = next;
        } else {
            prev = c;
            c = c->next;
        }
    }
}

int determine_order(document *doc, size_t pos) {
    if (!doc || pos == 0) return 1;
    // Read text before pos and find the last "n. " pattern after a newline
    char *text = document_read(doc, 0, pos);
    if (!text) return 1;
    int prev_order = 0;
    for (size_t i = 0; i < pos; i++) {
        // "n. " pattern must be at start of text or after a newline
        if (i == 0 || text[i - 1] == '\n') {
            char *endptr;
            long val = strtol(text + i, &endptr, 10);
            if (endptr != text + i && *endptr == '.' &&
                (size_t)(endptr - text) + 1 < pos && *(endptr + 1) == ' ') {
                prev_order = (int)val;
            }
        }
    }
    free(text);
    return prev_order + 1;
}
