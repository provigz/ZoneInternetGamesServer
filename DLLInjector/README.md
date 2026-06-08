# Internet Games DLL Injector

A DLL Injector, used to inject the [Internet Games Client DLL](/InternetGamesClientDLL) into any of the Internet Games.

There are 2 separate Visual Studio projects available for the DLL Injector.
One targets Windows 7 and later, while the other is solely 32-bit (x86) and strictly supports only the XP/ME games.
Both projects include the same "Main.cpp" file, however the XP project is built with the `WIN_XP` flag.

The injector is not supported on Windows ME. Refer to the [main README](/README.md#on-windows-me) for workarounds on ME.

## Prerequisites

Before running the Injector, make sure a compiled `.dll` of the [Internet Games Client DLL](/InternetGamesClientDLL) project is available under the same directory, where the Injector executable resides. It must be named "InternetGamesClientDLL.dll", "InternetGamesClientDLL_XP.dll" if targeting XP games.

Additionally, ensure the architecture of the target game matches the architecture of the injector.

## Functionality

When ran, the injector finds any running game processes and injects the respective client DLL. As of v2.0, no further action is needed.
