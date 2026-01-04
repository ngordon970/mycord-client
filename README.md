Mycord – Networked Chat Client (C)

Mycord is a multi-threaded command-line chat client written in C that communicates with a server over TCP using a custom binary protocol. The client supports user login/logout, real-time message exchange, username mentions, and graceful shutdown handling.

This repository contains only the client implementation developed as part of a systems programming course. All instructor-provided materials (including the server and assignment scaffolding) have been intentionally excluded.

Features

TCP client built with POSIX sockets

Custom binary messaging protocol

Multi-threaded design for concurrent user input and server message handling

Graceful shutdown on Ctrl-C and EOF

Username mention detection with terminal highlighting and bell alerts

Optional quiet mode to suppress notifications

Technologies Used

C

POSIX sockets (socket, connect, send, recv)

Pthreads

Unix signal handling

Git

How It Works (High Level)

The client connects to a server over TCP.

Messages are exchanged using a fixed-size binary format.

One thread handles user input, while another listens for incoming server messages.

Messages that contain @<username> are highlighted and trigger a terminal bell unless quiet mode is enabled.

The client cleans up resources and notifies the server before exiting.

Usage

Compile the client:

gcc client.c -o client -pthread


Run the client:

./client --username <name> --ip <server_ip> --port <port>


Optional flags:

--quiet — disables mention notifications and bell sounds

Author

Noah Gordon
