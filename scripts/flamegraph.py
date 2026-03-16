#!/usr/bin/python

"""为指定的程序运行 perf 及相关生成程序，生成火焰图
"""

from argparse import ArgumentParser
import subprocess


class Args(ArgumentParser):
    """解析命令行参数
    """

    def __init__(self):
        super().__init__()

        self.add_argument("-o", "--output",
                          help="目标火焰图文件路径['fg.svg']", default="fg.svg")
        self.add_argument("input", nargs='+', help="被分析的程序及其参数")


args = Args().parse_args()

subprocess.run(
    ['perf', 'record', '--call-graph', 'dwarf', '--', *args.input],
    check=True,
)

with open('out.perf', 'w', encoding='utf-8') as out_perf:
    subprocess.run(
        ['perf', 'script', '-i', 'perf.data'],
        check=True,
        stdout=out_perf,
    )

with open('out.folded', 'w', encoding='utf-8') as out_folded:
    subprocess.run(
        ['stackcollapse-perf.pl', 'out.perf'],
        check=True,
        stdout=out_folded,
    )

with open(args.output, 'w', encoding='utf-8') as out_svg:
    subprocess.run(
        ['flamegraph.pl', 'out.folded'],
        check=True,
        stdout=out_svg,
    )
