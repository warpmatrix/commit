from mininet.node import Host, Controller
from mininet.link import TCLink
from mininet.topo import Topo
from mininet.net import Mininet
from mininet.log import info, setLogLevel

from typing import Callable, List, Dict
from threading import Timer  # used to start a tcp flow after some time
import random

from utils import *


def test1(client_path: str):
    client_link_opts = {"bw": 1000, "delay": "10ms", "max_queue_size": 200}
    btlink_opts = {"bw": 10, "delay": "30ms", "loss1": 1, "loss2": 1, "max_queue_size": 100}
    servers_link_opts = [
        {"delay": "5ms"},
        {"delay": "10ms"},
        {"delay": "15ms"},
        {"delay": "20ms"}
    ]
    topo = Topo1(client_link_opts=client_link_opts,
        btlink_opts=btlink_opts, servers_link_opts=servers_link_opts
    )
    os.popen('python ./script.py {}'.format(topo.server_num))
    net = Mininet(controller=Controller, topo=topo, link=TCLink)
    setLogLevel('info')

    net.start()
    procs, files = runServers(topo, net)
    runClient(net, topo, client_path)
    clean(procs, files)
    net.stop()


class Topo1( Topo ):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)

    # def build( self, client_link_opts: dict, btlink_opts: dict, servers_link_opts: [dict]): # for python3.9+
    def build( self, client_link_opts: dict, btlink_opts: dict, servers_link_opts: List[Dict]):
        self.server_num = len(servers_link_opts)
        self.client = self.addHost('client', ip='10.100.0.1')
        self.client_switch = self.addSwitch('s0')
        addTCLink = add_link_maker(self, 's2')
        addTCLink(self.client, self.client_switch, **client_link_opts)

        self.servers_name = []
        self.server_switch = self.addSwitch('s1')
        for idx in range(self.server_num):
            server = self.addHost('server{}'.format(idx),
                ip='10.100.2.{}'.format(idx)
            )
            self.servers_name.append(server)
            addTCLink(server, self.server_switch,
                **servers_link_opts[idx]
            )

        addTCLink(self.client_switch,
            self.server_switch, **btlink_opts
        )

def test2(client_path: str):
    client_link_opts = {"bw": 1000, "delay": "10ms", "max_queue_size": 200}
    btlink_opts = {"bw": 10, "delay": "30ms", "loss1": 1, "loss2": 1, "max_queue_size": 100}
    servers_link_opts = [
        {"delay": "5ms"},
        {"delay": "10ms"},
        {"delay": "15ms"},
        {"delay": "20ms"}
    ]
    topo = Topo2(client_link_opts=client_link_opts,
        btlink_opts=btlink_opts, servers_link_opts=servers_link_opts
    )
    os.popen('python ./script.py {}'.format(topo.server_num))
    net = Mininet(controller=Controller, topo=topo, link=TCLink)
    setLogLevel('info')
    net.start()
    procs, files = runServers(topo, net)

    def start_tcp_flow():
        tcp_left = net.getNodeByName("tcp_left")
        tcp_right = net.getNodeByName("tcp_right")
        iperfnotes = net.iperf(hosts=[tcp_left, tcp_right], l4Type="TCP", seconds=20)
        print(iperfnotes)

    t = Timer(10.0, start_tcp_flow)
    t.start()
    capture_filter = 'tcp'
    interface_name = 'tcp_left-eth0'
    capture_file = 'tcpflow.pcap'
    tshark_cmd = f'tshark -i {interface_name} -f "{capture_filter}" -w {capture_file}'
    tshark_proc = net.getNodeByName("tcp_left").popen(tshark_cmd, shell=True)
    runClient(net, topo, client_path)

    tshark_proc.terminate()  # stop tshark process
    clean(procs, files)
    if not t.finished:
        t.cancel()
    net.stop()

class Topo2( Topo ):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)

    # def build( self, client_link_opts: dict, btlink_opts: dict, servers_link_opts: [dict]): # for python3.9+
    def build( self, client_link_opts: dict, btlink_opts: dict, servers_link_opts: List[Dict]):
        self.server_num = len(servers_link_opts)
        self.client = self.addHost('client', ip='10.100.0.1')
        self.client_switch = self.addSwitch('s0')
        add_clinet_TCLink = add_link_maker(self, 's0')
        add_clinet_TCLink(self.client, self.client_switch, **client_link_opts)

        self.servers_name = []
        self.server_switch = self.addSwitch('s1')
        add_server_TCLink = add_link_maker(self, 's1')
        for idx in range(self.server_num):
            server = self.addHost('server{}'.format(idx),
                ip='10.100.2.{}'.format(idx)
            )
            self.servers_name.append(server)
            add_server_TCLink(server, self.server_switch,
                **servers_link_opts[idx]
            )

        addTCLink = add_link_maker(self, 's2')
        addTCLink(self.client_switch, self.server_switch, **btlink_opts)

        tcp_left_node = self.addHost('tcp_left', ip='10.240.1.1')
        tcp_right_node = self.addHost('tcp_right', ip='10.240.1.2')
        # connect tcp left node with left switch
        self.addLink(tcp_left_node, self.client_switch, **{"bw": 1000, "delay": '2ms'})
        # connect tcpright node with right switch
        self.addLink(self.server_switch, tcp_right_node, **{"bw": 1000, "delay": '2ms'})


