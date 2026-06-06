{Authors: CoLdBLooD and Darkstealth}
{e-mail : ilfourie@icon.co.za, webmaster@twinsunfiles.cjb.net
{License: This source code may be used and distrubuted freely. A modified
          version of the source code or any part of it and/or its compiled
          binaries may only be distributed in any form if the authors are
          notified about it and credit is given to them with the modified
          distribution(s).
}
unit int8h_2;

interface

var
 tot : longint;
 tot2 : real;
 fps : longint;
 count : real;
 frames : longint;
 changed : boolean;

procedure init8h;
procedure close8h;

implementation
uses dos;

const
 intt = $1c;

var
  oldint : Procedure;
{$F+}
procedure hook_fps; interrupt;
begin

  count:=count+0.054945054;
  tot2:=tot2+0.054945054;
  if count > 1 then
  begin
   count:=0;
   tot:=tot+1;
   changed:=true;
   fps:=frames;
   frames:=0;
  end;

  inline ($9C); { PUSHF -- Push flags }
  { Call old ISR using saved vector }
  oldint;
end;
{$F-}

procedure init8h;
begin
 changed:=false;
 count:=0;
 fps:=255;
 tot:=0;
 tot2:=0;
 GetIntVec(intt,@oldint);
 SetIntVec(intt,Addr(hook_fps));
end;

procedure close8h;
begin
 SetIntVec(intt,@oldint);
end;


end.