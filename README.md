# Zoit Docs

A real-time collaborative markdown editor written in C. Multiple clients connect to a shared server, edit the same document concurrently, and receive updates at configurable intervals — all through POSIX IPC, with no external dependencies.

Built as part of COMP2017 (Systems Programming) at the University of Sydney.

## How It Works

The **server** holds a shared markdown document in memory using a custom chunk-based linked list. When a client connects, the server spawns a dedicated thread to handle its commands. Edits from all clients are queued, applied to the document, and periodically broadcast back to connected clients.

**Clients** register with the server via POSIX real-time signals (`SIGRTMIN`), then communicate through named FIFOs (one per direction). Each client receives the current document state on connect and can issue editing commands for the rest of the session.

### Key Features

- **Concurrent editing**: Multiple clients edit simultaneously, each handled in its own pthread
- **Role-based access control**: Users are assigned `read` or `write` permissions via a roles file — write commands from read-only users are rejected
- **Markdown formatting**: Insert, delete, bold, italic, headings (H1–H3), ordered/unordered lists, code spans, blockquotes, horizontal rules, and links
- **Version tracking**: Each batch of edits increments the document version, enabling conflict detection
- **Persistent output**: The final document is saved as `doc.md` on server shutdown

## Architecture

```
source/
  server.c      → Main server: signal handling, client threads, broadcast loop
  client.c      → Client process: FIFO setup, command input, server reader thread
  document.c    → Chunk-based document model (linked list with split/merge)
  command.c     → Command parsing, printing, and lifecycle
  markdown.c    → Markdown-aware editing API (insert, delete, format)
libs/
  document.h    → Document and chunk data structures
  command.h     → Command types and struct definition
  markdown.h    → Public API for markdown operations
```

## Building and Running

### Prerequisites

- GCC with pthreads support
- POSIX-compliant system (Linux / macOS)

### Build

```bash
make
```

### Start the Server

```bash
./server <update_interval_ms>
```

The server prints its PID and waits for client connections. The update interval (in milliseconds) controls how frequently queued edits are applied and broadcast.

### Connect a Client

```bash
./client <server_pid> <username>
```

The username must appear in `roles.txt` or the connection is rejected. Once connected, type editing commands directly:

```
INSERT 0 Hello world
HEADING 1 0
BOLD 0 4
DISCONNECT
```

### Roles File

Define user permissions in `roles.txt`:

```
daniel write
ryan read
yao read
```

### Shutdown

Type `QUIT` in the server terminal. The server waits for all clients to disconnect, then saves the document to `doc.md`.

## Technical Details

- **IPC via named FIFOs**: Each client gets two FIFOs (`FIFO_C2S_<pid>` and `FIFO_S2C_<pid>`) for bidirectional communication, created and cleaned up automatically
- **Signal-driven registration**: Clients send `SIGRTMIN` to the server to initiate connection; the server responds with `SIGRTMIN+1` after creating the FIFOs
- **Chunk-based document**: Text is stored as a linked list of chunks that can be split and merged at arbitrary positions, enabling efficient mid-document insertions and deletions without copying the entire buffer
- **Thread safety**: All document mutations and client list modifications are protected by pthread mutexes
