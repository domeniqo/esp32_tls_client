import socket
import sys

s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)

try:
    s.bind(("", 8090))

except socket.error as msg:
    print("Bind failed. Error Code : " + str(msg[0]) + " Message " + msg[1])
    sys.exit()

print("Socket bind complete")
s.listen(10)

conn, addr = s.accept()

print("Connected with " + addr[0] + ":" + str(addr[1]))

while a := conn.recv(1024):
    print(a)

s.close()
