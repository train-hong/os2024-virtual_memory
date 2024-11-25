import os
from gradelib import *

gen = 0

name = ['mp2_1', 'mp2_2', 'mp2_3', 'mp2_4', 'mp2_5', 'custom_1', 'custom_2', 'custom_3', 'custom_4', 'custom_5']
bases = ['qemu', 'qemu', 'qemu', 'fifo', 'lru', 'qemu', 'qemu', 'qemu', 'fifo', 'lru']
scores = [6, 12, 12, 12, 18, 4, 8, 8, 8, 12]

for s, base, score in zip(name, bases, scores):

    @test(score, "{}".format(s), base=base)
    def test_(t, tgbase):
        os.system("make clean  >/dev/null 2>&1")
        r = Runner(save("{}.out".format(t)))
        r.run_qemu(shell_script([t]), tg_base=tgbase, timeout=300)
        if gen == 1:
            res = r.qemu.output.split("\n")
            st = 0
            ed = 0
            while not '$ {}'.format(t) in res[st]:
                st += 1
            ed = st+1
            while not '$' in res[ed]:
                ed += 1
            f = open("mp2_output/{}.out".format(t), 'w+')
            f.write("\n".join(res[st+1:ed]))
        else: 
            r.match(*(open("mp2_output/{}.out".format(t), "r").read().splitlines()))
run_tests()
