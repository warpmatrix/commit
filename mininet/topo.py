from mininet.topo import Topo
from mininet.node import Host
from mininet.net import Mininet
from mininet.link import TCLink, Link
from mininet.cli import CLI
from mininet.log import info, setLogLevel

from time import sleep
from typing import Callable, List
import os


def main():
    topo = MpdTopo(20)
    os.popen('python ./script.py {}'.format(topo.server_num))
    net = Mininet(topo=topo, link=TCLink)
    setLogLevel('info')
    net.start()
    net.iperf()
    procs, files = runServers(topo, net)

    def change_link_maker():
        cnt, idx = 0, 0
        def f(links_id: List[int]):
            nonlocal cnt, idx
            cnt += 1
            bws = [5, 1]
            # cnt = (cnt + 1) % len(bws)
            idx = 1
            if cnt == 5:
                for id in links_id:
                    net.links[id].intf1.config(bw=bws[idx])
        return f

    times = 1
    for _ in range(times):
        proc, f = runClient(topo, net)
        dec_link = change_link_maker()
        def isFin():
            last_str = os.popen('cat client_stdout | tail -n 1').read()
            return last_str.count("test finished") > 0
        def handle():
            last_str = os.popen('cat client_stdout | tail -n 1').read()
            # dec_link([topo.bnLink])
            info(last_str)
        wait(isFin, 10, handle)
        results = os.popen('python ../tools/get_score.py ./MPDTrace.txt').read().split('\n')
        print(results[-3:])
        clean([proc], [f])

    net.iperf()
    CLI(net)
    clean(procs, files)
    net.stop()


class MpdTopo( Topo ):
    def __init__(self, server_num: int = 1):
        self.server_num = server_num
        self.delays = ['20ms', '30ms', '40ms', '25ms', '30ms', '35ms', '40ms', '45ms', '50ms', '55ms']
        super().__init__()

    def build( self ):
        self.client = self.addHost('client', ip='10.100.0.1')
        self.client_switch = self.addSwitch('s0')
        self.addLink(self.client, self.client_switch, bw=100)

        self.servers_name, self.servers_link = [], []
        self.server_switch = self.addSwitch('s1')
        addTCLink = self.add_link_maker()
        for idx in range(self.server_num):
            server = self.addHost('server{}'.format(idx),
                ip='10.100.2.{}'.format(idx)
            )
            self.servers_name.append(server)
            s_link = addTCLink(server, self.server_switch,
                bw=5, delay=str(20+5*idx)+'ms', loss=1, max_queue_size=100
            )
            self.servers_link.append(s_link)

        self.bnLink = addTCLink(self.client_switch, self.server_switch,
            bw=10*self.server_num, delay='25ms', loss=1
        )

    def add_link_maker(self):
        idx = 0
        def f(node0, node1, delay='5ms', bw=100, loss=1, max_queue_size=100):
            nonlocal idx
            bridge = self.addSwitch("s1-{}".format(idx))
            idx += 1
            self.addLink(node0, bridge, cls=TCLink, delay=delay, loss=loss, max_queue_size=max_queue_size)
            link = self.addLink(node1, bridge, cls=TCLink, loss=0, bw=bw, max_queue_size=max_queue_size)
            return link
        return f


def runServers(topo: MpdTopo, net: Mininet):
    procs, files = [], []
    for idx, name in enumerate(topo.servers_name):
        server: Host = net.get(name)
        f = open(name+'stdout', mode='w')
        files.append(f)
        proc = server.popen('./bin/servertest ./config/upnode_mn{}.json\
            '.format(idx), stdout=f)
        procs.append(proc)
    # wait for procs initiation
    sleep(3)
    return procs, files

def runClient(topo: MpdTopo, net: Mininet):
    client: Host = net.get(topo.client)
    f = open(topo.client+'_stdout', mode='w')
    proc = client.popen('./MPDtest ./config/downnode_mn.json', stdout=f)
    return proc, f

def clean(procs, files):
    for proc in procs:
        proc.kill()
    for f in files:
        if not f.closed:
            f.close()
    sleep(1)


def wait(cond: Callable, dur: int, handle: Callable=None):
    if cond is None:
        return

    while not cond():
        if not handle is None:
            handle()
        sleep(dur)


if __name__ == "__main__":
    main()
