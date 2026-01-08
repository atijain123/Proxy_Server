# HTTP Proxy Server in C language
I have made a basic HTTP Proxy Server in C language and i have added all the files in this repo.
The proxy functions as a middle layer between users and remote servers, handling incoming HTTP requests, relaying them to target servers, delivering the responses back to clients, and logging request information.
The project aims to showcase fundamental principles of computer networking and systems programming, such as socket-based communication, request routing, logging mechanisms, and automated building processes.

# Core Functions
* Uses TCP socket communication
* Developed in standard C
* Receives and processes HTTP client requests
* Routes requests to remote servers and streams responses back
* Supports external file-based configuration
* Produces runtime logs for monitoring and troubleshooting
* Compiled and maintained using a Makefile

# Repository layout
```text
project/
├── Docs/
│ └── DESIGN.md
├── config/
│ ├── blocked.txt
│ └── server.conf
├── src/
│ ├── logs/
│ │ └── proxy.log
│ ├── Makefile
│ ├── main.c
│ └── proxy_server.exe
```
# Directory Overview
## Docs/
Contains DESIGN.md file.
### DESIGN.md
Explains the proxy server’s architecture, the flow of request handling, and the main design choices.

## Config/
Conatins Configuration files.
### server.conf
Specifies runtime settings including the listening port and log-related options.

## src/
Contains build and compilation resources and holds log files produced while the proxy server is active.
### logs/
Logs timestamps, client information, requested hosts, and request results.
### Makefile
Handles automatic compilation of the proxy server source code.

# Main Files
## main.c
The Main source file of this project in C langugae
This file is Accountable for
- Relaying server responses
- Recording activity logs
- Managing client connections
- Binding to and listening on the configured port
- Forwarding HTTP requests
- Setting up sockets

## proxy_server.exe
The file generated after a successful build process.

# How to Compile
## Requirements
- GCC compiler
- Make utility installed

## Build Steps
```bash
cd src
make
```
This command builds the source code and produces the executable file.

## Usage
Once the project is built, launch the proxy server and configure your client to connect through it.

### Example
```bash
curl.exe -v -x http://localhost:8888 http://example.com
```
By default, the proxy runs on port 8888 and forwards requests to the target server.

## Logging
All client requests and proxy activities are logged in:
```bash
logs/proxy.log
```
This file can be used to debug and analyze the proxy’s behavior while it is running.
