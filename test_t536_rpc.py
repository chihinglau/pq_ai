import socket, time, json

s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.settimeout(3)
try:
    s.connect(('192.168.100.1', 9090))
    print('Connected to RK3576:9090!')
    
    # Test ping
    req = json.dumps({"cmd":"ping"}).encode()
    s.send(req + b'\n')
    resp = s.recv(1024)
    print(f'Ping response: {resp.decode()}')
    
    # Test inference request
    req2 = json.dumps({
        "cmd": "infer",
        "features": [0.5, 0.3, 0.1, 0.8, 0.2],
        "vthd": 2.5,
        "ithd": 3.8
    }).encode()
    s.send(req2 + b'\n')
    resp2 = s.recv(4096)
    print(f'Infer response: {resp2.decode()}')
    
    s.close()
    print('SUCCESS')
except Exception as e:
    print(f'FAILED: {e}')