def test3(client_path: str):
    client_link_opts = {"bw": 1000, "delay": "10ms", "max_queue_size": 1000}
    btlink_opts1 = {"bw": 3.5, "delay": "30ms", "loss1": 1, "loss2": 1, "max_queue_size": 20}
    btlink_opts2 = {"bw": 1.5, "delay": "30ms", "loss1": 1, "loss2": 1, "max_queue_size": 20}
    servers_link_opts = [
        {"delay": "5ms"},
        {"delay": "10ms"},
        {"delay": "15ms"},
        {"delay": "20ms"}
    ]
    topo = Topo3(client_link_opts=client_link_opts,
        btlinks_opts=[btlink_opts1, btlink_opts2], servers_link_opts=servers_link_opts)
    os.popen('python ./script.py {}'.format(topo.server_num))
    net = Mininet(controller=Controller, topo=topo, link=TCLink)
    setLogLevel('info')
    net.start()
    procs, files = runServers(topo, net)

    def start_tcp_flow():
        tcp_left = net.getNodeByName("tcp_left")
        tcp_right = net.getNodeByName("tcp_right")
        iperfnotes = net.iperf(hosts=[tcp_left, tcp_right], l4Type="TCP", seconds=20)
        print(iperfnotes)

    t = Timer(10.0, start_tcp_flow)
    t.start()

    capture_filter = 'tcp'
    interface_name = 'tcp_left-eth0'
    capture_file = 'tcpflow.pcap'
    tshark_cmd = f'tshark -i {interface_name} -f "{capture_filter}" -w {capture_file}'
    tshark_proc = net.getNodeByName("tcp_left").popen(tshark_cmd, shell=True)
    runClient(net, topo, client_path)

    tshark_proc.terminate()  # stop tshark process
    clean(procs, files)
    if not t.finished:
        t.cancel()
    net.stop()


class Topo3( Topo ):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)

    # def build( self, client_link_opts: dict, btlink_opts: dict, servers_link_opts: [dict]): # for python3.9+
    def build( self, client_link_opts: dict, btlinks_opts: List[Dict], servers_link_opts: List[Dict]):
        self.server_num = len(servers_link_opts)
        addTCLink = add_link_maker(self, 's2')
        self.client = self.addHost('client', ip='10.100.0.1')
        self.client_switch = self.addSwitch('s0')
        addTCLink(self.client, self.client_switch, **client_link_opts)
        self.client_bo0 = self.addSwitch('s0-0')
        addTCLink(self.client_switch, self.client_bo0, **client_link_opts)
        self.client_bo1 = self.addSwitch('s0-1')
        addTCLink(self.client_switch, self.client_bo1, **client_link_opts)

        self.servers_name = []
        self.server_switch0 = self.addSwitch('s1-0')
        addTCLink(self.client_bo0, self.server_switch0, **btlinks_opts[0])
        self.server_switch1 = self.addSwitch('s1-1')
        addTCLink(self.client_bo1, self.server_switch1, **btlinks_opts[1])
        for idx in range(self.server_num):
            server = self.addHost('server{}'.format(idx),
                ip='10.100.2.{}'.format(idx)
            )
            self.servers_name.append(server)
            if idx < self.server_num - 2:
                addTCLink(server, self.server_switch0, **servers_link_opts[idx])
            else:
                addTCLink(server, self.server_switch1, **servers_link_opts[idx])


        tcp_left_node = self.addHost('tcp_left', ip='10.240.1.1')
        tcp_right_node = self.addHost('tcp_right', ip='10.240.1.2')
        # connect tcp left node with left switch
        self.addLink(tcp_left_node, self.client_bo1, **{"bw": 1000, "delay": '2ms'})
        # connect tcpright node with right switch
        self.addLink(self.server_switch0, tcp_right_node, **{"bw": 1000, "delay": '2ms'})


