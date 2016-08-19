# Turing Codec

**The Turing Codec (see http://turingcodec.org) is a software HEVC codec**

The Turing Codec's source code is published under the GPL version 2 licence.  Commercial support and intellectual property rights for the Turing codec are also available under a proprietary license. 
For more information, contact us at info @ turingcodec.org.

## Build

### Linux
The Turing Codec is continuously integrated by Travis-CI using on the dependencies and script in the [.travis.yml](.travis.yml) file. This file may be a useful starting point for building on Linux.

#### Prerequisites
On Ubuntu 14, run the following commands to install the necessary tools and libraries.

```
sudo apt-get update
sudo apt-get install g++-5
sudo apt-get install cmake
sudo apt-get install make
```

#### Build
Run the following commands to build the codec:

```
git clone https://github.com/bbc/turingcodec.git
mkdir -p turingbuild/debug turingbuild/release
cd build/release
cmake ../../turingcodec
make
```

### Windows

#### Prequisites
 * Visual Studio 2015 (any edition)
 * CMake for Windows

#### Build
 * Open CMake for Windows
 * Set "Where is the source code" as the folder containing this README.md file
 * Set "Where to build the binaries" to any new folder location you prefer
 * Click 'Configure' and select the Win64 configuration for Visual Studio 2015
 * Click 'Generate'
 * Project file "Turing.sln" should be emitted into the new build folder location - open this in Visual Studio and build

## Test

### Encoder Signature test

To test that the encoder produces expected output, run the following command:

```
turing-exe signature <turingcodec path>/test/
```

### Decoder conformance bitstream test

To test that the decoder produces correct output, run the following commands:

```
git clone -n https://github.com/kupix/bitstreams.git
cd bitstreams
git checkout b19d683
cd ..
turing-exe testdecode
```

## About the code

### Architecture

[See separate document ](architecture.md).

### Organisation

Three types of C++ source file are used within the project, identified by their extension

* **Module.h** - Header file containing declarations, templated definitions and inline function definitions
* **Module.hpp** - Header file used where templated function definitions need to be separated from their declaration. Declaration is in the corresponding .h file
* **CompilationUnit.cpp** - Compilation unit. In some cases, they may comprise just #include directives and explicit template instantiations. 





