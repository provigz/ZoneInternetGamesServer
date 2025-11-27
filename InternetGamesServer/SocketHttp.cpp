#define WIN32_LEAN_AND_MEAN

#include <ws2tcpip.h>

#include <iostream>
#include <sstream>
#include <tuple>

#include "Config.hpp"
#include "MatchManager.hpp"
#include "Socket.hpp"
#include "Util.hpp"
#include "Win7/Match.hpp"
#include "WinXP/Match.hpp"

DWORD WINAPI SocketHTTPListenHandler(void* ListenSocketRaw);

void SetUpSocketHTTP()
{
	struct addrinfo* result = NULL, * ptr = NULL, hints;
	ZeroMemory(&hints, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	hints.ai_flags = AI_PASSIVE;

	// Resolve the local address and port to be used by the server
	HRESULT addrResult = getaddrinfo(NULL, "80", &hints, &result);
	if (addrResult != 0)
	{
		std::ostringstream err;
		err << "[HTTP] \"getaddrinfo\" failed: " << WSAGetLastError() << std::endl;
		std::cout << err.str();
		SessionLog() << err.str();
		return;
	}

	// Set up the TCP listening socket
	SOCKET ListenSocket = INVALID_SOCKET;
	ListenSocket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
	if (ListenSocket == INVALID_SOCKET)
	{
		std::ostringstream err;
		err << "[HTTP] Error at \"socket()\": " << WSAGetLastError() << std::endl;
		std::cout << err.str();
		SessionLog() << err.str();

		freeaddrinfo(result);
		return;
	}
	HRESULT bindResult = bind(ListenSocket, result->ai_addr, (int)result->ai_addrlen);
	if (bindResult == SOCKET_ERROR)
	{
		std::ostringstream err;
		err << "[HTTP] \"bind\" failed: " << WSAGetLastError() << std::endl;
		std::cout << err.str();
		SessionLog() << err.str();

		freeaddrinfo(result);
		closesocket(ListenSocket);
		return;
	}
	freeaddrinfo(result);

	std::ostringstream out;
	out << "[HTTP] Listening on port 80!" << std::endl << std::endl;
	std::cout << out.str();
	SessionLog() << out.str();

	DWORD nSocketHTTPThreadID;
	if (!CreateThread(0, 0, SocketHTTPListenHandler, reinterpret_cast<LPVOID>(ListenSocket), 0, &nSocketHTTPThreadID))
	{
		std::ostringstream err;
		err << "[HTTP] Couldn't create a thread to handle HTTP listen socket: " << GetLastError() << std::endl;
		std::cout << err.str();
		SessionLog() << err.str();

		closesocket(ListenSocket);
	}
}


DWORD WINAPI SocketHTTPHandler(void* rawSocket);

DWORD WINAPI SocketHTTPListenHandler(void* ListenSocketRaw)
{
	SOCKET ListenSocket = reinterpret_cast<SOCKET>(ListenSocketRaw);
	while (true)
	{
		// Listen for a client socket connection
		if (listen(ListenSocket, SOMAXCONN) == SOCKET_ERROR)
		{
			SessionLog() << "[HTTP SOCKET] \"listen\" failed: " << WSAGetLastError() << std::endl;

			closesocket(ListenSocket);
			return -1;
		}

		// Accept the client socket
		sockaddr_in socketAddr;
		int socketAddrSize = sizeof(socketAddr);
		SOCKET ClientSocket = accept(ListenSocket, reinterpret_cast<sockaddr*>(&socketAddr), &socketAddrSize);
		if (ClientSocket == INVALID_SOCKET)
		{
			SessionLog() << "[HTTP SOCKET] \"accept\" failed: " << WSAGetLastError() << std::endl;
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
			SessionLog() << "[HTTP SOCKET] Rejected connection from banned client " << clientAddress << '!' << std::endl;

			closesocket(ClientSocket);
			continue;
		}

		// Connected with client successfully
		SessionLog() << "[HTTP SOCKET] Accepted connection from " << clientAddress << '.' << std::endl;

		// Set a generic recv/send timeout
		const DWORD timeout = SOCKET_BLOCKING_TIMEOUT;
		if (setsockopt(ClientSocket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout)) != 0)
		{
			SessionLog() << "[HTTP SOCKET] \"setsockopt\" for recv() timeout failed: " << WSAGetLastError() << std::endl;

			closesocket(ClientSocket);
			continue;
		}
		if (setsockopt(ClientSocket, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout)) != 0)
		{
			SessionLog() << "[HTTP SOCKET] \"setsockopt\" for send() timeout failed: " << WSAGetLastError() << std::endl;

			closesocket(ClientSocket);
			continue;
		}

		// Create a thread to handle the socket
		DWORD nSocketThreadID;
		if (!CreateThread(0, 0, SocketHTTPHandler, reinterpret_cast<LPVOID>(ClientSocket), 0, &nSocketThreadID))
		{
			SessionLog() << "[HTTP SOCKET] Couldn't create a thread to handle socket from " << clientAddress
				<< ": " << GetLastError() << std::endl;

			closesocket(ClientSocket);
			continue;
		}
	}

	closesocket(ListenSocket);
	return 0;
}


