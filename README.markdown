SCRIPTBOTS V5
==========
* Author: Andrej Karpathy <andrej.karpathy@gmail.com>
* Contributors: Dave Coleman <davetcoleman@gmail.com>
* Contributors: Gregory Hopkins <gregory.hopkins@colorado.edu>
* License: GNU General Public License, version 3 (GPL-3.0)

Project website: 
(https://sites.google.com/site/scriptbotsevo/home)

Mailing List / Forum:
(http://groups.google.com/group/scriptbots/topics)

Questions / Comments:
Best posted at the google group, above


BUILDING
---------

Scriptbots uses the following dependencies:

* CMake >= 2.8 (http://www.cmake.org/cmake/resources/software.html)
* Boost (http://www.boost.org/)
* OpenMP (Dependencies should be already installed)

Optional dependencies for visualization (can read headless):

* OpenGL and GLUT (http://www.opengl.org/resources/libraries/glut/)
* Linux: freeglut (http://freeglut.sourceforge.net/) 


**For Ubuntu/Debian Linux**

Install basic dependencies with:

    $ sudo apt-get install cmake build-essential libopenmpi-dev freeglut3-dev libxi-dev libxmu-dev libboost-serialization-dev

To build and run ScriptBots on Linux run the batch script:

    $ . autorun.sh

If you are running Linux through VirualBox you might need to add this command to the batch script:
    $ LIBGL_ALWAYS_INDRECT=1 ./scriptbots

**For Mac**

First install homebrew to make installing dependencies easy:
    
    https://github.com/mxcl/homebrew/wiki/Installation

Now install dependencies:

    brew install CMake Boost

OpenGL and GLUT come pre-installed on Mac, so you should be ready to go!

To build and run ScriptBots simply cd to the ScriptBots directory and run the batch script:

    $ . autorun.sh

**For Other Flavors of Linux**

Ensure that CMake is installed on system. OpenGL and GLUT are optional but recommended.

To install Boost from binaries download the tar file from http://www.boost.org/. Or, download the current version as of this writing using this command:

    wget http://sourceforge.net/projects/boost/files/boost/1.49.0/boost_1_49_0.tar.gz/download

Untar the file:

    tar xvfz FILENAME

Within the untarred folder:

    ./bootstrap.sh --prefix=/SOME_FOLDER/boost/1.49.0 --with-libraries=serialization
    ./b2 install
 
You might need to export the install path using:

    export BOOST_ROOT=/SOME_FOLDER/boost/1.49.0/

**For Windows**

Follow basically the same steps, but after running cmake open up the VS solution (.sln) file it generates and compile the project from VS. NOTE: these instructions have not been updated for the Boost addition.


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

PLOTTING BOT STATISTICS
--------
On Linux, from within scriptbox directory:

	$ sudo apt-get install gnuplot
	$ . plot.sh


VIDEO RECORDING
---------
On Linux:

	$ sudo apt-get install xvidcap avidemux ffmpeg2theora mplayer
   	$ xvidcap


BRAIN MAPPING
------------

	P1 R1 G1 B1 FOOD P2 R2 G2 B2 SOUND SMELL HEALTH P3 R3 G3 B3 CLOCK1 CLOCK2 HEARING BLOOD TEMP TOUCH RAND PREV_OUT  PREV_PLAN
	0   1  2  3  4   5   6  7 8   9     10     11   12 13 14 15   16     17     18     19    20   21    22    23-32    33-41


	LEFT RIGHT R G B SPIKE BOOST SOUND GIVING  NEXT_PLAN 
	  0   1    2 3 4   5     6    7       8      9-17

