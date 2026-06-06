{Authors: CoLdBLooD and Darkstealth}
{e-mail : ilfourie@icon.co.za, webmaster@twinsunfiles.cjb.net
{License: This source code may be used and distrubuted freely. A modified
          version of the source code or any part of it and/or its compiled
          binaries may only be distributed in any form if the authors are
          notified about it and credit is given to them with the modified
          distribution(s).
}
{$G+,N+}
{ graphics lib }
unit grax_fla;

interface

type
 tvirscr = array[0..63999] of byte;

 txysize  = array[1..2] of word;

 TVgaPal = array[0..767] of byte;
 TPCXHeader  = record
  Manufacturer,
  Version,
  Encoding,
  BitsPerPixel  : Byte;
  Xmin,
  Ymin,
  Xmax,
  Ymax          : Word;
  HDpi,
  VDpi          : Word;
  Colormap      : array[1..48] of byte;
  Reserved,
  NPlanes       : Byte;
  BytesPerLine,
  PaletteInfo,
  HscreenSize,
  VscreenSize   : Word;
  Filler        : array[1..54] of byte;
 end;
{
Byte      Item          Size   Description/Comments
 0         Manufacturer 1      Constant Flag, 10 = ZSoft .pcx
 1         Version      1      Version information
                               0 = Version 2.5 of PC Paintbrush
                               2 = Version 2.8 w/palette information
                               3 = Version 2.8 w/o palette information
                               4 = PC Paintbrush for Windows(Plus for
                                  Windows uses Ver 5)
                               5 = Version 3.0 and &gt of PC Paintbrush
                                  and PC Paintbrush +, includes
                                  Publisher's Paintbrush . Includes
                                  24-bit .PCX files
 2         Encoding      1     1 = .PCX run length encoding
 3         BitsPerPixel  1     Number of bits to represent a pixel
                                  (per Plane) - 1, 2, 4, or 8
 4         Window        8     Image Dimensions: Xmin,Ymin,Xmax,Ymax
12         HDpi          2     Horizontal Resolution of image in DPI*
14         VDpi          2     Vertical Resolution of image in DPI*
16         Colormap     48     Color palette setting, see text
64         Reserved      1     Should be set to 0.
65         NPlanes       1     Number of color planes
66         BytesPerLine  2     Number of bytes to allocate for a scanline
                                  plane.  MUST be an EVEN number.  Do NOT
                                  calculate from Xmax-Xmin.
68         PaletteInfo   2     How to interpret palette- 1 = Color/BW,
                                  2 = Grayscale (ignored in PB IV/ IV +)
70         HscreenSize   2     Horizontal screen size in pixels. New field
                                  found only in PB IV/IV Plus
72         VscreenSize   2     Vertical screen size in pixels. New field
                                  found only in PB IV/IV Plus
74         Filler       54     Blank to fill out 128 byte header.  Set all
                                  bytes to 0
}
const
 vga = $a000;

var
 vgamem : array[1..64000] of byte absolute $a000:$0;
 virscr : ^tvirscr;
 virscr2 : ^tvirscr;
 virscr3 : ^tvirscr;
 virscr4 : ^tvirscr;
 vaddr,vaddr2,vaddr3,vaddr4 : word;
 f : file;
 xysize         : array[1..2] of word;



function  strtoint(s:string):integer;
PROCEDURE PUTPIC3(X,Y,SX,SY:WORD;P:Pointer;WHERE: WORD);
Procedure GrabPic(Var P : Pointer;X,Y,Wd,Hg : Word;Where: Word);
Procedure Line(a,b,c,d:integer;col:byte;where:word);
Procedure Hline (x1,x2,y:word;col:byte;where:word);
Procedure LoadSpriteFromPcx(Phile:String;Var P:Pointer);
Procedure GrabSprite(X,Y : Integer;Wd,Hg : Integer;WherePic : Pointer;Where:Word);
Procedure ShowSprite(X,Y : Integer;WherePic : Pointer;Trans : ByteBool;Where:Word);

procedure writeBMP(fn : string;pal:tvgapal);
procedure loadBMPScreen(fn : string;where : word);
procedure loadrawpal(fn:string;var vgapal:tvgapal);
procedure loadPCXScreen(fn : string;where:word);
function  inttostr(i: longint): string;
procedure freesprite(var p:pointer);
procedure pal(c,r,g,b : byte);
procedure setvgapal(vgapal:pointer;shifval : byte);
procedure wtrace;
procedure flip(where,whereto:word);
procedure flp(where,whereto:word);
procedure clss(where:word;c:byte);
procedure putpixel(x,y : integer;c:byte;where:word);
procedure setupvirtual;
procedure shutdown;
procedure setmcga;
procedure settext;

procedure loadBMPpal(fn:string;var vgapal:tvgapal);
{--------------------- PCX procedure -------------------- }
procedure loadPCXpal(fn:string;var vgapal:tvgapal);
function loadPCXfromfile(fn : string):pointer;
{^^^^^^^^^^^^^^^^^^^^^ PCX procedure ^^^^^^^^^^^^^^^^^^^^}


implementation


 {Val.PAS}

{Sample code for the Val procedure.}

{ For Windows: }
{ uses WinCrt; }

function strtoint(s:string):integer;
var I, Code: Integer;
begin
{ Get text from command line }
 Val(s,I,Code);
 { Error during conversion to integer? }
 if code <> 0 then
   strtoint:=666
 else
   strtoint:=i;
end;

PROCEDURE PUTPIC3;{(X,Y,SX,SY:WORD;P:Pointer;WHERE: WORD);}

VAR
YCOUNTER : INTEGER;
OLDDS,OWD,test1,test2 : word;
Begin
test1:=seg(p^);
test2:=ofs(p^);
OWD:=SX;
inc(sx);
{ OHG:=SY; }
IF (X+SX) > 320 THEN SX:=SX-((SX+X)-320);
IF (Y+SY) > 200 THEN SY:=SY-((SY+Y)-200);
{IF SX > OWD THEN SX:=0;  }
{SX := 200;}
ASM
Push DS

{cli}
Mov Ax,SY
Mov Ycounter,Ax
Mov Ax,Y
Mov Di,Ax
xchg Al,ah
Shl Di,6
Add Ax,X
Add Di,Ax
Mov Es,Where
Mov Ds,Test1
{Push Seg [P]
Pop Ds }
Mov Dx,SX
{INC DX}
Mov Bx,OWD
Inc Bx
Mov Si,Test2
@YcountLoop:
  nop
  Mov Cx,DX
  Add Si,DX
  Add Di,DX
    @XcountLoop:
      Dec Si
      Dec Di
      Mov Al,DS:[SI]
      {Cmp Al,0}
      Test Al,Al
      Jz @ZeroJump
      Mov ES:[DI],Al
     @ZeroJump:
      Dec Cx
    JNZ @Xcountloop

  Add Si,Bx
  Add Di,320
  Dec Ycounter
JNZ @YcountLoop

{sti }
Pop Ds

End;
End;
{END;}

Procedure GrabPic(Var P : Pointer;X,Y,Wd,Hg : Word;Where: Word);
Var
OLDDS,OWD,TEst1,test2 : word;
ycounter : integer;
Begin
test1:=seg(p^);
test2:=ofs(p^);
OWD:=WD;
IF (X+Wd) > 320 THEN Wd:=Wd-((Wd+X)-320);
IF (Y+Hg) > 200 THEN Hg:=Hg-((Hg+Y)-200);
ASM
{Push DS }                        { save ds}
MOV OLDDS,DS

{cli }

Mov Ax,Hg                       { ax = height }
Mov Ycounter,Ax                 { ycounter = height }
Mov Ax,Y                        { ax = y location }
Mov Di,Ax                       { di = y location }
xchg Al,ah
Shl Di,6
Add Ax,X
Add Di,Ax                       { di = offset on vga }
Mov Es,Where                    { point es to active page }
Mov Ds,Test1                    { point ds to place to store pic }
Mov Dx,OWD                      { dx = width }
Inc Dx                          { dx = width+1 }
Mov Bx,Dx                       { bx = width+1 }

Mov Si,Test2                    { si = offset of place to store pic }
@YcountLoop:
  nop
  Mov Cx,Dx                     { cx = width+1 }
  Add Si,Dx                     { di = di+width+1 }
  Add Di,Dx
    @XcountLoop:
      Dec Si
      Dec Di
      Mov Al,ES:[DI]
      Mov DS:[SI],Al
    Loop @Xcountloop
  Add Si,Bx
  Add Di,320
  Dec Ycounter
JNZ @YcountLoop

{sti }
MOV DS,OLDDS
End;

End;


Procedure Line(a,b,c,d:integer;col:byte;where:word);
  { This draws a solid line from a,b to c,d in colour col }
  function sgn(a:real):integer;
  begin
       if a>0 then sgn:=+1;
       if a<0 then sgn:=-1;
       if a=0 then sgn:=0;
  end;
var i,s,d1x,d1y,d2x,d2y,u,v,m,n:integer;
begin
     u:= c - a;
     v:= d - b;
     d1x:= SGN(u);
     d1y:= SGN(v);
     d2x:= SGN(u);
     d2y:= 0;
     m:= ABS(u);
     n := ABS(v);
     IF NOT (M>N) then
     BEGIN
          d2x := 0 ;
          d2y := SGN(v);
          m := ABS(v);
          n := ABS(u);
     END;
     s := m shr 1;
     FOR i := 0 TO m DO
     BEGIN
          if (a<320)and(a>=0)and(b>=0)and(b<200) then
          putpixel(a,b,col,where);
          s := s + n;
          IF not (s<m) THEN
          BEGIN
               s := s - m;
               a:= a + d1x;
               b := b + d1y;
          END
          ELSE
          BEGIN
               a := a + d2x;
               b := b + d2y;
          END;
     end;
END;

Procedure Hline (x1,x2,y:word;col:byte;where:word); assembler;
  { This draws a horizontal line from x1 to x2 on line y in color col }
asm
  mov   ax,where
  mov   es,ax
  mov   ax,y
  mov   di,ax
  shl   ax,8
  shl   di,6
  add   di,ax
  add   di,x1

  mov   al,col
  mov   ah,al
  mov   cx,x2
  sub   cx,x1
  shr   cx,1
  jnc   @start
  stosb
@Start :
  rep   stosw
end;

Procedure LoadSpriteFromPcx(Phile:String;Var P:Pointer);
Type
TPcxHeader = Record
 Manufacturer : Byte;
 Version      : Byte;
 Encoding     : Byte;
 BitsPerPixel : Byte;
 XMin         : Word;
 YMin         : Word;
 XMax         : Word;
 YMax         : Word;
 HDpi         : Word;
 VDpi         : Word;
 Colormap     : Array[0..47] Of byte;
 Reserved     : Byte;
 NPlanes      : Byte;
 BytesPerLine : Word;
 Palletteinfo : Word;
 HscreenSize  : Word;
 VscreenSize  : Word;
 Filler       : Array[0..53] Of byte;
End;

Procedure EncGet(Var Data:Byte;Var Count:Integer;Var Fi:File);
Var
 i : Byte;
Begin
        Count:= 1;

        Blockread(Fi,i,1);

        if ($C0 = ($C0 And i)) Then
        begin
         count := $3F And i;
         Blockread(Fi,i,1);
        end;
        data := i;
End;
Var
 PCXHeader : TPCXHeader;
 F : File;
 XSize,YSize : Word;
 Totalbytes : Word;
 RData,BData : Byte;
 PicSeg,PicOfs : Word;
 X,Y,XSub,Counter,Xcount : Integer;
Begin
{$I-}
Assign(F,Phile);
Reset(F,1);
{$I+}
if ioresult <> 0 then begin settext;writeln('File: ',Phile,' NOT FOUND!');halt;end;
Blockread(F,PCXHeader,128);

With PCXHEADER Do
Begin

 XSIZE:=1+ Xmax - Xmin;
 YSIZE:=1+ Ymax - Ymin ;
 TotalBytes:= NPlanes * BytesPerLine;

{ Writeln('XMin,Ymin  : ',Xmin,':',Ymin);
 Writeln('XMax,YMax  : ',Xmax,':',Ymax);
 Writeln('XSize,YSize: ',Xsize,':',Ysize);
 Writeln('TotalBytes : ',Totalbytes);   }

If P = Nil Then Getmem(P,(Xsize*(Ysize+1))+4);

 PicSeg:=Seg(P^);
 PicOfs:=Ofs(P^);

 xsub:=totalbytes-xsize;{ else xsub:=1;  }

 Memw[Picseg:Picofs]:=XSize-1;
 inc(picofs,2);
 Memw[Picseg:Picofs]:=YSize-1;
 inc(picofs,2);
{ if odd(xsize) then}

 Y:=0;
xcount:=0;
{While Y <= Ysize Do}


while y <= (bytesperline*ysize) do begin

encget(rdata,Counter,F);
for x := 1 to counter do
begin
Mem[Picseg:Picofs]:=RData;
inc(Picofs);
{inc(xcount,x);}
inc(xcount);
if xcount=bytesperline then begin dec(picofs,xsub);xcount:=0;end;

end;
inc(y,counter);
end;


(*
for y := 0 to totalbytes*ysize do
Begin
 Xsub:=1;
 While Xsub < TotalBytes Do
  Begin
   Blockread(F,BData,1);
   If (BData And $C0) = $C0 Then
    Begin
     Blockread(F,RData,1);
     {Bdata:=Bdata xor $c0;   }
     Bdata := Bdata and $3F;
     For X := 1 to Bdata do
      Begin
       Mem[Picseg:Picofs]:=RData;
       Inc(Picofs);
      End;
     Inc(Xsub,Bdata);
    End
   Else
    Begin
     Mem[Picseg:Picofs]:=BData;
     Inc(Picofs);
     Inc(xsub);
    End;
  {Writeln('Encoded String',Bdata xor Byte(11000000)) Else}
  End;
 {Writeln('No Encoded',Bdata xor Byte(11000000));}

 Inc(y);
*)
End;
{asm
int 3h
mov al,bdata
and al,$c0
end;       }
{Else }{Writeln(Bdata);}



Close(F);

End;

Procedure GrabSprite(X,Y : Integer;Wd,Hg : Integer;WherePic : Pointer;Where:Word);
{ This procedure grabs a piece of texture of the source and places it in a
  pointer with the wd and hg in the first 2 words. It a ShowSprite but in
  reverse - this procedure is HIGLY optimized }
Var
 dWordLop : byte;
 wordLop : byte;
 bytelop : byte;
begin
{dwordlop:=Wd div 4;}
{dec(wd);}
dec(hg);
dwordlop:=Wd div 4;
wordlop:=((Wd mod 4) div 2);
bytelop:=((Wd mod 4) mod 2);
dec(wd);
If WherePic = nil then Getmem(WherePic,(Wd*(hg+1))+4);
 Asm
 push ds
  mov  ds,where             { -ax- -bx- -cx- -dx-}
  mov  si,x
  mov  ax,y
  mov  cx,ax
  xchg ah,al
  shl  cx,6
  add  ax,cx
  add  si,ax

  Les  Di,WherePic    { Neat! Points Es And Di To Data - Skips 18 lines of pascal red-tape}

  Mov  Ax,Wd
  Mov  Dx,Hg

  mov  bx,Ax


  Mov  Es:[Di+2],Dx
  Mov  Es:[Di],Ax

  Add  di,4
  inc bx
  mov cx,320
  sub cx,bx
  mov bx,cx

xor cx,cx
mov ah,dwordlop
mov al,wordlop
mov dh,bytelop
{int 3h }
@XLoop:

 { Mov  Al,Ds:[Si+Bx]    { Screen  }
 { Mov  Es:[Di+Bx],Al    { Texture }

mov  cl,ah { Howmany Dwords may we put ? }
{  shr cx,1}

Db $66 { This changes Rep Movsw to Rep Movsd }
 rep movsw

{ add di,8
 add si,8}

  mov cl,al { Howmany words may we put ? }

 rep movsw

mov cl,dh
rep movsb


 { add  si,320}

  add  si,bx { use bx for sub/add }
  dec  dl { Max height 256 }
 jns  @xloop
 pop ds
End;
end;
Procedure ShowSprite(X,Y : Integer;WherePic : Pointer;Trans : ByteBool;Where:Word);Assembler;
 Asm
          Push  Ds                    { Stack it                             }
          Mov   Ah,trans              { Move a true or false into AH         }
          Test  Ah,ah                 { Is it true or false ?                }
          jz    @nomod                { If false goto @nomod else proceed    }
        {  Mov   Di,offset @modme      { Move the offset of @modme in di      }
          Mov   Ax,$c084              { Move the test al,al command in ax    }
          Mov   Cs:offset @modme,ax            { Move the command strait into our code}
          Mov   Ax,$0274              { Move the jz 02h command in ax        }
          Mov   Cs:offset @modme+2,ax          { Now move it into the code            }
          Jmp   @don                  { Jump to @don                         }
                                      {}
 @nomod:  {Mov   Di,offset @modme      {}
          Mov   Ax,$9090              { Move two Nop's into ax               }
          Mov   Cs:offset @modme,ax            {}
          Mov   Ax,$9090              {}
          Mov   Cs:offset @modme+2,ax          {}
                                      {}
 @don:    Mov   Ds,where              { Move destination into DS             }
          Mov   Si,x                  { Start Calcing the offset             }
          Mov   Bx,y                  { }
          Mov   Cx,bx                 { }
          Xchg  Bh,bl                 { Bx:=Bx*256                           }
          Shl   Cx,6                  { Cx:=Cx*128                           }
          Add   Bx,cx                 { }
          Add   Si,bx                 { Get final offset                     }
          Les   Di,WherePic           { Neat! Points Es And Di To Data - Skips 18 lines of pascal red-tape}
          Mov   Cx,Es:[Di]            { Ok load the Width from our pointer   }
          Mov   Dx,Es:[Di+2]          { Now the height                       }


{          dec dx}
          Mov   Bx,cx                 { }
{          dec bx}
          Add   Di,4                  { Skip the two words and go to picture }
                                      { }
 @XLoop:  Mov   Al,Es:[Di+Bx]         { Get the first pixel from Texture     }
                                      {}
 @modme:  Nop    { test al,al }       { * Here the code is modified          }
          Nop                         { * Do not change the position of      }
          Nop    { jz @trans }        { * the @trans label. In other words do}
          Nop                         { * not add anything between @modme and}
                                      { * @trans                             }
          Mov   Ds:[Si+Bx],Al         { Now move the pixel to the Destination}
                                      {}
 @trans:  Dec   Bx                    { Decrease our xloop                   }
          Jns   @xloop                { Are we past zero ? if not loop       }
          Add   Di,cx                 { Add the wd to your texture for nxt line}
          Add   Si,320                { Add 320 to si for next line on screen}
          Mov   BX,CX                 { Restore our wd vlue                  }
          Inc   Di                    { increase di                          }
{          Inc   Di                    { increase di                          }
          Dec   Dx                    { next y line                          }
          Jns   @xloop                { are we past zero ? if not loop       }
          Pop   Ds                    { UnStack it                           }
 End;

function IntToStr(I: Longint): String;
{ Convert any integer type to a string }
var
 S: string[11];
begin
 Str(I, S);
 IntToStr := S;
end;

procedure pal;assembler;
asm
{ int 3h  }
 mov dx,$3c8

 mov al,c
 out dx,al
 inc dx

 mov al,r
 out dx,al
 mov al,g
 out dx,al
 mov al,b
 out dx,al

(*
Procedure Pal(ColorNo : Byte; R,G,B : Byte);
  { This sets the Red, Green and Blue values of a certain color }
  Begin
     Port[$3c8] := ColorNo;
        Port[$3c9] := R;
           Port[$3c9] := G;
              Port[$3c9] := B;
              End;*)
end;


procedure freesprite(var p:pointer);
var
 xy : pointer;

begin
 xy:=addr(xysize);
 move(p^,xy^,4);
 freemem(p,(xysize[1]*xysize[2])+4);
end;

procedure loadPCXpal(fn:string;var vgapal:tvgapal);
var
 f : file;
 x : integer;
begin
 assign(f,fn);
 reset(f,1);
 seek(f,filesize(f)-sizeof(tvgapal));
 blockread(f,vgapal,sizeof(tvgapal));
 close(f);
end;

procedure loadrawpal(fn:string;var vgapal:tvgapal);
var
 f : file;
 x : integer;
begin
 assign(f,fn);
 reset(f,1);
 blockread(f,vgapal,sizeof(tvgapal));
{ for x := 0 to 255 do
 begin
  vgapal[(x*3)]:=vgapal[(x*3)] shr 2;
  vgapal[(x*3)+1]:=vgapal[(x*3)+1] shr 2;
  vgapal[(x*3)+2]:=vgapal[(x*3)+2] shr 2;
 end;    }

 close(f);
end;

type
 tBMPheader = record
    id    : array[0..1] of char;
    size  : longint;
    re1   : word;
    re2   : word;
    boff  : longint;
    size2  : longint;
    width : longint;
    height   : longint;
    planes   : word;
    bitcount : word;
    compre   : longint;
    sizeimg  : longint;
    xppm     : longint;
    yppm     : longint;
    ncolor   : longint;
    impcol   : longint;
 end;

(*
 tBMPInfoheader = record
    size     : longint;
    width    : longint;
    height   : longint;
    planes   : word;
    bitcount : word;
    compre   : longint;
    sizeimg  : longint;
    xppm     : longint;
    yppm     : longint;
    ncolor   : longint;
    impcol   : longint;
 end;

 tBMPheaderAll = record
  BMPHeader : tBMPheader;
  BMPInfo   : tBMPInfoheader;
 end;                          *)
procedure loadBMPpal(fn:string;var vgapal:tvgapal);
var
 f : file;
 x : integer;
 header : tbmpheader;
 vgapal2 : array[0..(256*4)] of byte;

begin
 assign(f,fn);
 reset(f,1);

 blockread(f,header,sizeof(tBMPheader));
 seek(f,(header.boff)-(256*4));
 blockread(f,vgapal2,sizeof(vgapal2));
 close(f);


 for x := 0 to 255 do
 begin
{r}  vgapal[(x*3)]:=vgapal2[((x*4)+2)];      {b}
{g}  vgapal[((x*3)+1)]:=vgapal2[((x*4)+1)];      {g}
{b}  vgapal[((x*3)+2)]:=vgapal2[((x*4))];    {r}
 end;


end;

{
 Loads uncompressed 256 col BMP fromat files
}
procedure writeBMP(fn : string;pal:tvgapal);
var
 moff,ytot,x : word;
 header : tbmpheader;
 vgapal2 : array[0..(256*4)-1] of byte;
begin
 assign(f,fn);
 rewrite(f,1);

{ clss(where,0);}
 for x := 0 to 255 do
 begin
{r}  vgapal2[((x*4)+2)]:=pal[(x*3)];      {b}
{g}  vgapal2[((x*4)+1)]:=pal[((x*3)+1)];      {g}
{b}  vgapal2[((x*4))]:=pal[((x*3)+2)];    {r}
     vgapal2[((x*4)+3)]:=0;
 end;
flp(vga,vaddr);
for ytot := 0 to 199 do
for moff := 0 to 319 do
virscr3^[moff+((199-ytot)*320)]:=virscr^[moff+(ytot*320)];

header.id:='BM';
header.size:=(64000+sizeof(vgapal2)+sizeof(tbmpheader));
header.re1:=0;
header.re2:=0;
header.boff:=(sizeof(vgapal2)+sizeof(tbmpheader));
header.size2:=$28;
header.width:=320;
header.height:=200;
header.planes:=1;
header.bitcount:=8;
header.compre:=0;
header.sizeimg:=64000;
header.xppm:=0;
header.yppm:=0;
header.ncolor:=256;
header.impcol:=0;

blockwrite(f,header,sizeof(tbmpheader));
blockwrite(f,vgapal2,(256*4));
blockwrite(f,virscr3^,64000);
close(f);

end;

procedure loadBMPScreen(fn : string;where : word);
var
 moff,ytot : word;
 header : tbmpheader;

begin
 assign(f,fn);
 reset(f,1);

blockread(f,header,sizeof(tbmpheader));
seek(f,header.boff);
blockread(f,virscr3^,64000);
close(f);

for ytot := 0 to 199 do
for moff := 0 to 319 do
virscr^[moff+((199-ytot)*320)]:=virscr3^[moff+(ytot*320)];

end;

{ * must be the size of the screen (320*200*256)
  * virscr3 must be loaded
  * loads to the vscreen specified
}
procedure loadPCXScreen(fn : string;where:word);
var
 size           : longint;
 data,count     : word;
 moff,ytot      : word;

begin
 assign(f,fn);
 reset(f,1);
 seek(f,sizeof(tpcxheader));
 size:=filesize(f)-sizeof(Tpcxheader)-768;
 blockread(f,virscr3^,size);
 close(f);
 ytot:=0;
 moff:=0;

 while (ytot<size) do
 begin
  count := virscr3^[ytot];
  inc(ytot);
  if ((count and $c0)=$c0) then
  begin
   count:=count xor $c0;
   data :=virscr3^[ytot];
   inc(ytot);
  end else
  begin
   data := count;
   count:=1;
  end;
  asm
        mov  es,where
        mov  di,moff
        mov  ax,data
        mov  ah,al
        mov  cx,ax
db $66; shl  ax,16
        mov  dx,count
        mov  ax,cx
db $66; mov  cx,dx
db $66; ror  cx,2
db $66; repz stosw
db $66; rol  cx,1
        repz stosw
db $66; rol  cx,1
        repz stosb
  end;
  inc(moff,count);
 end;
end;


function loadPCXfromfile(fn : string):pointer;
var
 p              : pointer;
 header         : Tpcxheader;
 size,index     : longint;
 xsize,ysize,
 TotalBytes,
 subtotal       : longint;
 data,count     : byte;
 loop           : longint;
 ytot           : longint;
begin
 assign(f,fn);
 reset(f,1);
 blockread(f,header,sizeof(Tpcxheader));

 with header do
 begin
  XySIZE[1] := Xmax - Xmin + 1;
  xYSIZE[2] := Ymax - Ymin + 1;
  inc(xysize[2]);

  TotalBytes := NPlanes * BytesPerLine;
  if p = nil then getmem(p,(xysize[1]*xysize[2])+4);
  loadPCXfromfile:=p;
  fillchar(p^,(xysize[1]*xysize[2])+4,4);
  move(xysize,p^,4);
  index:=3;

{  writeln(xysize[1],':',xysize[2],'|',totalbytes); }
 end;
 subtotal:=0;
 ytot:=0;
repeat
 repeat
  blockread(f,count,1);

  if (count and $c0)=$c0 then
  begin
   blockread(f,data,1);
   count:=count and $3f;
   for loop := 1 to count do mem[seg(p^):ofs(p^)+ytot+index+subtotal+loop]:=data;
   inc(subtotal,count);
  end
  else
  begin
   inc(subtotal);
   mem[seg(p^):ofs(p^)+ytot+index+subtotal]:=count;
  end;
 until subtotal>=totalbytes;
 inc(ytot,subtotal);
 subtotal:=0;
until ytot>=(totalbytes*(xysize[2]));

 close(f);
end;

procedure setvgapal(vgapal:pointer;shifval : byte);assembler;
asm
        mov al,shifval
        mov cs:[offset @modme+2],al
        push  ds
        lds   si,vgapal
        mov   dx,$3c8
        xor   ah,ah

@loop1: mov   al,ah
        out   dx,al
        inc   dx
        mov   cx,3

@mylop: lodsb

@modme: shr   al,2

        out   dx,al

{        outsb
        outsb
        outsb}

loop    @mylop
        dec   dx
        inc   ah
        jnz   @loop1
        pop  ds
end;

procedure wtrace; assembler;
asm
   mov dx,3DAh
@l1:in al,dx
   and al,08h
   jnz @l1
@l2:in al,dx
   and al,08h
   jz  @l2
end;


procedure flip(where,whereto:word);assembler;
asm
   push ds
   mov es,whereto
   mov ds,where
   xor di,di
   xor si,si
   mov cx,16000
   db $66
   repz movsw
   pop ds
end;

procedure flp(where,whereto:word);assembler;
asm
   push ds
   mov es,whereto
   mov ds,where
   xor di,di
   xor si,si
   mov cx,16000
   db $66
   repz movsw
   pop ds
end;

procedure clss(where:word;c:byte);assembler;
asm
   mov es,where
   xor di,di
   mov al,c
   mov ah,al
   mov cx,ax
   db $66
   ror ax,16
   mov ax,cx

   mov cx,16000
   db $66
   repz stosw
end;

procedure putpixel(x,y : integer;c:byte;where:word);assembler;
asm
   push where
   pop es
   mov ax,y        { 0000 0000 0100 0001 }
   xchg al,ah
   mov bx,ax
   shr ax,2
   add ax,bx
   add ax,x
   mov bx,ax
   mov al,c
   mov es:[bx],al
     {y*320}{y shl 6}
end;

procedure setupvirtual;
begin
 getmem(virscr,sizeof(tvirscr));
 getmem(virscr2,sizeof(tvirscr));
 getmem(virscr3,sizeof(tvirscr));
 getmem(virscr4,sizeof(tvirscr));
 vaddr:=Seg(virscr^);
 vaddr2:=Seg(virscr2^);
 vaddr3:=Seg(virscr3^);
 vaddr4:=seg(virscr4^);
end;

procedure shutdown;
begin
 freemem(virscr,sizeof(tvirscr));
 freemem(virscr2,sizeof(tvirscr));
 freemem(virscr3,sizeof(tvirscr));
 freemem(virscr4,sizeof(tvirscr));
end;

procedure setmcga;assembler;
asm
   mov ax,13h
   int 10h
end;


procedure settext;assembler;
asm
   mov ax,3h
   int 10h
end;


begin
end.