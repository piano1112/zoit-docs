// Tests for applying several edits made against the same document version.
// Every command in one batch uses positions from the version the clients saw,
// so later commands must be shifted by the inserts applied before them.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../libs/markdown.h"

static int failures = 0;

// Build a document whose current version contains `base`.
static document *make_doc(const char *base) {
    document *doc = markdown_init();
    if (base[0] != '\0') {
        markdown_insert(doc, doc->version, 0, base);
        markdown_increment_version(doc);
    }
    return doc;
}

static void expect(const char *name, document *doc, const char *want) {
    markdown_increment_version(doc);
    char *got = markdown_flatten(doc);
    if (strcmp(got, want) == 0) {
        fprintf(stderr, "PASS  %s\n", name);
    } else {
        fprintf(stderr, "FAIL  %s\n      want: \"%s\"\n      got:  \"%s\"\n", name, want, got);
        failures++;
    }
    free(got);
    markdown_free(doc);
}

int main(void) {
    document *doc;

    // Two inserts at different places: the second is shifted by the first.
    doc = make_doc("Hello world");
    markdown_insert(doc, doc->version, 5, ",");
    markdown_insert(doc, doc->version, 11, "!");
    expect("insert then insert after it", doc, "Hello, world!");

    // Two inserts at the same place: arrival order decides.
    doc = make_doc("");
    markdown_insert(doc, doc->version, 0, "A");
    markdown_insert(doc, doc->version, 0, "B");
    expect("same position keeps arrival order", doc, "AB");

    // An insert before a delete shifts the delete range.
    doc = make_doc("Hello world");
    markdown_insert(doc, doc->version, 0, "Hi ");
    markdown_delete(doc, doc->version, 5, 6);
    expect("insert then delete after it", doc, "Hi Hello");

    // A delete before an insert does not move the insert.
    doc = make_doc("Hello world");
    markdown_delete(doc, doc->version, 0, 6);
    markdown_insert(doc, doc->version, 11, "!");
    expect("delete then insert after it", doc, "world!");

    // A delete over text where someone inserted keeps the inserted text.
    doc = make_doc("abcdef");
    markdown_insert(doc, doc->version, 3, "XX");
    markdown_delete(doc, doc->version, 1, 4);
    expect("delete around an earlier insert", doc, "aXXf");

    // Two deletes over the same text delete it once.
    doc = make_doc("abcdef");
    markdown_delete(doc, doc->version, 1, 3);
    markdown_delete(doc, doc->version, 2, 3);
    expect("overlapping deletes", doc, "af");

    // Formatting positions are shifted too.
    doc = make_doc("Hello world");
    markdown_insert(doc, doc->version, 0, ">> ");
    markdown_bold(doc, doc->version, 0, 5);
    expect("insert then bold", doc, ">> **Hello** world");

    // An insert right at the end of a bold range stays outside it.
    doc = make_doc("Hello world");
    markdown_insert(doc, doc->version, 5, ",");
    markdown_bold(doc, doc->version, 0, 5);
    expect("insert at bold end stays outside", doc, "**Hello**, world");

    // Inserts after a bold are shifted by both of its markers.
    doc = make_doc("Hello world");
    markdown_bold(doc, doc->version, 0, 5);
    markdown_insert(doc, doc->version, 11, "!");
    expect("bold then insert after it", doc, "**Hello** world!");

    // Link markers and URL are counted as inserted text.
    doc = make_doc("see docs now");
    markdown_link(doc, doc->version, 4, 8, "x.io");
    markdown_insert(doc, doc->version, 12, ".");
    expect("link then insert after it", doc, "see [docs](x.io) now.");

    // Heading adds a newline and marker before the position.
    doc = make_doc("intro title");
    markdown_heading(doc, doc->version, 1, 6);
    markdown_insert(doc, doc->version, 11, "!");
    expect("heading then insert after it", doc, "intro \n# title!");

    if (failures) {
        fprintf(stderr, "%d test(s) failed\n", failures);
        return 1;
    }
    fprintf(stderr, "all tests passed\n");
    return 0;
}
