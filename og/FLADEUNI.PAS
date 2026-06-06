{Authors: CoLdBLooD and Darkstealth}
{e-mail : ilfourie@icon.co.za, webmaster@twinsunfiles.cjb.net
{License: This source code may be used and distrubuted freely. A modified
          version of the source code or any part of it and/or its compiled
          binaries may only be distributed in any form if the authors are
          notified about it and credit is given to them with the modified
          distribution(s).
}
{$G+,N+}
unit fladeuni;

interface
uses grax_fla,crt,int8h_2;

type
 tflaheader = record
  id       : array[0..5] of byte;
  nframes  : longint;
  framed   : word;
{  framed2   : byte;   }

  xsize    : word;
  ysize    : word;
 end;

 tdatas = record
  loop : word;
  size : word;
  dummy : word;
 end;

 tdatas2 = record
  proc : word;
  size : word;
 end;

var

 pcxdump : boolean;

f : file;
myglo : word;

 dumptodisk : boolean;
 dest : string;
 op,path : string;
 filen : string;
 mypal:pointer;
 mypal2 : tvgapal;
 myx:integer;
 goffset : longint;
 flahead : tflaheader;
 datas   : tdatas;
 datas2  : tdatas2;
 pcxhead : TPCXheader;
 dopal : boolean;


procedure flaplay(fn: string);
procedure rip_de;
procedure rip_de2;

implementation

procedure do_pal;
var
 siz : word;
begin
{ move(virscr^[goffset+1],siz,2);
 siz:=(siz*2)+siz;    }

{ move(virscr^[goffset+5],mypal,siz); }

 mypal:=addr(virscr^[goffset+4]);
 move(mypal^,mypal2,768);
 dopal:=true;
{wtrace;}
{ setvgapal(mypal,2); }
{ flp(vaddr2,vga);  }
{ while port[$60]<>2 do; }
end;

{ write a pcx file from the data on the screen }
{ changed for flade }
procedure writepcx(fn:string;pal : tvgapal);
var
 f2   : file;
 moff   : word;
 data,count     : byte;
 ytot           : word;
 pcxhead: tpcxheader;
 width,height : integer;

begin
 width:=320;
 height:=200;

{ fillchar(pcxhead,sizeof(tpcxheader),0);   }
 with pcxhead do
 begin
  Manufacturer:=10;
  version:=5;
  encoding:=1;
  bitsperpixel:=8;
  xmin:=0;
  ymin:=0;
  xmax:=width-1;
  ymax:=height-1;
  hdpi:=300;
  vdpi:=300;
  reserved:=0;
  nplanes:=1;
  if odd(xmax) then bytesperline:=xmax+1 else
  bytesperline:=xmax+2;
  PaletteInfo:=1;
  HscreenSize:=0;
  VscreenSize:=0;
  XySIZE[1] := Xmax - Xmin + 1;
  xYSIZE[2] := Ymax - Ymin + 1;
 end;

 ytot:=0;
 moff:=0;
 flp(vga,vaddr);

 repeat
  data:=virscr^[ytot];
  count:=0;
  while (data=virscr^[ytot+count])and(count<63) do inc(count);
  if ((data and $c0)=$c0)or(count>1) then
  begin
   virscr3^[moff]:=(count or $c0);
   inc(moff);
  end;
  virscr3^[moff]:=data;
  inc(moff);
  inc(ytot,count);
 until ytot>=((xysize[1])*(xysize[2]));

 virscr3^[moff]:=12;
 inc(moff);

 assign(f2,fn);
 rewrite(f2,1);
 blockwrite(f2,pcxhead,sizeof(tpcxheader));
 blockwrite(f2,virscr3^,moff);
 blockwrite(f2,pal,768);
 close(f2);
end;


procedure flaplay(fn: string);
var
 counter : integer;
 fcount  : longint;
 bleh : byte;
    hehe : longint;
    naam : string;
begin
 gotoxy(1,24);

directvideo:=false;
 bleh:=pos('.',filen)-1;
 changed:=false;
 repeat until changed;
{$I-}
if dumptodisk then mkdir(dest+'\'+copy(filen,1,bleh));
{$I+}
  if IOresult<>0 then write('Error');
 assign(f,fn);
 FileMode := 0;
 reset(f,1);
 blockread(f,flahead,sizeof(tflaheader));
{ flahead.framed:=round(1/flahead.framed); }
 blockread(f,datas2,sizeof(tdatas2));
 seek(f,filepos(f)+datas2.size);

 fcount := 0;

 while (fcount < flahead.nframes)and(port[$60]<>1) do
 begin
  blockread(f,datas,6);
  blockread(f,virscr^,datas.size);

  goffset:=0;

  counter:=0;

  while counter < datas.loop do
  begin
   move(virscr^[goffset],datas2,4);
   inc(goffset,4);

   case datas2.proc of
    1 : do_pal;
    3 : ;
    6 : rip_de2;
    8 : rip_de;
   end;

   inc(goffset,datas2.size);
   inc(counter);
  end;


{   wtrace;
   flp(vaddr2,vga);   }

{if not dumptodisk then
begin}
{wtrace;
wtrace;}
{end;}

repeat

until tot2>=(1/flahead.framed);
tot2:=0;

if dopal then begin setvgapal(mypal,2);dopal:=false; end;
flp(vaddr2,vga);
inc(frames);
{write(#13,fps,'':4);}

  if dumptodisk then
   begin
   { readkey;}
    flp(vga,vaddr);
    bleh:=pos('.',filen)-1;
    naam:='00000000';
    delete(naam,1,length(inttostr(fcount)));
    naam:=naam+inttostr(fcount);
    naam:=dest+'\'+copy(filen,1,bleh)+'\'+naam;
if pcxdump then
    writepcx(naam+'.pcx',mypal2)
else
    writebmp(naam+'.bmp',mypal2);
{    clss(vga,255);}
   end;

  inc(fcount);
 end;
 close(f);

{  asm
  sti
 end;}
end;



procedure rip_de;
const
myofs : word = 0;
var
 data : byte;
 count : byte;
 offs:longint;
 myx : integer;
 does : boolean;
 xsize,ysize : integer;
 xloop : byte;
 yloop : integer;


 dummy1 : word;

 mseg,mofs : word;

label loc_1,loc_2,loc_3,loc_4;

begin

mseg:=seg(virscr^);
mofs:=goffset;
xsize:=flahead.xsize;
ysize:=flahead.ysize;
yloop:=flahead.ysize;
asm
push ds

{push $a000
pop es  }
mov es,vaddr2

xor di,di
{xor si,si  }
mov si,mofs

mov dummy1,si

{*******}mov ds,vaddr{*********}

{int 3h }
		mov	dx,di
                db $66
		xor	ax,ax
loc_1:
		lodsb
		or	al,al
		jz	loc_4
		mov	bl,al

loc_2:
		lodsb
		test	al,al
		js	loc_3

		mov	cx,ax
		lodsb
		rep	stosb
		dec	bl
		jnz	loc_2
		jmp	loc_4
loc_3:
		neg	al
                db $66
		mov	cx,ax
		shr	cx,2
                db $66
		rep	movsw
		mov	cl,al
		and	cl,3
		rep	movsb
		dec	bl
		jnz	loc_2
loc_4:
		add	dx,xsize
		mov	di,dx
		dec	yloop
		jnz	loc_1


pop ds
mov dummy1,si


{lodsw
mov myglo,ax}
end;

end;

procedure rip_de2;
const
myofs : word = 0;
var
 data : byte;
 count : byte;
 offs:longint;
 myx : integer;
 does : boolean;
 xsize,ysize : integer;
 xloop : byte;
 yloop : integer;


 dummy1 : word;

 mseg,mofs : word;

label loc_1,loc_2,loc_3,loc_4;

begin

mseg:=seg(virscr^);
mofs:=goffset;
xsize:=flahead.xsize;
ysize:=flahead.ysize;
yloop:=flahead.ysize;
asm
push ds

{push $a000
pop es   }
mov es,vaddr2

xor di,di
{xor si,si  }
mov si,mofs

mov dummy1,si

{*******}mov ds,mseg{*********}

{int 3h  }
                xor     ax,ax
		lodsw
		mov	dx,xsize
		imul	dx
		add	di,ax
		mov	dx,di
		lodsw
		mov	yloop,ax
		xor	ax,ax
loc_1:
		lodsb
		or	al,al
		jz	loc_4
		mov	bl,al
loc_2:
 		lodsb
		add	di,ax
		lodsb
		test	al,al
		js	loc_3
		mov	cx,ax
		shr	cx,2
                db      $66
		rep	movsw
		mov	cl,al
		and	cl,3
		rep	movsb
		dec	bl
		jnz	loc_2
		jmp	loc_4
loc_3:
		neg	al
		mov	cx,ax
		lodsb
		rep	stosb
		dec	bl
		jnz	loc_2
loc_4:
		add	dx,xsize
		mov	di,dx
		dec	yloop
		jnz	loc_1


pop ds
mov dummy1,si


{lodsw
mov myglo,ax}
end;

end;
end.

