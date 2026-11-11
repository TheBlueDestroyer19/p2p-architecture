# User Guide — P2P File Transfer System

This document explains how to build, configure, and use the P2P File Transfer System.

The project consists of two independent repositories:

- **Client repository:** Establishes connections and sends files.
- **Server repository:** Listens for connections and receives files.

Both repositories must be cloned and built independently.

## 1. Prerequisites

The following tools and libraries are required:

| Requirement | Purpose |
|---|---|
| GCC | Compiling C source files |
| POSIX-compatible OS | Socket and file APIs |
| OpenSSL development library | SHA-256 implementation |
| Git | Cloning the repositories |

### Ubuntu / Debian

Install the required dependencies:

```bash
sudo apt update
sudo apt install build-essential libssl-dev git
```

Verify GCC:

```bash
gcc --version
```

## 2. Clone the Repositories

Clone the client and server repositories separately:

```bash
git clone https://github.com/TheBlueDestroyer19/p2p-architecture
```

Enter the appropriate repository when building or running either component.

## 3. Configuration

### Server Configuration

Open the server configuration header and configure the listening address and port.

```c
#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 8080
#define SERVER_BACKLOG 5
```

- `SERVER_IP`: Address on which the server listens.
- `SERVER_PORT`: TCP port used by the application.
- `SERVER_BACKLOG`: Maximum number of pending connection requests in the listen queue.

For testing on the same machine, `127.0.0.1` is sufficient.

For communication across different machines, configure the server to listen on an appropriate network interface. Ensure that the firewall permits incoming TCP connections on the selected port.

### Client Configuration

Configure the client with the server's IP address and port.

For local testing:

```text
IP Address : 127.0.0.1
Port       : 8080
```

For remote communication, use the server machine's reachable IP address.

## 4. Build Instructions

### Build the Server

From the server repository root:

```bash
gcc -Wall -Wextra -Wpedantic -Iinclude src/*.c -o server -lcrypto
```

### Build the Client

From the client repository root:

```bash
gcc -Wall -Wextra -Wpedantic -Iinclude src/*.c -o client -lcrypto
```

The `-lcrypto` flag links the OpenSSL cryptographic library.

If compilation succeeds, the executable will be generated in the repository root.

## 5. Running the Application

### Step 1: Start the Server

From the server repository:

```bash
./server
```

The server starts listening for incoming connections.

### Step 2: Start the Client

From the client repository:

```bash
./client
```

The client attempts to establish a TCP connection with the configured server.

Once connected, it prompts for a file path.

### Step 3: Send a File

Enter the path of the file you want to transfer.

Example:

```text
Enter file path: /home/user/Documents/report.pdf
```

The client then:

1. Reads the file metadata.
2. Initiates a transfer.
3. Sends the file in chunks.
4. Waits for acknowledgements.
5. Computes the SHA-256 digest.
6. Sends the final digest for verification.

The server receives and stores the file in:

```text
p2p_recieved/
```

The original filename is retained.

**Note:** The server does not overwrite an existing file with the same name. If the destination filename already exists, the transfer is rejected.

### Step 4: Send More Files

After a successful transfer, the client returns to the file path prompt.

You can continue sending files over the same TCP connection.

### Step 5: Disconnect

Enter:

```text
/quit
```

The client sends a disconnect message and waits for the server's acknowledgement before closing the connection.

The server then returns to its listening loop and can accept another client.

## 6. Testing Locally

You can test the system using two terminals on the same machine.

**Terminal 1 — Server**

```bash
cd server
./server
```

**Terminal 2 — Client**

```bash
cd client
./client
```

Make sure both components use the same IP address and port configuration.

## 7. Verifying File Integrity

After a successful transfer, compare the SHA-256 digests of the original and received files.

On Linux:

```bash
sha256sum /path/to/original/file
sha256sum server/p2p_recieved/filename
```

Both SHA-256 values should be identical.

For example:

```text
a1b2c3...  original.pdf
a1b2c3...  received.pdf
```

The actual hash will depend on the file contents.

The server also performs this verification automatically before acknowledging the completion of a transfer.

## 8. Graceful Server Shutdown

Press:

```text
Ctrl + C
```

The server handles the interrupt signal, exits its main loop, closes active connections, and releases its resources.

## 9. Troubleshooting

| Problem | Possible Cause | Suggested Action |
|---|---|---|
| Connection refused | Server is not running | Start the server first |
| Connection timeout | Firewall or incorrect IP | Check network configuration |
| Address already in use | Port is occupied | Stop the other process or choose another port |
| File not found | Invalid client-side path | Check the file path |
| Permission denied | Insufficient filesystem permissions | Check directory and file permissions |
| Destination already exists | Server refuses to overwrite | Rename or remove the existing file |
| SHA-256 mismatch | File contents differ | Retry the transfer and investigate the source |
| Compilation failure | Missing development libraries | Install GCC and OpenSSL development packages |

## 10. Important Notes

- The server and client must implement compatible protocol versions.
- The application uses TCP and relies on its reliable, ordered byte stream.
- The current implementation supports one active client at a time.
- Multiple clients can connect sequentially, but simultaneous transfers are not supported.
- The transfer mechanism uses stop-and-wait acknowledgements.
- Incomplete transfers are discarded rather than resumed.
- SHA-256 provides integrity verification, not encryption or authentication.
- Do not expose the server to untrusted networks without additional security measures.

## 11. Version Information

**Release:** v1.0.0

This release supports basic end-to-end file transfer with metadata exchange, packet validation, acknowledgements, and SHA-256 verification.

For architectural details and protocol specifications, refer to [README.md](README.md).
