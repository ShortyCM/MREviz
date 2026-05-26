import struct
w=h=32
data=bytearray()
data+=struct.pack('<HHH',0,1,1)
imgsize=40+w*h*4+128
offset=6+16
data+=struct.pack('<BBBBHHII',w,h,0,0,1,32,imgsize,offset)
img=bytearray()
img+=struct.pack('<IIIHHIIIIII',40,w,h*2,1,32,0,w*h*4,0,0,0,0)
for y in range(h-1,-1,-1):
    for x in range(w):
        cx,cy=15.5,15.5
        r=((x-cx)**2+(y-cy)**2)**0.5
        if r<5: rgb=(220,30,30)
        elif r<10: rgb=(245,245,245)
        elif r<15: rgb=(220,30,30)
        else: rgb=(30,30,30)
        b,g,r_=rgb[2],rgb[1],rgb[0]
        img += bytes([b,g,r_,255])
img += bytes(128)
open('app.ico','wb').write(data+img)
print('wrote app.ico')
