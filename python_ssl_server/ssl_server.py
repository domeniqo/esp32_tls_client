import socket, ssl


def deal_with_client(connstream):
    data = connstream.recv(1024)
    # empty data means the client is finished with us
    while data:
        if not do_something(connstream, data):
            # we'll assume do_something returns False
            # when we're finished with client
            break
        data = connstream.recv(1024)
    # finished with client


def do_something(conn, data):
    print(data)
    return False


context = ssl.create_default_context(ssl.Purpose.CLIENT_AUTH)
# server.pem is self-signed certificate
context.load_cert_chain(certfile="server.pem", keyfile="server.key")
# context setting for not checking CA paths and trusting self-signed certs
context.verify_flags = ssl.VERIFY_X509_PARTIAL_CHAIN

bindsocket = socket.socket()
bindsocket.bind(("", 8080))
bindsocket.listen(5)

while True:
    newsocket, fromaddr = bindsocket.accept()
    connstream = context.wrap_socket(newsocket, server_side=True)
    try:
        deal_with_client(connstream)
    finally:
        connstream.shutdown(socket.SHUT_RDWR)
        connstream.close()
