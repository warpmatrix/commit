import sys

downnode_content='''{
    "IP": "10.100.0.1",
    "port": 48888,
    "selfpeerID": "00010203040506070809101112131415161718FF",
    "upnodes": ['''

server_num = int(sys.argv[1])
with open('./config/downnode_mn.json', mode='w') as f:
    print(downnode_content, file=f)
    for idx in range(server_num):
        print("        {", file=f)
        print(f'            "IP": "10.100.2.{idx}",', file=f)
        print(f'            "port": 41111,', file=f)
        print(f'            "selfpeerID": "00010203040506070809101112131415161718{idx:02}"', file=f)
        if idx < server_num - 1:
            print('        },', file=f)
        else:
            print('        }', file=f)
    print('    ]', file=f)
    print('}', file=f)


for idx in range(server_num):
    with open(f"./config/upnode_mn{idx}.json", mode='w') as f:
        print("{", file=f)
        print(f'    "IP": "10.100.2.{idx}",', file=f)
        print(f'    "port": 41111,', file=f)
        print(f'    "selfpeerID": "00010203040506070809101112131415161718{idx:02}"', file=f)
        print('}', file=f)
