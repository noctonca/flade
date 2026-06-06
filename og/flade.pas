{Authors: CoLdBLooD and Darkstealth}
{e-mail : ilfourie@icon.co.za, webmaster@twinsunfiles.cjb.net
{License: This source code may be used and distrubuted freely. A modified
          version of the source code or any part of it and/or its compiled
          binaries may only be distributed in any form if the authors are
          notified about it and credit is given to them with the modified
          distribution(s).
}

uses crt, grax_fla, dos, fladeuni, int8h_2;


function validpath(s:string):boolean;
var
 old : string;
begin
 if s = '' then begin validpath:=false;exit;end;
  getdir(0,old);
  {$I-}
  chdir(path);
  {$I+}
  validpath:=ioresult = 0;
  chdir(old);
end;

var
 DirInfo: SearchRec;         { For Windows, use TSearchRec }
 ch : char;
begin

setupvirtual;


{flaplay('D:\lba\fla\dragon3.fla'); }
TextMode(C80 + Font8x8);
clrscr;
writeln;
path:=' ';

while not validpath(path) do
begin
 write('Enter path of FLA Video directory ( Ex. D:\LBA\FLA ): ');
 readln(path);
end;
{path:='D:\lba\fla';  }

if path[length(path)-1] = '\' then delete(path,length(path)-1,1);
writeln(path);

ch:=#0;
write('Dump FLA to disk? (<Enter> = Yes, <Esc> = No) ');
while not (ch in [#13,#27]) do ch:=readkey;
writeln;
if ch <> #13 then dumptodisk:=false else dumptodisk:=true;
{dumptodisk:=true;}


 if dumptodisk then
 begin
 ch:=#0;
  write(#13#10,'File format to dump to: <Enter> = PCX or <Esc> = BMP ');
  while not (ch in [#13,#27]) do ch:=readkey;
  writeln;
  if ch <> #13 then pcxdump:=false else pcxdump:=true;
  write(#13#10,'Enter destination directory to dump files to (Ex. C:\Fla): ');
  readln(Dest);
  if dest[length(path)-1] <> '\' then dest:=dest+'\';
  {$I-}
  mkdir(dest);
  {$I+}
  if IOresult<>0 then write('Error ',IOresult);
 end;

init8h;

repeat

TextMode(C80 + Font8x8);
writeln('‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹‹');
writeln('€   FLADE ver 0.99.3 Beta   €');
writeln('ﬂﬂﬂﬂﬂﬂﬂﬂﬂﬂﬂﬂﬂﬂﬂﬂﬂﬂﬂﬂﬂﬂﬂﬂﬂﬂﬂﬂﬂ');
 getdir(0,op);
  {$I-}
 chdir(path);
  {$I+}

 FindFirst('*.Fla', Archive, DirInfo); { Same as DIR *.PAS }
 while DosError = 0 do
 begin
   Writeln(DirInfo.Name,'':19-length(DirInfo.Name)
   ,':','':2-round((DirInfo.size/1024)/1024) div 10, (DirInfo.size / 1024)/1024:0:2,' MB');
   FindNext(DirInfo);
 end;
 chdir(op);

write(#13#10,'Enter name of FLA to play: ');
readln(filen);

if filen = '' then break;

if pos('.fla',filen) = 0 then filen:=filen+'.fla';
writeln(path+filen);
setmcga;
flaplay(path+'\'+filen);

directvideo:=false;

{writeln('Press A Key');}
readkey;

settext;
until port[$60] = 1;

close8h;

shutdown;
clrscr;
writeln('FLADE (FLA DEcoder) ver 0.99.3 Beta by CoLdBLooD & Darkstealth');
writeln;


end.