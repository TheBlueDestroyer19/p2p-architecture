# P2P File Transfer System

**Version:** `v1.1.0`  
**Language:** C  
**Protocol:** TCP/IP  
**Platform:** Linux

A C-based file transfer system built using TCP sockets and a custom application-layer protocol. The project focuses on reliable file transmission, data integrity, concurrent client handling, and safe resource management.

Version 1.1.0 introduces a **thread-per-client server architecture**, allowing multiple clients to transfer files concurrently while maintaining independent transfer states.

---

## 1. Features

### Core functionality
- TCP-based client-server communication using POSIX sockets.
- Custom binary application-layer protocol.
- Length-prefixed packet format for reliable message framing over TCP.
- Handling of partial socket reads and writes.
- Support for file transfers using fixed-size data chunks.
- SHA-256-based file integrity verification using OpenSSL.
- Unique transfer identifiers for individual file transfers.
- Transaction-based naming of received files to avoid filename collisions.
- Safe file creation and cleanup.

### Concurrent server (v1.1.0)
- Thread-per-client architecture.
- Supports up to **5 simultaneously connected clients**.
- Each client is handled by an independent worker thread.
- Multiple clients can transfer files concurrently.
- Independent transfer state for every client.
- Main server thread manages connection slots and worker lifecycle.
- Completed workers are joined before their connection slots are reused.

### Graceful shutdown
- Handles `SIGINT` for controlled server shutdown.
- Stops admitting new clients once shutdown begins.
- Immediately disconnects idle clients.
- Allows active transfers to finish within a 60-second grace period.
- Forcefully terminates remaining connections after the grace period.
- Cleans up incomplete transfers and worker resources.

---

## 2. Architecture

The project is organized into two independent codebases: the client and the server.

```text
p2p/
├── client/
│   ├── include/
│   ├── src/
│   └── ...
│
├── server/
│   ├── include/
│   ├── src/
│   └── ...
│
├── README.md
└── USER.md
```

The client and server maintain separate source files, headers, and build configurations.

### Server architecture

The server uses a thread-per-client model.

```text
                  ┌──────────────────┐
                  │   Main Server    │
                  │      Thread      │
                  └────────┬─────────┘
                           │
                     Accept Clients
                           │
                  ┌────────▼─────────┐
                  │ Connection Table │
                  │   Max: 5 Clients │
                  └────────┬─────────┘
                           │
             ┌─────────────┼─────────────┐
             │             │             │
        ┌────▼────┐   ┌────▼────┐   ┌────▼────┐
        │ Worker 1│   │ Worker 2│   │ Worker 3│
        └────┬────┘   └────┬────┘   └────┬────┘
             │             │             │
        Client 1      Client 2      Client 3
```

**Main server thread responsibilities:**
- Initialize the listening socket.
- Accept incoming connections.
- Allocate available connection slots.
- Create worker threads for accepted clients.
- Monitor worker completion notifications.
- Join completed workers and reclaim their slots.
- Coordinate server shutdown.

**Worker thread responsibilities:**
- Handle the entire lifetime of one client connection.
- Process multiple sequential file transfers.
- Maintain independent transfer state and file receiver resources.
- Verify received file data.
- Close its client socket when finished.
- Notify the main thread when its execution completes.

Each worker owns its transfer state, preventing concurrent clients from interfering with one another.

### Connection management

A fixed-size connection table tracks the lifecycle of each client.

Each slot transitions through three states:

| State | Description |
|---|---|
| `SLOT_FREE` | Available for a new connection |
| `SLOT_RUNNING` | Worker is handling a client |
| `SLOT_FINISHED` | Worker has completed and awaits reclamation |

A mutex protects shared connection-table state.

A non-blocking notification pipe wakes the main server thread when a worker finishes. The main thread then joins the worker and releases its slot.

This ensures that a connection slot is never reused before its previous worker has been joined.

---

## 3. Communication Protocol

TCP provides a reliable, ordered byte stream, but it does not preserve application-level message boundaries.

To address this, the project implements a custom length-prefixed packet protocol.

### Packet structure

Every packet contains a fixed-size 19-byte header.

| Field | Size | Description |
|---|---:|---|
| Version | 1 byte | Protocol version |
| Message Type | 2 bytes | Identifies packet purpose |
| Transfer ID | 8 bytes | Identifies a file transfer |
| Sequence Number | 4 bytes | Identifies packet order |
| Payload Length | 4 bytes | Size of the payload |

All multi-byte header fields use big-endian byte order.

The payload follows the header and can contain metadata, file data, acknowledgements, or integrity information.

### Message types

| Message | Value | Purpose |
|---|---:|---|
| `MSG_DATA` | `0x0001` | Carries file data |
| `MSG_ACK` | `0x0002` | Acknowledges a packet |
| `MSG_ERROR` | `0x0003` | Represents a transfer error |
| `MSG_TRANSFER_START` | `0x0004` | Initiates a file transfer |
| `MSG_TRANSFER_END` | `0x0005` | Signals completion of a transfer |
| `MSG_DISCONNECT` | `0x0006` | Requests connection termination |

### Transfer sequence

A file transfer follows this sequence:

1. Client generates a unique 64-bit transfer ID.
2. Client sends `MSG_TRANSFER_START` containing file metadata.
3. Server validates the metadata and initializes the receiver.
4. Server acknowledges the transfer start.
5. Client sends file data in chunks.
6. Server writes received chunks and acknowledges successful processing.
7. Client sends `MSG_TRANSFER_END` containing the SHA-256 digest.
8. Server verifies the expected file size and digest.
9. Server synchronizes and closes the received file.
10. Server acknowledges successful completion.

