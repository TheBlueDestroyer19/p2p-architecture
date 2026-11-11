# P2P File Transfer System

A lightweight peer-to-peer file transfer system implemented in C using POSIX TCP sockets. The project focuses on understanding network programming, application-layer protocol design, reliable data transfer, binary file handling, and data integrity verification.

The client and server are maintained as **two independent codebases**. They communicate using a common application protocol implemented separately on both sides.

## Features

- TCP-based client-server communication.
- Custom application-layer binary protocol.
- Length-prefixed packet framing.
- Structured file metadata exchange.
- Transfer identification using unique 64-bit IDs.
- Sequence-number-based packet ordering and validation.
- Stop-and-wait acknowledgement mechanism.
- Binary file transfer with support for large files.
- SHA-256 integrity verification using OpenSSL.
- Safe filename handling and protection against accidental overwrites.
- Graceful connection termination and server shutdown.
- Sequential handling of multiple client connections.

## Architecture

The system follows a layered design:

```text
                  CLIENT
                    |
          +---------------------+
          |    Sender Module   |
          +---------------------+
                    |
          +---------------------+
          |   Transfer Module  |
          +---------------------+
                    |
          +---------------------+
          |     Packet Layer   |
          +---------------------+
                    |
          +---------------------+
          |    Network Layer   |
          +---------------------+
                    |
             TCP Connection
                    |
          +---------------------+
          |    Network Layer   |
          +---------------------+
                    |
          +---------------------+
          |     Packet Layer   |
          +---------------------+
                    |
          +---------------------+
          |   Transfer Module  |
          +---------------------+
                    |
          +---------------------+
          |  File Receiver     |
          +---------------------+
                    |
               SERVER
```

### Client

The client is responsible for:

1. Establishing a TCP connection with the server.
2. Accepting a local file path from the user.
3. Extracting the filename and determining the file size.
4. Generating a unique transfer ID.
5. Sending file metadata.
6. Reading the file in chunks and transmitting them.
7. Waiting for acknowledgements before proceeding.
8. Computing the SHA-256 digest of the transmitted content.
9. Sending the final digest and waiting for transfer confirmation.

### Server

The server is responsible for:

1. Listening for incoming TCP connections.
2. Accepting a client connection.
3. Validating incoming packets and transfer state.
4. Extracting and validating file metadata.
5. Creating the destination file safely.
6. Writing received binary chunks to disk.
7. Computing an independent SHA-256 digest.
8. Verifying the final file size and digest.
9. Acknowledging successful transfers.
10. Cleaning up incomplete transfers and closing connections.

## Application-Layer Protocol

TCP provides a reliable, ordered byte stream, but it does not preserve application-level message boundaries.

To address this, the project implements a custom **length-prefixed packet protocol**.

Every packet consists of:

```text
+---------------------------+
|       Packet Header       |  19 bytes
+---------------------------+
|                           |
|       Payload             |  Variable length
|                           |
+---------------------------+
```

### Packet Header

The header has a fixed size of 19 bytes.

| Field | Size | Description |
|---|---:|---|
| Version | 1 byte | Protocol version |
| Message Type | 2 bytes | Identifies the packet type |
| Transfer ID | 8 bytes | Identifies the active transfer |
| Sequence Number | 4 bytes | Identifies packet order |
| Payload Length | 4 bytes | Number of payload bytes |

All multibyte integer fields are encoded in **big-endian byte order**.

The header is serialized and deserialized manually. Raw C structures are never transmitted over the network, avoiding problems caused by compiler padding, alignment, and host byte order.

Maximum payload size: **65,536 bytes**.

### Message Types

| Message | Value | Purpose |
|---|---:|---|
| `MSG_DATA` | `0x0001` | Carries a file data chunk |
| `MSG_ACK` | `0x0002` | Acknowledges a packet |
| `MSG_ERROR` | `0x0003` | Represents an error |
| `MSG_TRANSFER_START` | `0x0004` | Initiates a file transfer |
| `MSG_TRANSFER_END` | `0x0005` | Marks the end of a transfer |
| `MSG_DISCONNECT` | `0x0006` | Terminates the connection |

## Data Structures

The following structures represent the principal data exchanged and maintained by the system.

### 1. PacketHeader

Represents the fixed-size header of every protocol packet.

```c
typedef struct {
    uint8_t version;
    uint16_t type;
    uint64_t transfer_id;
    uint32_t sequence_number;
    uint32_t payload_length;
} PacketHeader;
```

### 2. Packet

Combines a packet header with its dynamically allocated payload.

```c
typedef struct {
    PacketHeader header;
    uint8_t *payload;
} Packet;
```

The payload is dynamically allocated when receiving a packet and released using `free_packet()`.

### 3. FileMetadata

Contains the information required to initiate a file transfer.

```c
#define MAX_FILENAME_LENGTH 255
#define METADATA_FIXED_SIZE 10

typedef struct {
    char filename[MAX_FILENAME_LENGTH + 1];
    uint64_t file_size;
} FileMetadata;
```

Metadata is transmitted as the payload of `MSG_TRANSFER_START`.

#### Metadata wire format

