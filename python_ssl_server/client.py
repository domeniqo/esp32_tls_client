import socket

with socket.create_connection(("localhost", 8090)) as sock:
    sock.send(b"Hello world from python!")
