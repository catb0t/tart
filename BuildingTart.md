# Building Tart #

Building Tart requires two pieces of software: [CMake](http://www.cmake.org/) and [LLVM](http://www.llvm.org). Oh, and a C++ compiler of course.

For LLVM, you'll need to check out a fairly recent version from the [Subversion repository](http://llvm.org/docs/GettingStarted.html#checkout) - it won't work with the LLVM 2.8 release.

CMake 2.8 or any later version should work.

I recommend building LLVM with CMake rather than using the `configure` script. Since I sometimes work with several different versions of LLVM, I don't install it in the default places (i.e. `/usr/local/libs` and so on.) Instead, I use `-D CMAKE_INSTALL_PREFIX=<somedir>` to install it in my development directory, which looks like this:

```
dev/
   llvm/         (contains LLVM sources)
   llvm-build/   (build directory)
   llvm-install/ (installation directory)
   tart/         (Tart sources)
   tart-build/   (Tart build directory)
```

The commands used to build LLVM:

```
svn co http://llvm.org/svn/llvm-project/llvm/trunk llvm
mkdir llvm-build
cd llvm-build
cmake ../llvm -D CMAKE_INSTALL_PREFIX=../llvm-install -D CMAKE_BUILD_TYPE=Debug
make
make install
```

Once that is done, you can check out and build Tart:

```
hg clone https://tart.googlecode.com/hg/trunk tart 
mkdir tart-build
cd tart-build
cmake -D CMAKE_BUILD_TYPE=Debug -D LLVM_CONFIG=<path_to_llvm-config> ../tart
make check
```

Note that if you are using and IDE such as Eclipse, XCode, or Visual Studio to build Tart, you may want to add a generator argument (`-G <generator name>`) to the cmake command line, to generate a project file that you can load into your IDE. See the cmake documentation for details.