The protocol uses **stop-and-wait acknowledgement**: the sender waits for an ACK before sending the next packet.

Sequence numbers begin at zero for each transfer, with the transfer-start packet using sequence number zero.

---

## 4. File Integrity and Storage

### SHA-256 verification

The project uses OpenSSL's EVP interface to calculate SHA-256 hashes.

- Client calculates the digest while reading the source file.
- Server independently calculates the digest while receiving and writing data.
- Client sends the final 32-byte digest in the transfer-end packet.
- Server compares both digests before acknowledging successful completion.

This verifies that the received file matches the transmitted data.

**Note:** SHA-256 provides integrity verification, not authentication or protection against malicious modification of the communication.

### File storage

Received files are stored in:

```text
p2p_recieved/
```

The server generates a unique stored filename by appending the transfer ID before the final file extension.

Examples:

```text
report.pdf
    → report_00000000a1b2c3d4.pdf

archive.tar.gz
    → archive.tar_00000000a1b2c3d4.gz

README
    → README_00000000a1b2c3d4

.env
    → .env_00000000a1b2c3d4
```

The original filename is retained in the transfer metadata.

The server uses safe file-creation operations, including `openat()` with `O_EXCL` and `O_NOFOLLOW`, to prevent accidental overwrites and avoid following symbolic links.

Incomplete files are removed when a transfer fails or the connection terminates unexpectedly.

---

## 5. Graceful Server Shutdown

The server supports controlled shutdown through `SIGINT` (`Ctrl+C`).

The shutdown procedure is designed to avoid abruptly interrupting active transfers whenever possible.

### Shutdown sequence

1. Server receives the shutdown signal.
2. Main thread stops admitting new clients.
3. Idle client connections are shut down immediately.
4. Active transfers are allowed to complete.
5. Workers exit after completing their current transfers.
6. The server waits for workers to finish and joins them.
7. If the 60-second grace period expires, remaining client sockets are shut down.
8. Incomplete transfers are cleaned up.
9. Server releases its resources and terminates.

New transfers are rejected once shutdown begins, even if a client already has an established connection.

The shutdown deadline is measured using a monotonic clock to avoid issues caused by changes to system time.

---

## 6. Building the Project

### Prerequisites

- Linux environment
- GCC compiler
- POSIX threads
- OpenSSL development libraries

On Debian/Ubuntu:

```bash
sudo apt update
sudo apt install build-essential libssl-dev
```

### Build the client

```bash
cd client
gcc -Wall -Wextra -Wpedantic -Iinclude src/*.c -o client -lcrypto
```

### Build the server

```bash
cd server
gcc -Wall -Wextra -Wpedantic -pthread -Iinclude src/*.c -o server -lcrypto
```

---

## 7. Running the Project

Start the server first:

```bash
cd server
./server
```

Then, from another terminal:

```bash
cd client
./client
```

The current local testing configuration uses:

```c
#define SERVER_IP   "127.0.0.1"
#define SERVER_PORT 8080
```

For testing across different machines, configure the server to bind to an appropriate network interface and configure the client to connect to the server's reachable IP address.

---

## 8. Testing

The implementation has been tested across the following scenarios:

- Single-client file transfer.
- Multiple simultaneous client connections.
- Concurrent file transfers.
- Maximum client limit enforcement.
- Connection-slot reuse after client disconnection.
- Multiple transfers over the same connection.
- Transfers involving identical original filenames.
- Client disconnection during an active transfer.
- Graceful shutdown with idle clients.
- Graceful shutdown during active transfers.
- Mixed idle and transferring clients during shutdown.
- Forced shutdown after the grace period.
- Cleanup of incomplete files.

File size and SHA-256 digest verification are used to validate successful transfers.

---

## 9. Version History

### v1.1.0 — Concurrent Multi-Client Server

- Introduced thread-per-client server architecture.
- Added support for up to five simultaneous client connections.
- Implemented connection-slot lifecycle management.
- Added worker completion notification using a non-blocking pipe.
- Added mutex-based synchronization for shared connection state.
- Implemented graceful shutdown with a 60-second transfer completion window.
- Added rejection of new transfers during shutdown.
- Introduced transaction-ID-based stored filenames.
- Improved cleanup of incomplete transfers and worker resources.

### v1.0.0 — Initial Stable Release

- Implemented TCP client-server file transfer.
- Introduced custom binary packet protocol.
- Added packet serialization and deserialization.
- Implemented partial send/receive handling.
- Added transfer IDs and sequence numbers.
- Implemented stop-and-wait acknowledgements.
- Added SHA-256 file integrity verification.
- Implemented safe file storage and incomplete-transfer cleanup.
- Supported sequential client connections.

---

## 10. Future Roadmap

Potential directions for future development:

| Version | Planned Enhancement |
|---|---|
| `v1.2.0` | Sliding-window protocol and pipelined transfers |
| `v1.3.0` | Interrupted transfer resumption |
| `v1.4.0` | TLS encryption and client authentication |
| `v2.0.0` | Peer discovery, direct peer-to-peer communication, and NAT traversal |

These are proposed future enhancements, not features currently implemented.

---

## License

Specify the project's license here.

