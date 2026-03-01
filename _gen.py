import pathlib
BT = chr(96)
root = pathlib.Path(__file__).parent
lines_out = []
def a(s):
    lines_out.append(s)
a('import pathlib')
a('root = pathlib.Path(__file__).parent')
a('p = root / chr(80)+chr(82)+chr(79)+chr(74)+chr(69)+chr(67)+chr(84)+chr(95)+chr(79)+chr(86)+chr(69)+chr(82)+chr(86)+chr(73)+chr(69)+chr(87)+chr(46)+chr(109)+chr(100)')
a('print(p)')
open('_upd.py', 'w', encoding='utf-8').write(chr(10).join(lines_out))
print('generated')