std::string GetWaitingLobbiesPageHTML();

DWORD WINAPI SocketHTTPHandler(void* rawSocket)
{
	SOCKET socket = reinterpret_cast<SOCKET>(rawSocket);

	std::string requestBuffer;
	char receivedBuf[2048];

	try
	{
		size_t requestEndPos;
		while ((requestEndPos = requestBuffer.find("\r\n\r\n")) == std::string::npos)
		{
			const int receivedLen = recv(socket, receivedBuf, sizeof(receivedBuf), 0);
			if (receivedLen > 0)
			{
				requestBuffer.append(receivedBuf, receivedLen);
				continue;
			}
			if (receivedLen == 0) // Disconnected
				goto CloseSocket;

			throw std::runtime_error("\"recv\" failed: " + std::to_string(WSAGetLastError()));
		}

		const std::string request = requestBuffer.substr(0, requestEndPos);
		std::string html;
		if (StartsWith(request, "GET / HTTP/1."))
			html = GetWaitingLobbiesPageHTML();
		else
			goto CloseSocket;

		const std::string httpHeader =
			"HTTP/1.1 200 OK\r\n"
			"Content-Type: text/html; charset=UTF-8\r\n"
			"Content-Length: " + std::to_string(html.size()) + "\r\n"
			"Connection: close\r\n"
			"\r\n";
		if (send(socket, httpHeader.c_str(), static_cast<int>(httpHeader.size()), 0) == SOCKET_ERROR)
			throw std::runtime_error("\"send\" failed: " + std::to_string(WSAGetLastError()));
		if (send(socket, html.c_str(), static_cast<int>(html.size()), 0) == SOCKET_ERROR)
			throw std::runtime_error("\"send\" failed: " + std::to_string(WSAGetLastError()));
	}
	catch (const std::exception& err)
	{
		SessionLog() << "[HTTP SOCKET] Error communicating with socket " << Socket::GetAddress(socket)
			<< ": " << err.what() << std::endl;
	}

CloseSocket:
	if (shutdown(socket, SD_BOTH) == SOCKET_ERROR)
		SessionLog() << "[HTTP SOCKET] \"shutdown\" failed: " << WSAGetLastError() << std::endl;
	closesocket(socket);
	return 0;
}


std::string GetWaitingLobbiesPageHTML()
{
	const auto matches = MatchManager::Get().AcquireMatches();

#define COUNT_MATCHES_WIN7(GAME) \
	std::count_if(matches.first.begin(), matches.first.end(), \
		[](const auto& m) { return m->GetState() == Win7::Match::STATE_WAITINGFORPLAYERS && m->GetGame() == Win7::Match::Game::GAME; })
#define COUNT_MATCHES_WIN7_SKILL(GAME, SKILL) \
	std::count_if(matches.first.begin(), matches.first.end(), \
		[](const auto& m) { return m->GetState() == Win7::Match::STATE_WAITINGFORPLAYERS && m->GetGame() == Win7::Match::Game::GAME && m->GetLevel() == Win7::Match::Level::SKILL; })
#define COUNT_MATCHES_WINXP(GAME) \
	std::count_if(matches.second.begin(), matches.second.end(), \
		[](const auto& m) { return m->GetState() == Win7::Match::STATE_WAITINGFORPLAYERS && m->GetGame() == WinXP::Match::Game::GAME; })
#define COUNT_MATCHES_WINXP_SKILL(GAME, SKILL) \
	std::count_if(matches.second.begin(), matches.second.end(), \
		[](const auto& m) { return m->GetState() == Win7::Match::STATE_WAITINGFORPLAYERS && m->GetGame() == WinXP::Match::Game::GAME && m->GetSkillLevel() == WinXP::Match::SkillLevel::SKILL; })

	const std::string html = R"(
<html>
	<head>
		<title>Lobbies - ZoneInternetGamesServer</title>
		<style>
			table {
				font-size: large;
				border: 1px solid black;
				border-collapse: collapse;
			}
			table tr * {
				padding: 10px;
			}
			table tr td, table tr th {
				text-align: center;
				white-space: pre;
				border: 1px solid black;
			}
		</style>
	</head>
	<body>
		<h1>Waiting lobbies on <a href="https://github.com/provigz/ZoneInternetGamesServer" target="_blank">ZoneInternetGamesServer</a>:</h1>
		<table>
			<tr>
				<td>Version / Game</td>
				<th>Backgammon</th>
				<th>Checkers</th>
				<th>Spades</th>
				<th>Hearts</th>
				<th>Reversi</th>
			</tr>
			<tr>
				<th>Windows 7</th>
)" + (g_config.skipLevelMatching ?
	"<td>" + std::to_string(COUNT_MATCHES_WIN7(BACKGAMMON)) + "</td><td>"
		+ std::to_string(COUNT_MATCHES_WIN7(CHECKERS)) + "</td><td>"
		+ std::to_string(COUNT_MATCHES_WIN7(SPADES)) :
				"<td>Beginner: " + std::to_string(COUNT_MATCHES_WIN7_SKILL(BACKGAMMON, BEGINNER)) +
