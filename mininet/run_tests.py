from time import sleep
from typing import Callable, List, Dict
import os
import sys

from topos import *

def main():
    client_path = "./MPDtest"
    if len(sys.argv) > 1:
        client_path = sys.argv[1]
    client_name = os.path.basename(client_path)
    test_cases: List[Callable] = [test1, test2, test3, test4, test5]
    for idx, test_case in enumerate(test_cases):
        test_case(client_path)
        save_result(idx + 1, client_name)

def save_result(idx, client_name):
    trace_files = os.popen("ls ./MPDTrace_*.txt").read().split('\n')
    case_name =  "topo_{}".format(idx)
    test_case_dir = "trace/{}".format(case_name)
    os.popen('mkdir -p {}'.format(test_case_dir))
    for trace_file in trace_files:
        if len(trace_file) == 0:
            continue
        results = os.popen('python ../tools/get_score.py {}'.format(trace_file)).read().split('\n')
        print("{}:".format(case_name), results[-3:])
        os.popen('python ../tools/get_score.py {} > {}/{}_score.txt\
                '.format(trace_file, test_case_dir, client_name))
    sleep(2)
    os.popen("rename 's/^.\/MPD/.\/{}_/' ./MPDTrace_*.txt".format(client_name))
    sleep(1)
    os.popen('mv ./*_Trace_*.txt {}'.format(test_case_dir))


if __name__ == "__main__":
    main()
