import socket
import ssl

hostname = "localhost"
context = ssl.create_default_context()
# context setting to ignore hostname and accept self-signed certs
context.check_hostname = False
context.verify_mode = ssl.VerifyMode.CERT_NONE

with socket.create_connection((hostname, 8080)) as sock:
    with context.wrap_socket(sock, server_hostname=hostname) as ssock:
        ssock.write(b"Hello SSL world from python!")
        print(ssock.version())
