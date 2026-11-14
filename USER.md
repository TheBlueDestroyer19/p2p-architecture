# USER GUIDE — P2P File Transfer System (v1.2.0)

## 1. Project Overview

This repository contains two independent codebases:

- `client/` — TCP file-transfer client.
- `server/` — TCP file-transfer server.

Both have independent source files, header files, build configurations and executables. They communicate over TCP.

The project uses:
- C (GNU C11)
- GCC
- POSIX sockets
- POSIX threads (server)
- OpenSSL (SHA-256)

---

## 2. Repository Structure

```text
p2p/
├── client/
│   ├── include/
│   ├── src/
│   │   ├── app/
│   │   ├── network/
│   │   ├── protocol/
│   │   ├── reliability/
│   │   └── transfer/
│   ├── build/
│   ├── Makefile
│   └── .gitignore
│
├── server/
│   ├── include/
│   ├── src/
│   │   ├── app/
│   │   ├── network/
│   │   ├── protocol/
│   │   ├── reliability/
│   │   ├── runtime/
│   │   ├── storage/
│   │   └── transfer/
│   ├── build/
│   ├── Makefile
│   └── .gitignore
│
├── README.md
├── USER.md
└── LICENSE
```

All public header files are maintained directly inside their respective `include/` directories.

All generated object files and executables are stored inside `build/`.

---

## 3. Prerequisites

The following tools and libraries are required:

- GCC compiler
- GNU Make
- OpenSSL development libraries
- POSIX threads support
- Linux environment

### Ubuntu / Debian

Install the dependencies:

```bash
sudo apt update
sudo apt install build-essential libssl-dev
```

Verify the installations:

```bash
gcc --version
make --version
```

---

## 4. Building and Running Using Makefiles (Recommended)

Both the client and server have independent Makefiles.

The Makefiles automatically:
- Locate their respective project directories.
- Discover all `.c` source files recursively under `src/`.
- Compile source files separately.
- Generate dependency files for header tracking.
- Place object files inside `build/obj/`.
- Generate the final executable inside `build/`.

### 4.1 Running the Client

Navigate to the client directory:

```bash
cd client
```

Build and run:

```bash
make run
```

The executable is generated at:

```text
client/build/client
```

### 4.2 Running the Server

Navigate to the server directory:

```bash
cd server
```

Build and run:

```bash
make run
```

The executable is generated at:

```text
server/build/server
```

### 4.3 Running from the Repository Root

The Makefiles are independent, so they can also be invoked from the repository root using:

```bash
make -C client run
```

For the server:

```bash
make -C server run
```

The `-C` option tells Make to execute using the specified directory.

### 4.4 Available Make Commands

The following commands are supported by both Makefiles:

| Command | Description |
|---|---|
| `make` | Compile the application |
| `make run` | Build if necessary and execute the application |
| `make clean` | Remove the entire build directory |
| `make rebuild` | Clean and perform a fresh build |

For example:

```bash
cd server

make clean
make
make run
```

---

## 5. Building and Running Using Custom GCC Commands

The applications can also be compiled manually without using Makefiles.

These commands recursively discover all C source files under `src/`, including files inside nested directories.

Run the commands from the respective client or server directory.

### 5.1 Client

Navigate to the client directory:

```bash
cd client
```

Create the build directory:

```bash
mkdir -p build
```

Compile:

```bash
gcc -std=gnu11 \
    -Wall -Wextra -Wpedantic \
    -Iinclude \
    $(find src -type f -name '*.c') \
    -o build/client \
    -lcrypto
```

Run:

```bash
./build/client
```

### 5.2 Server

Navigate to the server directory:

```bash
cd server
```

Create the build directory:

```bash
mkdir -p build
```

Compile:

```bash
gcc -std=gnu11 \
    -Wall -Wextra -Wpedantic \
    -pthread \
    -Iinclude \
    $(find src -type f -name '*.c') \
    -o build/server \
    -lcrypto \
    -pthread
```

Run:

```bash
./build/server
```

### Compiler Flags Explained

| Flag | Purpose |
|---|---|
| `-std=gnu11` | Uses GNU C11 |
| `-Wall` | Enables common compiler warnings |
| `-Wextra` | Enables additional warnings |
| `-Wpedantic` | Reports non-standard language extensions |
| `-Iinclude` | Adds the header directory to the include search path |
| `-pthread` | Enables POSIX thread support |
| `-lcrypto` | Links OpenSSL cryptographic functionality |

**Note:** The manual commands compile all source files in one invocation. The Makefiles instead compile individual source files into object files and use dependency tracking for incremental builds.

---

## 6. Client-Server Execution

The server must be running before connecting a client.

For local testing, use two terminals.

**Terminal 1 — Server**

```bash
cd server
make run
```

**Terminal 2 — Client**

```bash
cd client
make run
```

The current local configuration uses:

```c
#define SERVER_IP   "127.0.0.1"
#define SERVER_PORT 8080
```

For communication between different machines, configure the server to bind to an appropriate network interface and configure the client with the server's reachable IP address.

---

## 7. Build Directory Management

Each application maintains its own build directory.

Example:

```text
server/
└── build/
    ├── server
    └── obj/
        ├── app/
        ├── network/
        ├── protocol/
        ├── runtime/
        ├── storage/
        └── transfer/
```

The client follows the same structure, with its own application-specific modules.

To remove generated binaries and object files:

```bash
make clean
```

To perform a fresh build:

```bash
make rebuild
```

The `build/` directory is excluded from Git through `.gitignore`.

---

## 8. Development Guidelines

1. Keep client and server codebases independent.
2. Place public header files directly inside the respective `include/` directory.
3. Organize source files according to their functionality.
4. Keep generated binaries and object files inside `build/`.
5. Avoid committing generated build artifacts.
6. Preserve compatibility between client and server protocol definitions.
7. Test both applications after modifying shared protocol behaviour.
8. Use the Makefiles for normal development and incremental builds.
9. Use custom GCC commands when manual compilation is required.

---

## 9. Git and Versioning

The repository follows a versioned release workflow.

- `main` contains the latest stable implementation.
- Development branches are used for upcoming changes.
- Annotated Git tags identify stable releases.

Examples:

```bash
git branch
git tag
```

To inspect a particular release:

```bash
git switch --detach v1.1.0
```

To return to the latest stable branch:

```bash
git switch main
```

---

## 10. License

This project is distributed under the MIT License.

Refer to the `LICENSE` file for the complete license terms.

