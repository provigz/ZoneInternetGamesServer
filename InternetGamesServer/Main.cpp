#define WIN32_LEAN_AND_MEAN

#pragma comment(lib, "User32.lib")
#pragma comment(lib, "Ws2_32.lib")

#include <ws2tcpip.h>

#include <iostream>
#include <fstream>

#include "Command.hpp"
#include "Config.hpp"
#include "MatchManager.hpp"
#include "Socket.hpp"
#include "Util.hpp"

#define DEFAULT_CONFIG_FILE "InternetGamesServer.config"

int main(int argc, char* argv[])
{
	std::string argConfigFile = DEFAULT_CONFIG_FILE;
	const char* argPort = nullptr;

	// Process arguments
	for (int i = 1; i < argc; ++i)
	{
		if (!strcmp(argv[i], "-c") || !strcmp(argv[i], "--config"))
		{
			if (argc < i + 2 || argv[i + 1][0] == '-')
			{
				std::cout << "Config file must be specified after \"-c\" or \"--config\"." << std::endl;
				return -1;
			}
			argConfigFile = argv[++i];
		}
		else if (!strcmp(argv[i], "-p") || !strcmp(argv[i], "--port"))
		{
			if (argc < i + 2 || argv[i + 1][0] == '-')
			{
				std::cout << "Port number must be provided after \"-p\" or \"--port\"." << std::endl;
				return -1;
			}
			argPort = argv[++i];
		}
		else
		{
			std::cout << "Invalid argument \"" << argv[i] << "\"!" << std::endl;
			return -1;
		}
	}

	g_config.Load(argConfigFile);
	if (argPort)
	{
		try
		{
			g_config.port = static_cast<USHORT>(std::stoi(argPort));
		}
		catch (const std::exception& err)
		{
			std::cout << "Invalid port number argument provided: " << err.what() << std::endl;
		}
		g_config.Save();
	}
	if (!g_config.logsDirectory.empty())
	{
		try
		{
			CreateNestedDirectories(g_config.logsDirectory);
		}
		catch (const std::exception& err)
		{
			std::cout << err.what() << std::endl;
		}
	}

	LoadXPAdBannerImage();

	// Open session log file stream, if logging is enabled
	if (!g_config.logsDirectory.empty())
	{
		std::ostringstream logFileName;
		logFileName << g_config.logsDirectory << "\\SESSION_" << std::time(nullptr) << ".txt";

		auto stream = std::make_unique<std::ofstream>(logFileName.str());
		if (!stream->is_open())
			std::cout << "Failed to open log file \"" << logFileName.str() << "\"!" << std::endl;
		else
			SetSessionLog(std::move(stream));
	}

	// Create a thread to update the logic of all matches
	DWORD nMatchManagerThreadID;
	if (!CreateThread(0, 0, MatchManager::UpdateHandler, nullptr, 0, &nMatchManagerThreadID))
	{
		std::ostringstream err;
		err << "Couldn't create a thread to update MatchManager: " << GetLastError() << std::endl;
		std::cout << err.str();
		SessionLog() << err.str();
		return 6;
	}

	/** SET UP WINSOCK */
	WSADATA wsaData;
	HRESULT startupResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (startupResult != 0)
	{
		std::ostringstream err;
		err << "ERROR: Initialization failure: " << startupResult << std::endl;
		std::cout << err.str();
		SessionLog() << err.str();
		return 1;
	}

	struct addrinfo* result = NULL, * ptr = NULL, hints;

	ZeroMemory(&hints, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	hints.ai_flags = AI_PASSIVE;

	// Resolve the local address and port to be used by the server
	const std::string portStr = std::to_string(g_config.port);
	HRESULT addrResult = getaddrinfo(NULL, portStr.c_str(), &hints, &result);
	if (addrResult != 0)
	{
		std::ostringstream err;
		err << "\"getaddrinfo\" failed: " << WSAGetLastError() << std::endl;
		std::cout << err.str();
		SessionLog() << err.str();

		WSACleanup();
		return 2;
	}

	// Set up the TCP listening socket
	SOCKET ListenSocket = INVALID_SOCKET;
	ListenSocket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
	if (ListenSocket == INVALID_SOCKET)
	{
		std::ostringstream err;
		err << "Error at \"socket()\": " << WSAGetLastError() << std::endl;
		std::cout << err.str();
		SessionLog() << err.str();

		freeaddrinfo(result);
		WSACleanup();
		return 3;
	}
	HRESULT bindResult = bind(ListenSocket, result->ai_addr, (int)result->ai_addrlen);
	if (bindResult == SOCKET_ERROR)
	{
		std::ostringstream err;
		err << "\"bind\" failed: " << WSAGetLastError() << std::endl;
		std::cout << err.str();
		SessionLog() << err.str();

		freeaddrinfo(result);
		closesocket(ListenSocket);
		WSACleanup();
		return 4;
	}
	freeaddrinfo(result);

	{
		std::ostringstream out;
		out << "[MAIN] Listening on port " << portStr << "!" << std::endl << std::endl;
		std::cout << out.str();
		SessionLog() << out.str();
	}
	FlushSessionLog();

	// Create a thread to accept and respond to command input
	DWORD nCommandProcessorThreadID;
	if (!CreateThread(0, 0, CommandHandler, nullptr, 0, &nCommandProcessorThreadID))
	{
		std::ostringstream err;
		err << "Couldn't create a thread to process command input!" << std::endl;
		std::cout << err.str();
		SessionLog() << err.str();

		closesocket(ListenSocket);
		WSACleanup();
		return 6;
	}

	while (true)
	{
		// Listen for a client socket connection
		if (listen(ListenSocket, SOMAXCONN) == SOCKET_ERROR)
		{
			const std::string fatalErr = "[SOCKET] \"listen\" failed: " + std::to_string(WSAGetLastError());
			std::cout << "[FATAL!] " << fatalErr;
			SessionLog() << "[FATAL!] " << fatalErr;

			closesocket(ListenSocket);
			WSACleanup();
			return 5;
		}

		// Accept the client socket
		sockaddr_in socketAddr;
		int socketAddrSize = sizeof(socketAddr);
		SOCKET ClientSocket = accept(ListenSocket, reinterpret_cast<sockaddr*>(&socketAddr), &socketAddrSize);
		if (ClientSocket == INVALID_SOCKET)
		{
			SessionLog() << "[SOCKET] \"accept\" failed: " << WSAGetLastError() << std::endl;
			continue;
		}

		const Socket::Address clientAddress = Socket::GetAddress(socketAddr);

		// If the client originates from a banned IP, reject the connection by immediately disconnecting
		bool clientBanned = false;
		for (const std::string& ip : g_config.bannedIPs)
		{
			if (ip == clientAddress.ip)
			{
				clientBanned = true;
				break;
			}
		}
		if (clientBanned)
		{
			SessionLog() << "[SOCKET] Rejected connection from banned client " << clientAddress << '!' << std::endl;

			closesocket(ClientSocket);
			continue;
		}

		// If number of connections per IP is restricted,
		// reject the connection if enough sockets from that IP are already connected
		if (g_config.numConnectionsPerIP)
		{
			try
			{
				switch (WaitForSingleObject(Socket::s_socketListMutex, SOCKET_LIST_MUTEX_TIMEOUT_MS))
				{
					case WAIT_OBJECT_0: // Acquired ownership of the mutex
						break;
					case WAIT_TIMEOUT:
						throw MutexError("main(): Timed out waiting for socket list mutex: " + std::to_string(GetLastError()));
						break;
					case WAIT_ABANDONED: // Acquired ownership of an abandoned mutex
						throw MutexError("main(): Got ownership of an abandoned socket list mutex: " + std::to_string(GetLastError()));
						break;
					default:
						throw MutexError("main(): An error occured waiting for socket list mutex: " + std::to_string(GetLastError()));
						break;
				}
				USHORT numConns = 0;
				for (const Socket* socket : Socket::GetList())
				{
					if (socket->GetAddress().ip == clientAddress.ip)
					{
						if (++numConns >= g_config.numConnectionsPerIP)
							break;
					}
				}
				if (!ReleaseMutex(Socket::s_socketListMutex))
					throw MutexError("main(): Couldn't release socket list mutex: " + std::to_string(GetLastError()));

				if (numConns >= g_config.numConnectionsPerIP)
				{
					SessionLog() << "[SOCKET] Rejected connection from client " << clientAddress
						<< ": The number of existing connections from that IP exceeds the set limit!" << std::endl;

					closesocket(ClientSocket);
					continue;
				}
			}
			catch (const MutexError& fatalErr)
			{
				std::cout << "[FATAL!] " << fatalErr.what();
				SessionLog() << "[FATAL!] " << fatalErr.what();

				closesocket(ListenSocket);
				WSACleanup();
				return 7;
			}
		}

		// Connected with client successfully
		SessionLog() << "[SOCKET] Accepted connection from " << clientAddress << '.' << std::endl;

		// Set recv/send timeout for client socket
		DWORD timeout = SOCKET_RECV_TIMEOUT;
		if (setsockopt(ClientSocket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout)) != 0)
		{
			SessionLog() << "[SOCKET] \"setsockopt\" for recv() timeout failed: " << WSAGetLastError() << std::endl;

			closesocket(ClientSocket);
			continue;
		}
		timeout = SOCKET_SEND_TIMEOUT;
		if (setsockopt(ClientSocket, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout)) != 0)
		{
			SessionLog() << "[SOCKET] \"setsockopt\" for send() timeout failed: " << WSAGetLastError() << std::endl;

			closesocket(ClientSocket);
			continue;
		}

		// Create a thread to handle the socket
		DWORD nSocketThreadID;
		if (!CreateThread(0, 0, Socket::SocketHandler, reinterpret_cast<LPVOID>(ClientSocket), 0, &nSocketThreadID))
		{
			SessionLog() << "[SOCKET] Couldn't create a thread to handle socket from " << clientAddress
				<< ": " << GetLastError() << std::endl;

			closesocket(ClientSocket);
			continue;
		}
	}

	// Clean up
	closesocket(ListenSocket);
	WSACleanup();

	return 0;
}
