#!/usr/bin/env python

import subprocess

f_src = '''
int f{n}(){{
    int *p = {name}();
    return *p;
}}
'''

def gen_file(n):
    with open('main.cpp', 'w') as f:
        f.write('''
int *null() {
    return nullptr;
}

int *notNull() {
    return new int;
}
''')
        for i in range(n):
            f.write(f_src.format(
                n = i,
                name = 'notNull' if i % 2 == 0 else 'null'
            ))

if __name__ == '__main__':
    gen_file(1)

    subprocess.run(['clang++', '-c', 'main.cpp', '-o', '/dev/null'])

    gen_file(100_000 + 50_000)
