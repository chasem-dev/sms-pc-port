p=['START@1400','STICK_LEFT@2000','A@2400','STICK_LEFT@3100','A@3250','A@3450','A@3800']
def key(name,f,d=8):p.append('KEY_%s@%d+%d'%(name,f,d))
def click(x,y,f):p.append('MOUSE_LEFT_%d_%d@%d+2'%(x,y,f))
key('TAB',5000);click(284,366,5020);click(176,134,5040)
click(212,366,5070);click(176,134,5100);click(212,366,5130)
key('LSHIFT',5140,30);click(176,366,5160);key('TAB',5180)
p+=['KEY_V@5200+8','KEY_V@5220+8','R@5240+80']
key('TAB',5320);key('LSHIFT',5330,60);click(176,134,5340);click(320,366,5380);key('TAB',5400)
p.append('R@5440+100')
print(','.join(p))
