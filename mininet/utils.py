from mininet.node import Host, Controller
from mininet.link import TCLink
from mininet.topo import Topo
from mininet.net import Mininet
from mininet.log import info, setLogLevel

from time import sleep
import os

def add_link_maker(topo: Topo, prefix: str, base:int = 0):
    idx = base
    def f(node0, node1, **opt):
        nonlocal idx
        bridge = topo.addSwitch("{}-{}".format(prefix, idx))
        idx += 1

        delay, bw, max_queue_size = opt.get("delay"), opt.get("bw"), opt.get("max_queue_size")
        if delay is None:
            delay = 2
        loss1, loss2 = opt.get("loss1"), opt.get("loss2")

        # break node1<====> node2 into node1<===>bridge<====>node2
        # connect left part
        n1_bridge_link_opt = {"bw": bw}
        n0_bridge_opts = {"params1": n1_bridge_link_opt}  # node1--> bridge
        bridge_n1_link_opt = {"delay": delay, "loss": loss2, "max_queue_size": max_queue_size}  # n1<-------bridge
        n0_bridge_opts["params2"] = bridge_n1_link_opt
        # connect right part
        bridge_n2_link_opt = {"delay": delay, "loss": loss1, "max_queue_size": max_queue_size}  # bridge--->n2
        bridge_n1_opts = {"params1": bridge_n2_link_opt}
        n2_bridge_link_opt = {"bw": bw}  # beidge<----n2
        bridge_n1_opts["params2"] = n2_bridge_link_opt

        return topo.addLink(node0, bridge, cls=TCLink, **n0_bridge_opts), topo.addLink(bridge, node1, cls=TCLink, **bridge_n1_opts)

    return f


def runServers(topo: Topo, net: Mininet):
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

def startClient(client_name: str, net: Mininet, client_path: str):
    client: Host = net.get(client_name)
    f = open(client_name+'_stdout', mode='w')
    proc = client.popen('{} ./config/downnode_mn{}.json\
        '.format(client_path, client_name[len("client"):]), stdout=f)
    return proc, f

def clean(procs, files):
    for proc in procs:
        proc.kill()
    for f in files:
        if not f.closed:
            f.close()
    sleep(1)


def runClient(net: Mininet, topo: Topo, client_path: str):
    times = 1
    for _ in range(times):
        proc, f = startClient(topo.client, net, client_path)
        while proc.poll() is None:
            sleep(2)
        clean([proc], [f])


def runClients(net: Mininet, topo: Topo, client_path: str):
    times = 1
    for _ in range(times):
        procs, files = [], []
        proc1, f1 = startClient(topo.client1, net, client_path)
        procs.append(proc1)
        files.append(f1)
        proc2, f2 = startClient(topo.client2, net, client_path)
        procs.append(proc2)
        files.append(f2)
        while proc1.poll() is None or proc2.poll() is None:
            sleep(2)
        clean(procs, files)



