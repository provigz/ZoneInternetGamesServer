# Zone Internet Games Server

This project brings back the functionality of the Internet Games included in Windows 7 and XP/ME, the servers for which were officially shut down on:

* **Windows XP/ME**: July 31st, 2019
* **Windows 7**: January 22nd, 2020

Those games include:

* Internet Backgammon (XP/ME, 7)
* Internet Checkers (XP/ME, 7)
* Internet Spades (XP/ME, 7)
* Internet Hearts (XP/ME)
* Internet Reversi (XP/ME)

## Connecting an Internet Game to a hosted Internet Games Server

### On Windows XP/7

1. Ensure you have access to a hosted [Internet Games Server](#internet-games-server), either on `localhost`, your local network or via the Internet.
2. From the [latest release of this project](https://github.com/Vankata453/ZoneInternetGamesServer/releases), under "Assets", download the "Release" package for your architecture (x64 or x86).

> [!NOTE]
>
> You must download the x86 package to get the Client DLL and Injector for Windows XP games.

3. Extract the downloaded package, containing the [Internet Games Server](#internet-games-server), custom client DLL and injector.

> [!NOTE]
>
> Ensure the custom client DLL (`InternetGamesClientDLL.dll` or `InternetGamesClientDLL_XP.dll`) and the injector (`DLLInjector.exe` or `DLLInjector_XP.exe`) are in the same directory!

4. Start an Internet Game of your choice.
5. Run the DLL Injector, supplying a [target game argument](#command-line-arguments-dll-injector) if using the variant for Windows 7 games (`DLLInjector.exe`). A dialog for you to enter a host and port to an [Internet Games Server](#internet-games-server) should appear!

> [!WARNING]
>
> `DLLInjector.exe` is likely to be flagged by antivirus software as a threat. That is normal, since DLL Injecting is behaviour
> commonly used in malware to inject malicious code into other processes.
>
> DLL injecting, however, is required for preparing the Windows 7 Internet Games to connect to a custom server.
> Make sure you add `DLLInjector.exe` as an exception in your antivirus software!

> [!TIP]
>
> You can create a shortcut to `DLLInjector.exe` with a [target game argument](#command-line-arguments-dll-injector) for ease!

### On Windows ME

No supported DLL Injector or Client DLL are available for Windows ME.

I could not get modifications to the `hosts` file to work on ME. Thus, as an alternative, you can patch the game executables and `CMNCLIM.DLL` (which specifies the port).

Use the VBScript patcher for Windows ME (`InternetGamesPatcher_ME.vbs`) which is included in every build package of this project. Upon running it, you will be prompted to enter both a target host and port in separate dialogs. You can leave either one empty or press "Cancel" to not patch host/port.

> [!NOTE]
>
> Patching the game executables comes with the drawback of a restricted host name length to 44 characters.

To use the patcher from the command line (more verbose): `cscript //nologo InternetGamesPatcher_ME.vbs`

The patcher creates .bak copies of the original game executables and `CMNCLIM.DLL`, so that if anything goes wrong, you can easily restore them.

## Includes

This repository includes the following:

### Internet Games Server

![Preview of the Internet Games Server in action](docs/img/README_ServerPreview.png)

A Winsock server, which makes the Internet Games playable by acting as a Zone games server.
It matches players in lobbies, depending on the game being played, as well as the chosen skill level.
It can manage many matches of any of the games at the same time.

Each game has custom messages, which are supported by the server in order for it to function properly.

#### Internet Backgammon (Windows 7)

![Internet Backgammon (7)](docs/img/README_BackgammonPreview.png)

#### Internet Checkers (Windows 7)

![Internet Checkers (7)](docs/img/README_CheckersPreview.png)

#### Internet Spades (Windows 7)

![Internet Spades (7)](docs/img/README_SpadesPreview.png)

#### Internet Backgammon (Windows XP/ME)

![Internet Backgammon (XP/ME)](docs/img/README_BackgammonXPPreview.png)

#### Internet Checkers (Windows XP/ME)

![Internet Checkers (XP/ME)](docs/img/README_CheckersXPPreview.png)

#### Internet Spades (Windows XP/ME)

![Internet Spades (XP/ME)](docs/img/README_SpadesXPPreview.png)

#### Internet Hearts (Windows XP/ME)

![Internet Hearts (XP/ME)](docs/img/README_HeartsXPPreview.png)

#### Internet Reversi (Windows XP/ME)

![Internet Reversi (XP/ME)](docs/img/README_ReversiXPPreview.png)


> [!NOTE]
>
> For all games, there are differences from original server behaviour:
>
> * **If an opponent leaves a Windows 7 Backgammon or Checkers game, instead of replacing them with an AI player, the server ends the game.**
>
>   AI player logic was originally developed server-side. Originally, all three Windows 7 games and Spades and Hearts from Windows XP/ME supported them.
>   This server does not currently support game logic for Backgammon and Checkers, so computer players are not currently supported for those games.
>   Instead, the match is ended by disconnecting the other player, causing an "Error communicating with server" error message.
>
> * **Since the server does not support game logic everywhere, it may not entirely validate some messages from clients.**
>
>   If a player were to modify game messages being sent to the server to try and cheat, in some cases it might be up to the opponents' game clients to determine whether the action/s are legitimate or not.
>   Luckily, from my testing, this local validation seems to work nicely. On invalid data, the game ends with a "Corrupted data" message.
>   If the server catches an invalid message, the player in question will be disconnected. The game should continue if it supports computer players.

#### Command line arguments

* `-c` (`--config`): Sets the target configuration file. Any changes to options will be written there. *Default: "InternetGamesServer.config"*
* `-p` (`--port`): Port to host the server on. Automatically written to config file.

### Internet Games Client DLL + DLL Injector

A DLL, which is to be injected into any of the 3 games, using the DLL Injector application.

Both applications have two variants for Windows 7 and Windows XP games. The Windows XP one is only available in x86 builds.

Both the Injector and the Client DLL are **not supported on Windows ME**. Use the [VBScript patcher](#on-windows-me) to configure host and port on Windows ME.

#### Functionality

The Windows 7 Client DLL performs the following operations:

* Creates the `HKEY_CURRENT_USER\Software\Microsoft\zone.com\Zgmprxy` registry key, if it doesn't exist.
* Creates a `DisableTLS` DWORD 32-bit registry value under `HKEY_CURRENT_USER\Software\Microsoft\zone.com\Zgmprxy`, set to 1.
* Displays a dialog, where the user can enter a host and port, where an [Internet Games Server](#internet-games-server) is located, to connect to.
* Puts a hook on the [`GetAddrInfoW`](https://learn.microsoft.com/en-us/windows/win32/api/ws2tcpip/nf-ws2tcpip-getaddrinfow) function from `ws2_32.dll`, setting appropriate arguments, as well as the host and the port, specified in the dialog.

The Windows XP Client DLL performs the following operations:

* Displays a dialog, where the user can enter a host and port, where an [Internet Games Server](#internet-games-server) is located, to connect to.
* Puts a hook on the [`inet_addr`](https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-inet_addr) and [`htons`](https://learn.microsoft.com/en-us/windows/win32/api/winsock/nf-winsock-htons) functions from `ws2_32.dll`, setting the host and the port, specified in the dialog.

#### Command line arguments (DLL Injector)

* **[WINDOWS 7 ONLY!] Exactly one of the following arguments:**
  * `-b` (`--backgammon`) *OR* `-c` (`--checkers`) *OR* `-s` (`--spades`) **[REQUIRED]**: Specifies a target Windows 7 game to inject the client DLL into.
  * `-x` (`--xp`): For injecting the client DLL into any Windows XP game **(x86 only!)**.
* `-r` (`--repeat`): See [Using DLL Injector on multiple instances](#using-dll-injector-on-multiple-instances). *Default: 0*

## Building

To build any of the projects, open up the respective project file (.vcxproj) in Visual Studio and build from there.

### Running Multiple Instances

For information on how to run multiple instances of any of the Internet Games, [read this](docs/MultipleInstances.md).

#### Using DLL Injector on multiple instances

To use the DLL Injector on multiple instances at the same time, provide the `-r` (or `--repeat`) argument to it,
allowing to skip a select number of previously started processes of the specified Internet Game.
For example, to inject the DLL into a second instance of the same game, provide `-r 1` (or `--repeat 1`).

## Credits

* [codereversing.com](https://www.codereversing.com/archives/138) for providing some logs and tons of helpful information, regarding reverse-engineering the Windows 7 Internet Games.
