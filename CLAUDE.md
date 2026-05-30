# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build & Run

```bash
make                    # build with g++ -std=c++17 -pthread -g -O0
make clean              # remove binary, .o, .d files
./server <port> <processes> <threads>
# e.g. ./server 8888 4 2
```

No test suite exists. Manual testing requires a custom TCP client that speaks the binary frame protocol.

## Architecture

The server uses a **multi-process + thread-pool** model:

- **Main process** (`main.cpp`): creates the listening socket, fork()s N room child processes, starts M accept threads, and uses `select()` on pipes from each child to track room state changes. The `Manager` class (`unpthread.h`) maps business room numbers (100001+) to internal array indices and tracks per-room member counts.

- **Accept threads** (`thread_main.cpp`): M threads share the listening socket behind a mutex. On accept, each reads the first frame — `CREATE_MEETING` assigns the connection as room owner, `JOIN_MEETING` routes it as a member. Fd passing to room processes uses `write_fd`/`recv_fd` via Unix domain sockets with `SCM_RIGHTS`.

- **Room processes** (`room.cpp`): each child runs `process_main()` which spawns one `accept_fd` thread (receives new fds from the main process) plus 5 `send_func` threads (drain the send queue and broadcast to targets). The main loop is `epoll_wait` on all member fds in the room.

### Key data flow

1. Client connects → accept thread reads first frame
2. Main process finds/creates room via `Manager`, sends fd + command to the room process's Unix socket
3. Room process `accept_fd` thread receives the fd, adds it to the room's epoll set
4. Room event loop reads messages from members, converts SEND→RECV types, pushes to `SEND_QUEUE`
5. Send threads pop from queue, serialise frames, broadcast to targets (everyone except sender, or single target for responses)

### Critical conventions

- **Error handling**: wrapper functions (`Pthread_create`, `Accept`, `Socketpair`, etc.) call `err_quit()` on failure, which logs and `exit(1)`s. Non-fatal errors use `err_msg()`. There are no graceful degradation paths — almost all syscall failures are fatal.
- **Network byte order**: all multi-byte fields on the wire are network byte order. `ntohs`/`htonl` at boundaries.
- **Fd lifecycle**: the main process `close()`s fds immediately after passing them to a room process. Room processes own member fds thereafter. Owner exit triggers `clear()` which closes all member fds and notifies the main process via pipe.
- **Global state**: `Manager* manager`, `int listenfd`, and `pthread_t* tptr` are shared globals declared `extern` across `main.cpp`, `room.cpp`, and `thread_main.cpp`. The room's `room_manager` and `SEND_QUEUE` are also file-scope globals in `room.cpp`.

## Protocol

Custom binary framing: `$` (1B) + type (2B, network order) + ip (4B) + payload_len (4B, network order) + payload + `#` (1B).

`MsgType` enum in `net.h` defines all message types. SEND types (0,2,4) are client→server; RECV types (1,3,5) are the corresponding server→client broadcasts. Response/notification types are 20–24.

## File map

| File | Role |
|------|------|
| `main.cpp` | Entry point, process/thread orchestration, `select` loop for room state |
| `thread_main.cpp` | Accept threads, `CREATE_MEETING`/`JOIN_MEETING` dispatch |
| `room.cpp` | Room child process: epoll loop, frame dispatch, send threads, `room_manager` class |
| `net.cpp` / `net.h` | Socket helpers, fd-passing (`write_fd`/`recv_fd`), `readn`/`writen`, protocol types |
| `msg.h` | `MSG` struct and `SEND_QUEUE` (blocking producer-consumer queue) |
| `unpthread.cpp` / `unpthread.h` | Pthread wrapper functions + `Manager` class (room lifecycle tracking) |
| `error.cpp` / `error.h` | `err_quit` and `err_msg` logging |
| `signal.cpp` / `signal_util.h` | `sigaction`-based `Signal()` wrapper + `SIGCHLD` handler |
