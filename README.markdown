SCRIPTBOTS
==========
* Author: Andrej Karpathy
* License: Do whatever you want with this code

Project website: 
(https://sites.google.com/site/scriptbotsevo/home)

Mailing List / Forum:
(http://groups.google.com/group/scriptbots/topics)

BUILDING
---------

Scriptbots uses the following dependencies:

* CMake >= 2.8 (http://www.cmake.org/cmake/resources/software.html)
* OpenGL and GLUT (http://www.opengl.org/resources/libraries/glut/)
* Linux: freeglut (http://freeglut.sourceforge.net/) 

OpenMP is used to speed up everything if you have multicore cpu.

**For Ubuntu/Debian**

Install all the dependencies with:

    $ sudo apt-get install cmake build-essential libopenmpi-dev freeglut3-dev libxi-dev libxmu-dev

To build and run ScriptBots on Linux simply run the batch script:

    $ . autorun.sh

If you are running Linux through VirualBox you might need to add this command to the batch script:
    $ LIBGL_ALWAYS_INDRECT=1 ./scriptbots

**For Windows**

Follow basically the same steps, but after running cmake open up the VS solution (.sln) file it generates and compile the project from VS.


USAGE
------
Follow the above instructions to compile then run the program.

**Keyboard Shortcuts:**

* p = pause
* d = toggle drawing (for faster computation)
* f = draw food too
* q = add 10 predators
* a = add 10 agents by crossover
* + = faster
* - = slower

**Mouse Usage:**

Pan around by holding down right mouse button, and zoom by holding down middle button. Click to see brain details.

**Bot Status Indicator Colors:**

* WHITE: bot just ate part of another agent
* YELLOW: bot just spiked another bot
* GREEN: bot just reproduced
* GREY: bot is getting group health bonus

**Bot Display Status Numbers:**
* Row 1: Generation Count
* Row 2: Age
* Row 3: Health
* Row 4: Next reproduction


RECORDING
---------
On Linux:

	$ sudo apt-get install xvidcap avidemux ffmpeg2theora mplayer
   	$ xvidcap


QUESTIONS COMMENTS 
------------------
Best posted at the google group, available on project site
or contact me at andrej.karpathy@gmail.com

Contributors:

* Casey Link <unnamedrambler@gmail.com>
* Dave Coleman <davetcoleman@gmail.com>
* Nimisha Morkonda <mgnimisha@gmail.com>

BRAIN MAPPING
------------

P1 R1 G1 B1 FOOD P2 R2 G2 B2 SOUND SMELL HEALTH P3 R3 G3 B3 CLOCK1 CLOCK2 HEARING BLOOD TEMP TOUCH RAND PREV_OUT  PREV_PLAN
0   1  2  3  4   5   6  7 8   9     10     11   12 13 14 15   16     17     18     19    20   21    22    23-32    33-41


LEFT RIGHT R G B SPIKE BOOST SOUND GIVING  NEXT_PLAN 
  0   1    2 3 4   5     6    7       8      9-17

TODO
------------
Data reporting/stats



Changes:
