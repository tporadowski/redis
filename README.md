Redis on Windows prototype
===
## What's new in this release

- The project is now a fork of https://github.com/antirez/redis
- libuv dependency was removed in favor of calling Win32 APIs directly. This was done in order to make integrations with upstream repo more manageable.
- Improvement to the snapshotting (save on disk) algorithm. The code now serializes to buffer on a background thread then writes to disk. The main thread is blocked only on writes while the background thread is writing to buffer. This is an improvement to a full block where the snapshot was written to disk on the main thread.
- Improved unit tests pass rate

===
Special thanks to Dušan Majkic (https://github.com/dmajkic, https://github.com/dmajkic/redis/) for his project on GitHub that gave us the opportunity to quickly learn some on the intricacies of Redis code. His project also helped us to build our prototype quickly.


## How to build Redis


### Build pthreads library using Visual Studio

You can use the free Express Edition available at http://www.microsoft.com/visualstudio/en-us/products/2010-editions/visual-cpp-express.

Redis uses pthreads. The build assumes it is linked as a static library. 
You need to get Windows pthreads sources and build as a static library.

1. You can download the code from ftp://sourceware.org/pub/pthreads-win32/pthreads-w32-2-8-0-release.exe.

2. Extract the files to a folder on your local drive.

3. From your local folder, copy all the files from Pre-built.2/include to your redis folder under 
deps/pthreads-win32/include (the directory needs to be created).

4. From Visual Studio, do Open Project, and navigate to the installed folder, and then pthreads.2.

5. Open the pthread project file (pthread.dsp).

6. VS will ask to convert to the current version format. Select Yes.

7. Open project properties and change the following:

    - Change configuration type from Dynamic Library (.dll) to Static Library (.lib)
    - Change Target Extension for .dll to .lib
    - Under C-C++/Code Generation, change Runtime Library choice to Multi-threaded (/MT)

8. Build the project. This will create a pthread.lib under pthreads.2 directory.

9. Copy pthread.lib to your redis folder under deps/pthreads-win32/lib/release|debug (you have to create the directories).

### Build Redis using Visual Studio

- Open the solution file msvs\redisserver.sln in Visual Studio 10, and build.

    This should create the following executables in the msvs\$(Configuration) folder:

    - redis-server.exe
    - redis-benchmark.exe
    - redis-cli.exe
    - redis-check-dump.exe
    - redis-check-aof.exe

    If there is an error in finding the pthreads.lib file, check that you have selected the same configuration as used for building the pthreads.lib file, and that you copied it to the expected location.

### Release Notes

This is a pre-release version of the software and is not fully tested.  
This is intended to be a 32bit release only. No work has been done in order to produce a 64bit version of Redis on Windows.
To run the test suite requires some manual work:

- The tests assume that the binaries are in the src folder, so you need to copy the binaries from the msvs folder to src. 
- The tests make use of TCL. This must be installed separately.
- To run the tests in a command window: `tclsh8.5.exe tests/test_helper.tcl`.

### Plan for the next release

- Update code base to Redis 2.4.7
- We plan to improve further the snapshotting process. We are working on some ideas on how to we can simulate the Copy-On-Write feature at the application level. 
 