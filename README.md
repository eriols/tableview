eotabkoll (for OS/400) erik olsson
---------------------------------------------------

Beginnings of a tool for viewing table data, similar to Filescope, DBU, WRKDBF etc.<br>
Runs under 5250 terminal emulators. I seem to recall (I have not had access to a IBM i box since 2011) that it worked best under Client Access.<br>
All screens are built with the DSM apis. 
All table/file access is done with the _Ropen family of apis, which comes with a number
of limitations of course. 

Some of the things you can do:<br>
View generated DDL for a db object (aliases does not work, api limitation)<br>
Look at a row detail of a column<br>
Position yourself using keys on keyed files<br>
List column info of a file<br> 
List and go to a particular member of a file. (The performance of the QUSLMBR api truly 
suck)<br>
Do regex searches (api limitations, again, XPG4 I think) against member names and descriptions<br>
View Decimal Floating Point datatypes (which as yet none of the above tools do)<br>
View journalling info<br>
Display database relations including stuff like the keys of a related db object<br>
Silly small things such as scrolling left and right, show RRN, turn Fkey display on/off<br>


Some of the things that you can't yet do:<br>
Edit a file<br>
Split screen (but then again, this is not z/OS so we can logon multiple times. 3270 sucks even harder than 5250)<br>
Floating point support is not great. Eg, you can't position yourself on a fp key.<br>
There is a bug with varchar keys too I recall.<br>


