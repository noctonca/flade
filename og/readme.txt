coldblood & darkstealth presents FLADE for Little Big Adventure/Relentless
--------------------------------------------------------------------------
ver 0.99.6 BETA


About:
------
Flade is a DOS utillity designed to decode the FLA video file format used in the game
Little Big Adventure/Relentless.

Warning:
--------
If you have not finished the game yet this program might SPOIL the experience by giving
you access to ALL the videos played throughout the game!

Features:
---------
this utillity...
+can play FLA videos
+can dump FLA videos frame-by-frame to disk.
+can be dumped to either PCX or BMP
-does not play any sound (since it is not located within the FLA files)
-can not convert the FLA video format to any other video format

New in:
-------
0.99.6 
+More friendly and improved user interface
+Can change configuration as many times as you like at runtime
+Removed prompt in menu and thus got rid of some bugs
+"Exit" is now selectable, but <esc> is also functionable
+Some other minor improvements

Usage:
------
Before you load the program check in which folder your FLA files are located.
Load the program and enter the location starting with the drive letter and a colon (C:,D:
or E: etc) and then each folder starting with a backslash. 
So for exampled if the FLA files are located in My Computer... D Drive... LBA Folder and 
then the FLA folder you would enter D:\LBA\FLA (exlcude my computer)

If the path is invalid you will be prompted again. 

You will be promted if you want to dump the videos. If this is your first run press "n" 
for No (we will get to that later).

When the menu loads you should see the title followed by the version number and under that
some thing similar to this:

THE_END.FLA        :  0.97 MB
VERSER.FLA         :  0.80 MB
VERSER2.FLA        :  0.75 MB

Configuration
Exit

Select and enter the video file you want to view using the arrow keys and the video will 
start playing.

NOTE: If you only see "Configuration" and "Exit" it means you have enter an invalid path. 
Just select "Configuration" using your arrow keys and press enter and the program will
prompt you again for a location.

BTW. Directory = Folder.

Dump:
-----
When FLA files are dumped to disk, each frame is copied to a file (PCX or BMP). 
After that it can be compiled into video formats such as Smacker or Bink etc using
a utility called Smacker (freely available at www.radgametools.com). Sounds and Music
can be included. Also be sure to check out the utility called LBADeComp at 
http://members.xoom.com/lbawld, which enables you to extract sounds and music that are
used in the video.

If you want to dump the videos to disk select and enter "Configuratiion" retype the FLA 
video location and when prompted if you want to Dump the videos press "y" for Yes.

You will now be promted what file format you want to dump to: PCX or BMP
We suggest you select BMP for the reason that it is well supported format in many animation
and image editors. So press "b" for BMP or "p" for PCX.

After that you will be promted for a location that you want to dump to. It is recommended
that the folder/directory already exist that you want to dump to. In that directory the
program should create a new directory for each video being dumped.

Now you will see the menu again. Select and enter the video of choice and it dump the video
while playing the video.

Bugs:
-----
While Playing
- There is a palette bug when playing videos. This is a display bug and should not affect
dumped files

While Dumping
- It might be neccesary to already have the directory created that you are dumping to.
- If the dump directory is invalid program will crash when playing videos.
- When when specified storage device is full program will crash while playing videos.

Disclaimer:
-----------
This program is for personal use only. Neither Coldblood or Darkstealth takes any 
responsibility for any damage what so ever caused by this program. USE AT OWN RISK.