"\nIntermediate: " + std::to_string(COUNT_MATCHES_WIN7_SKILL(BACKGAMMON, INTERMEDIATE)) +
"\nExpert: " + std::to_string(COUNT_MATCHES_WIN7_SKILL(BACKGAMMON, EXPERT)) + "</td>" +
				"<td>Beginner: " + std::to_string(COUNT_MATCHES_WIN7_SKILL(CHECKERS, BEGINNER)) +
"\nIntermediate: " + std::to_string(COUNT_MATCHES_WIN7_SKILL(CHECKERS, INTERMEDIATE)) +
"\nExpert: " + std::to_string(COUNT_MATCHES_WIN7_SKILL(CHECKERS, EXPERT)) + "</td>" +
				"<td>Beginner: " + std::to_string(COUNT_MATCHES_WIN7_SKILL(SPADES, BEGINNER)) +
"\nIntermediate: " + std::to_string(COUNT_MATCHES_WIN7_SKILL(SPADES, INTERMEDIATE)) +
"\nExpert: " + std::to_string(COUNT_MATCHES_WIN7_SKILL(SPADES, EXPERT))
) + R"(</td>
				<td>-</td>
				<td>-</td>
			</tr>
			<tr>
				<th>Windows XP/ME</th>
)" + (g_config.skipLevelMatching ?
	"<td>" + std::to_string(COUNT_MATCHES_WINXP(BACKGAMMON)) + "</td><td>"
		+ std::to_string(COUNT_MATCHES_WINXP(CHECKERS)) + "</td><td>"
		+ std::to_string(COUNT_MATCHES_WINXP(SPADES)) + "</td><td>"
		+ std::to_string(COUNT_MATCHES_WINXP(HEARTS)) + "</td><td>"
		+ std::to_string(COUNT_MATCHES_WINXP(REVERSI)) :
				"<td>Beginner: " + std::to_string(COUNT_MATCHES_WINXP_SKILL(BACKGAMMON, BEGINNER)) +
"\nIntermediate: " + std::to_string(COUNT_MATCHES_WINXP_SKILL(BACKGAMMON, INTERMEDIATE)) +
"\nExpert: " + std::to_string(COUNT_MATCHES_WINXP_SKILL(BACKGAMMON, EXPERT)) + "</td>" +
				"<td>Beginner: " + std::to_string(COUNT_MATCHES_WINXP_SKILL(CHECKERS, BEGINNER)) +
"\nIntermediate: " + std::to_string(COUNT_MATCHES_WINXP_SKILL(CHECKERS, INTERMEDIATE)) +
"\nExpert: " + std::to_string(COUNT_MATCHES_WINXP_SKILL(CHECKERS, EXPERT)) + "</td>" +
				"<td>Beginner: " + std::to_string(COUNT_MATCHES_WINXP_SKILL(SPADES, BEGINNER)) +
"\nIntermediate: " + std::to_string(COUNT_MATCHES_WINXP_SKILL(SPADES, INTERMEDIATE)) +
"\nExpert: " + std::to_string(COUNT_MATCHES_WINXP_SKILL(SPADES, EXPERT)) + "</td>" +
				"<td>Beginner: " + std::to_string(COUNT_MATCHES_WINXP_SKILL(HEARTS, BEGINNER)) +
"\nIntermediate: " + std::to_string(COUNT_MATCHES_WINXP_SKILL(HEARTS, INTERMEDIATE)) +
"\nExpert: " + std::to_string(COUNT_MATCHES_WINXP_SKILL(HEARTS, EXPERT)) + "</td>" +
				"<td>Beginner: " + std::to_string(COUNT_MATCHES_WINXP_SKILL(REVERSI, BEGINNER)) +
"\nIntermediate: " + std::to_string(COUNT_MATCHES_WINXP_SKILL(REVERSI, INTERMEDIATE)) +
"\nExpert: " + std::to_string(COUNT_MATCHES_WINXP_SKILL(REVERSI, EXPERT))
) + R"(</td>
			</tr>
		</table>
	</body>
</html>
)";

#undef COUNT_MATCHES_WIN7
#undef COUNT_MATCHES_WIN7_SKILL
#undef COUNT_MATCHES_WINXP
#undef COUNT_MATCHES_WINXP_SKILL

	MatchManager::Get().FreeAcquiredMatches();
	return html;
}