The metadata is serialized into a variable-length byte sequence:

| Field | Size | Description |
|---|---:|---|
| Filename Length | 2 bytes | Length of filename in bytes |
| File Size | 8 bytes | Original file size |
| Filename | Variable | Filename encoded as UTF-8 |

The total metadata payload length is:

\[
10 + \text{filename length}
\]

For example, a file named `report.pdf` with a size of 4096 bytes produces the following logical metadata:

```text
Filename Length : 10
File Size       : 4096
Filename        : report.pdf
```

The filename is transmitted as a basename, not as an absolute local path.

The receiver validates the filename and rejects empty names, path separators, special directory names, and control characters.

### 4. TransferState

Maintains the state of an ongoing transfer.

```c
typedef enum {
    TRANSFER_IDLE,
    TRANSFER_ACTIVE
} TransferStatus;

typedef struct {
    TransferStatus status;
    uint64_t transfer_id;
    uint32_t next_sequence_number;
} TransferState;
```

The transfer state ensures that packets belong to the correct transfer and arrive in the expected order.

### 5. FileReceiver (Server)

Maintains the state required to receive and verify a file.

```c
typedef struct {
    int directory_fd;
    int file_fd;
    uint64_t expected_size;
    uint64_t received_size;
    char filename[MAX_FILENAME_LENGTH + 1];
    EVP_MD_CTX *hash_ctx;
    int active;
} FileReceiver;
```

The server uses this structure to manage file creation, disk writes, received byte counts, and incremental SHA-256 computation.

## Transfer Lifecycle

A file transfer follows this sequence:

```text
CLIENT                                  SERVER
  |                                       |
  |------ TRANSFER_START (seq = 0) ------>|
  |<------------- ACK (seq = 0) ----------|
  |                                       |
  |--------- DATA (seq = 1) ------------->|
  |<------------- ACK (seq = 1) ----------|
  |                                       |
  |--------- DATA (seq = 2) ------------->|
  |<------------- ACK (seq = 2) ----------|
  |                                       |
  |                 ...                   |
  |                                       |
  |--------- TRANSFER_END --------------->|
  |        (SHA-256 digest)                |
  |<------------- ACK --------------------|
  |                                       |
  |            Transfer Complete          |
  |                                       |
```

### Sequence Number Convention

A single sequence-number space is maintained throughout a transfer.

| Packet | Sequence Number |
|---|---:|
| `TRANSFER_START` | 0 |
| First `DATA` | 1 |
| Second `DATA` | 2 |
| Subsequent `DATA` | Incremented |
| `TRANSFER_END` | Next expected sequence |

An ACK echoes the transfer ID and sequence number of the packet being acknowledged.

The sender advances its sequence number only after receiving a valid ACK.

### Stop-and-Wait Mechanism

The implementation uses a stop-and-wait transmission strategy.

For every packet:

1. The sender transmits the packet.
2. The sender waits for an acknowledgement.
3. The receiver validates and processes the packet.
4. The receiver sends an ACK only after successful processing.
5. The sender proceeds to the next packet.

This simplifies synchronization and packet-order validation, although it limits throughput on high-latency networks.

## File Integrity Verification

The project uses **SHA-256** through OpenSSL's EVP interface.

Both sides independently compute the digest:

- The client updates its hash as file chunks are transmitted.
- The server updates its hash as chunks are successfully written to disk.
- At the end of the transfer, the client sends its 32-byte digest in `MSG_TRANSFER_END`.
- The server finalizes its digest and compares it with the received digest.

The server acknowledges the final packet only if:

1. The received file size matches the expected size.
2. The computed SHA-256 digest matches the supplied digest.
3. File synchronization and closing succeed.

This detects accidental corruption or inconsistencies during transfer. SHA-256 verification alone does not authenticate the sender or protect against an active attacker who can modify both the file and its digest.

## File Storage and Safety

Received files are stored in:

```text
p2p_recieved/
```

The receiver uses directory-relative file operations and flags such as:

- `O_EXCL` to prevent overwriting existing files.
- `O_NOFOLLOW` to prevent following symbolic links.
- `openat()` to anchor file creation to the destination directory.

If a transfer fails or is interrupted before completion, the incomplete destination file is removed.

## Current Limitations

Version 1.0.0 intentionally focuses on a reliable basic transfer implementation.

- One active client is handled at a time.
- Multiple clients may wait in the TCP listen backlog.
- Transfers use stop-and-wait rather than a sliding window.
- Interrupted transfers cannot be resumed.
- TLS encryption and client authentication are not implemented.
- NAT traversal and automatic peer discovery are not implemented.

## Technology Stack

- **Language:** C
- **Networking:** POSIX TCP sockets
- **Synchronization:** TCP reliability and application-level acknowledgements
- **Integrity:** OpenSSL SHA-256 EVP API
- **Build:** GCC
- **Platform:** Linux / POSIX-compatible environments

## Version

**Current Release: v1.0.0**

The initial release provides end-to-end binary file transfer with metadata exchange, sequence validation, acknowledgements, and SHA-256 integrity verification.

See [USER.md](USER.md) for installation, configuration, and usage instructions.
