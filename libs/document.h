#include <stdint.h>
#include "command.h"

#ifndef DOCUMENT_H
#define DOCUMENT_H
/**
 * This file is the header file for all the document functions. You will be tested on the functions inside markdown.h
 * You are allowed to and encouraged multiple helper functions and data structures, and make your code as modular as possible. 
 * Ensure you DO NOT change the name of document struct.
 */

// Node in linked list structure
typedef struct chunk{
    char *text; // dynamically allocated 
    size_t length;          
    struct chunk *next; 
    int marked_for_deletion;   
} chunk;

// Linked list structure holding chunks
typedef struct {
    chunk *head;            
    uint64_t version;
    command *commands; 
} document;

// Functions from here onwards.

/**
 * Allocate and initialize a new chunk with given text and length.
 * Returns a pointer to the new chunk, or NULL on failure.
 */
chunk *chunk_create(const char *text, size_t length);

/**
 * Free a single chunk's allocated memory.
 */
void chunk_free(chunk *c);

/**
 * Create and initialize an empty document.
 * Returns a pointer to the document, or NULL on failure.
 */
document *document_create(void);

/**
 * Free all memory associated with a document, including its chunks.
 */
void document_destroy(document *doc);

/**
 * Split a chunk at the given local_offset, so that the original chunk keeps
 * [0, local_offset), and a new chunk contains [local_offset, end).
 * Returns pointer to the new chunk, or NULL on failure.
 */
chunk *chunk_split(chunk *c, size_t local_offset);

/**
 * Append a chunk to the end of the document's chunk list.
 */
void document_append_chunk(document *doc, chunk *c);

/**
 * Insert text at a given document position by possibly splitting chunks
 * and inserting a new chunk.
 * Returns 0 on success, -1 on failure.
 */
int document_insert_text(document *doc, size_t pos, const char *text);

/**
 * Mark a range [pos, pos+len) of characters from the document for 
 * deletion, possibly crossing multiple chunks.
 * Returns 0 on success, -1 on failure.
 */
int document_delete_range(document *doc, size_t pos, size_t len);

/**
 * Get the total length of the document in characters.
 */
size_t document_length(const document *doc);

void enqueue_command(document *doc, command *cmd);

void document_apply_edit(document *doc);

char *document_read(document *doc, size_t pos, size_t len);

void document_delete_marked(document *doc);

int determine_order(document *doc, size_t pos);

#endif
