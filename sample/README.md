# FidelityFX SSSR Sample 

A small demo to show integration and usage of the [FidelityFX SSSR library](https://github.com/GPUOpen-Effects/FidelityFX-SSSR/tree/master/ffx-sssr).

![Screenshot](screenshot.png)

# Build Instructions

### Prerequisites

To build this sample, the following tools are required:

- [CMake 3.4](https://cmake.org/download/)
- [Visual Studio 2017](https://visualstudio.microsoft.com/downloads/)
- [Windows 10 SDK 10.0.17763.0](https://developer.microsoft.com/en-us/windows/downloads/windows-10-sdk)
- [Python 3.6](https://www.python.org/downloads/release/python-360/)

Then follow these steps:

1) Clone the repository with its submodules:
    ```
    > git clone https://github.com/GPUOpen-Effects/FidelityFX-SSSR.git --recurse-submodules
    ```

2) Generate the solutions:
    ```
    > cd FidelityFX-SSSR\sample\build
    > GenerateSolutions.bat
    ```

3) Open the solution in the DX12 directory, compile and run.