def test4(client_path: str):
    client_link_opts = {"bw": 1000, "delay": "10ms", "max_queue_size": 200}
    btlink_opts = {"bw": 10, "delay": "30ms", "loss1": 1, "loss2": 1, "max_queue_size": 100}
    servers_link_opts = [
        {"delay": "5ms"},
        {"delay": "10ms"},
        {"delay": "15ms"},
        {"delay": "20ms"}
    ]
    topo = Topo4(client_link_opts=client_link_opts,
        btlink_opts=btlink_opts, servers_link_opts=servers_link_opts
    )
    os.popen('python ./script.py {}'.format(topo.server_num))
    net = Mininet(controller=Controller, topo=topo, link=TCLink)
    setLogLevel('info')

    net.start()
    procs, files = runServers(topo, net)
    runClients(net, topo, client_path)
    clean(procs, files)
    net.stop()


class Topo4( Topo ):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)

    # def build( self, client_link_opts: dict, btlink_opts: dict, servers_link_opts: [dict]): # for python3.9+
    def build( self, client_link_opts: dict, btlink_opts: List[Dict], servers_link_opts: List[Dict]):
        self.server_num = len(servers_link_opts)
        addTCLink = add_link_maker(self, 's2')
        self.client_switch = self.addSwitch('s0')
        self.client1 = self.addHost('client1', ip='10.100.0.1')
        addTCLink(self.client1, self.client_switch, **client_link_opts)
        self.client2 = self.addHost('client2', ip='10.100.0.2')
        addTCLink(self.client2, self.client_switch, **client_link_opts)

        self.servers_name = []
        self.server_switch = self.addSwitch('s1')
        addTCLink(self.client_switch, self.server_switch, **btlink_opts)
        for idx in range(self.server_num):
            server = self.addHost('server{}'.format(idx),
                ip='10.100.2.{}'.format(idx)
            )
            self.servers_name.append(server)
            addTCLink(server, self.server_switch, **servers_link_opts[idx])



def test5(client_path: str):
    client_link_opts = {"bw": 1000, "delay": "10ms", "max_queue_size": 200}
    btlink_opts = {"bw": 10, "delay": "30ms", "loss1": 1, "loss2": 1, "max_queue_size": 100}
    servers_link_opts = [
        {"delay": "5ms"},
        {"delay": "10ms"},
        {"delay": "15ms"},
        {"delay": "20ms"}
    ]
    topo = Topo5(client_link_opts=client_link_opts,
        btlink_opts=btlink_opts, servers_link_opts=servers_link_opts
    )
    os.popen('python ./script.py {}'.format(topo.server_num))
    net = Mininet(controller=Controller, topo=topo, link=TCLink)
    setLogLevel('info')
    net.start()
    procs, files = runServers(topo, net)

    def DisconnectLinkRandomly():
        """ disconnect one of the links between right switch and right server nodes
        """
        # find links between servers nodes and right switch
        server_names = topo.servers_name
        right_links = [link for link in net.links if
                       link.intf1.node.name in server_names or link.intf2.node.name in server_names]
        # for link in right_links:
        #     print(f'{link.intf1.node.name}-->{link.intf2.node.name}')
        # note: since in AddTClink() we add an aux switch between server node and right switch, so inside the right_links,
        # each link will have a node name as serverx and another node name as aux switch
        net.ping()
        link_to_dissconnect = random.choice(right_links)
        info(f' link between {link_to_dissconnect.intf1.node.name} and {link_to_dissconnect.intf2.node.name} will be disconnected')
        net.delLink(link_to_dissconnect)
        net.ping()

    t = Timer(10.0, DisconnectLinkRandomly)
    t.start()
    runClient(net, topo, client_path)

    clean(procs, files)
    if not t.finished:
        t.cancel()
    net.stop()


class Topo5( Topo ):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)

    # def build( self, client_link_opts: dict, btlink_opts: dict, servers_link_opts: [dict]): # for python3.9+
    def build( self, client_link_opts: dict, btlink_opts: dict, servers_link_opts: List[Dict]):
        self.server_num = len(servers_link_opts)
        self.client = self.addHost('client', ip='10.100.0.1')
        self.client_switch = self.addSwitch('s0')
        addTCLink = add_link_maker(self, 's2')
        addTCLink(self.client, self.client_switch, **client_link_opts)

        self.servers_name = []
        self.server_switch = self.addSwitch('s1')
        for idx in range(self.server_num):
            server = self.addHost('server{}'.format(idx),
                ip='10.100.2.{}'.format(idx)
            )
            self.servers_name.append(server)
            addTCLink(server, self.server_switch, **servers_link_opts[idx])

        addTCLink(self.client_switch, self.server_switch, **btlink_opts)
