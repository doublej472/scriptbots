ScriptBots v5
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


ABOUT
---------
There are several forks of the ScriptBots project, started by Andrej Karpathy. In this fork we have added a lot more settings to the settings.h file, added features to encourage swarming behavior of the bots and altered the MLP brain to have very simply memory. This last feature is the most controversial and something that is still experiemental.

Finally and most importantly, this fork has been optimized for multi-core systems, in particular super computers. It is designed to run on a single node using OpenMP, and was tested on the Janus super computer at the University of Colordo to run with 12 cores. This means it can run headless (without OpenGL/GLUT installed) and has the ability to save the world state and later reload it. This fork also has a lot of useful command line tags that are documented below.

We would like to see many of these features integrated back into the main repository of ScriptBots, and welcome and feedback. Thanks!


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

     sudo apt-get install cmake build-essential libopenmpi-dev freeglut3-dev libxi-dev libxmu-dev libboost-serialization-dev

To build and run ScriptBots on Linux run the batch script:

     . autorun.sh

If you are running Linux through VirualBox you might need to add this command to the batch script:
     LIBGL_ALWAYS_INDRECT=1 ./scriptbots

**For Mac**

First install homebrew to make installing dependencies easy:
    
    https://github.com/mxcl/homebrew/wiki/Installation

Now install dependencies:

    brew install CMake Boost

OpenGL and GLUT come pre-installed on Mac, so you should be ready to go!

To build and run ScriptBots simply cd to the ScriptBots directory and run the batch script:

     . autorun.sh

**For Other Flavors of Linux**

Ensure that CMake is installed on system. OpenGL and GLUT are optional but recommended.

To install Boost from binaries download the tar file from http://www.boost.org/. However, we recommend you download the older version 1.42 because this is the current debian distro for Ubuntu, and newer version of Boost serialization files are not backwards compatibile:

    wget http://sourceforge.net/projects/boost/files/boost/1.42.0/boost_1_42_0.tar.gz

Untar the file:

    tar xvfz boost_1_42_0.tar.gz

Within the untarred folder:

    ./bootstrap.sh --prefix=/SOME_FOLDER/boost/1.42.0 --with-libraries=serialization
    ./bjam install OR ./b2 install
 
You might need to export the install path using:

    export BOOST_ROOT=/SOME_FOLDER/boost/1.42.0/

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

**Optional Command Line Arguments:**

* -h: Run without graphics
* -v: Run in verbose mode
* -w: Load existing world
* -e <max epochs>: Specify the maximum number of epochs to run the program 
* -s <seconds>: Specify the maxium number of secnods to run the program
* -n <thread count>: Specify the number of threads

PLOTTING BOT STATISTICS
--------
On Linux, from within scriptbox directory:

	 sudo apt-get install gnuplot
	 . plot.sh


VIDEO RECORDING
---------
On Linux:

	sudo apt-get install xvidcap avidemux ffmpeg2theora mplayer
   	xvidcap


BRAIN MAPPING
------------

	P1 R1 G1 B1 FOOD P2 R2 G2 B2 SOUND SMELL HEALTH P3 R3 G3 B3 CLOCK1 CLOCK2 HEARING BLOOD TEMP TOUCH RAND PREV_OUT  PREV_PLAN
	0   1  2  3  4   5   6  7 8   9     10     11   12 13 14 15   16     17     18     19    20   21    22    23-32    33-41


	LEFT RIGHT R G B SPIKE BOOST SOUND GIVING  NEXT_PLAN 
	  0   1    2 3 4   5     6    7       8      9-17

