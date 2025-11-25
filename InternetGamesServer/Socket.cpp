#define _WINSOCK_DEPRECATED_NO_WARNINGS // Allows usage of inet_ntoa()

#include "Socket.hpp"

#include <cassert>
#include <sstream>
#include <iostream>
#include <fstream>

#include "Config.hpp"
#include "Resource.h"
#include "Win7/PlayerSocket.hpp"
#include "WinXP/PlayerSocket.hpp"
#include "WinXP/Protocol/Init.hpp"
#include "WinXP/Security.hpp"

static const char* SOCKET_WIN7_HI_RESPONSE = "STADIUM/2.0\r\n";
static const std::vector<BYTE> XP_AD_BANNER_DATA = {};

std::vector<Socket*> Socket::s_socketList = {};
const HANDLE Socket::s_socketListMutex = CreateMutex(nullptr, false, nullptr);


void LoadXPAdBannerImage()
{
	HINSTANCE hInstance = GetModuleHandle(NULL);
	HRSRC hRes = FindResource(hInstance, MAKEINTRESOURCE(IDB_XP_AD_BANNER), L"PNG");
	if (!hRes) return;

	HGLOBAL hData = LoadResource(hInstance, hRes);
	if (!hData) return;

	const DWORD size = SizeofResource(hInstance, hRes);
	if (size == 0) return;

	BYTE* pData = reinterpret_cast<BYTE*>(LockResource(hData));
	if (!pData) return;

	const_cast<std::vector<BYTE>&>(XP_AD_BANNER_DATA) = std::vector<BYTE>(pData, pData + size);
}


std::string
Socket::TypeToString(Type type)
{
	switch (type)
	{
		case WIN7:
			return "WIN7";
		case WINXP:
			return "WINXP";
		case WINXP_BANNER_AD_REQUEST:
			return "WINXP_BANNER_AD_REQUEST";
		case WINXP_BANNER_AD_IMAGE_REQUEST:
			return "WINXP_BANNER_AD_IMAGE_REQUEST";
		default:
			return "<unknown>";
	}
}


// Handler for the thread of a socket
DWORD WINAPI
Socket::SocketHandler(void* socket_)
{
	const SOCKET rawSocket = reinterpret_cast<SOCKET>(socket_);

	Socket socket(rawSocket);
	socket.Initialize();
	try
	{
		std::unique_ptr<PlayerSocket> player;
		{
			// Determine socket type, create PlayerSocket object and parse initial message
			char receivedBuf[2048];
			const int receivedLen = socket.ReceiveData(receivedBuf, sizeof(receivedBuf));
			if (!strncmp(receivedBuf, SOCKET_WIN7_HI_RESPONSE, receivedLen)) // WIN7
			{
				socket.m_type = WIN7;
				socket.SetNonBlocking();

				player = std::make_unique<Win7::PlayerSocket>(socket);

				socket.SendData(SOCKET_WIN7_HI_RESPONSE, static_cast<int>(strlen(SOCKET_WIN7_HI_RESPONSE)));
			}
			else if (receivedLen == sizeof(WinXP::MsgConnectionHi)) // WINXP/WINME
			{
				socket.m_type = WINXP;
				socket.SetNonBlocking();

				WinXP::MsgConnectionHi hiMessage;
				std::memcpy(&hiMessage, receivedBuf, receivedLen);
				WinXP::DecryptMessage(&hiMessage, sizeof(hiMessage), XPDefaultSecurityKey);

				if (WinXP::ValidateInternalMessage<WinXP::MessageConnectionHi>(hiMessage) &&
					hiMessage.protocolVersion == XPInternalProtocolVersion)
				{
					*socket.m_logStream << "[PROCESSED]: " << hiMessage << "\n\n" << std::endl;

					auto xpPlayer = std::make_unique<WinXP::PlayerSocket>(socket, *socket.m_logStream, hiMessage);

					WinXP::MsgConnectionHello helloMessage = xpPlayer->ConstructHelloMessage();
					WinXP::EncryptMessage(&helloMessage, sizeof(helloMessage), XPDefaultSecurityKey);
					*socket.m_logStream << "[QUEUED]: " << helloMessage << "\n(BYTES QUEUED=" << sizeof(helloMessage) << ")\n\n" << std::endl;
					socket.SendData(reinterpret_cast<const char*>(&helloMessage), sizeof(helloMessage));

					player = std::move(xpPlayer);
				}
				else
				{
					throw std::runtime_error("Invalid WinXP initial message!");
				}
			}
			else if (!strncmp("GET /windows/ad.asp", receivedBuf, strlen("GET /windows/ad.asp"))) // WINXP: Banner ad request
			{
				socket.m_type = WINXP_BANNER_AD_REQUEST;

				if (g_config.disableXPAdBanner)
					throw DisconnectSocket("Ignoring banner ad request: Disabled.");

				// Example of an old ad page: https://web.archive.org/web/20020205100250id_/http://zone.msn.com/windows/ad.asp
				// The banner.png image will be returned by this server later. Look for it on the same host using '/', so that browsers can display it too (useful for testing).
				// We link to the GitHub repository on the Wayback Machine, as it can be somewhat loaded in IE6 via HTTP (ads strictly open with IE).
				const std::string adHtml = R"(
					<HTML>
						<HEAD></HEAD>
						<BODY MARGINWIDTH="0" MARGINHEIGHT="0" TOPMARGIN="0" LEFTMARGIN="0" BGCOLOR="#FFFFFF">
							<A HREF="http://web.archive.org/web/2/https://github.com/provigz/ZoneInternetGamesServer" TARGET="_new">
								<IMG SRC="/banner.png" ALT="Powered by ZoneInternetGamesServer" BORDER=0 WIDTH=380 HEIGHT=200>
							</A>
							<ZONEAD></ZONEAD>
						</BODY>
					</HTML>
					)";
				const std::string adHttpHeader =
					"HTTP/1.1 200 OK\r\n"
					"Content-Type: text/html; charset=UTF-8\r\n"
					"Content-Length: " + std::to_string(adHtml.size()) + "\r\n"
					"Connection: close\r\n"
					"\r\n";
				socket.SendData(adHttpHeader.c_str(), static_cast<int>(adHttpHeader.size()));
				socket.SendData(adHtml.c_str(), static_cast<int>(adHtml.size()));

				throw DisconnectSocket("Banner ad sent over.");
			}
			else if (!strncmp("GET /banner.png", receivedBuf, strlen("GET /banner.png"))) // WINXP: Banner ad image request
			{
				socket.m_type = WINXP_BANNER_AD_IMAGE_REQUEST;

				if (g_config.disableXPAdBanner)
					throw DisconnectSocket("Ignoring banner ad image request: Disabled.");
				if (XP_AD_BANNER_DATA.empty())
					throw std::runtime_error("No banner ad image found!");

				const std::string adImageHttpHeader =
					"HTTP/1.1 200 OK\r\n"
					"Content-Type: image/png\r\n"
					"Content-Length: " + std::to_string(XP_AD_BANNER_DATA.size()) + "\r\n"
					"Connection: close\r\n"
					"\r\n";
				socket.SendData(adImageHttpHeader.c_str(), static_cast<int>(adImageHttpHeader.size()));

				const int sentLen = send(socket.GetRaw(), reinterpret_cast<const char*>(XP_AD_BANNER_DATA.data()),
					static_cast<int>(XP_AD_BANNER_DATA.size()), 0);
				if (sentLen == SOCKET_ERROR)
					throw std::runtime_error("\"send\" failed: " + std::to_string(WSAGetLastError()));
				*socket.m_logStream << "[SENT]: [RAW AD BANNER PNG]\n(BYTES SENT=" << sentLen << ")\n\n" << std::endl;

				throw DisconnectSocket("Banner ad image sent over.");
			}
			else
			{
				throw std::runtime_error("Invalid initial message or request!");
			}
		}

		assert(player);
		player->ProcessMessages();
	}
	catch (const DisconnectSocket& err) // Used to request disconnection without an actual error having occured
	{
		SessionLog() << "[SOCKET] Disconnecting socket " << socket.m_address
			<< ": " << err.what() << std::endl;
		return 0;
	}
	catch (const ClientDisconnected& err)
	{
		SessionLog() << "[SOCKET] Error communicating with socket " << socket.m_address
			<< ": Client has been disconnected: " << err.what() << std::endl;
		return 0;
	}
	catch (const MutexError& fatalErr)
	{
		std::cout << "[FATAL!] " << fatalErr.what();
		SessionLog() << "[FATAL!] " << fatalErr.what();
		throw;
	}
	catch (const std::exception& err)
	{
		SessionLog() << "[SOCKET] Error communicating with socket " << socket.m_address
			<< ": " << err.what() << std::endl;
		return 0;
	}
	return 0;
}


Socket::Address
Socket::GetAddress(SOCKET socket)
{
	sockaddr_in socketAddr;
	int socketAddrSize = sizeof(socketAddr);
	getpeername(socket, reinterpret_cast<sockaddr*>(&socketAddr), &socketAddrSize);

	return {
		inet_ntoa(socketAddr.sin_addr),
		ntohs(socketAddr.sin_port)
	};
}

Socket::Address
Socket::GetAddress(const sockaddr_in& socketAddr)
{
	return {
		inet_ntoa(socketAddr.sin_addr),
		ntohs(socketAddr.sin_port)
	};
}

char*
Socket::GetAddressString(IN_ADDR address)
{
	return inet_ntoa(address);
}


std::vector<Socket*>
Socket::GetSocketsByIP(const std::string& ip)
{
	std::vector<Socket*> sockets;
	for (Socket* socket : s_socketList)
	{
		if (socket->m_address.ip == ip)
			sockets.push_back(socket);
	}
	return sockets;
}

Socket*
Socket::GetSocketByIP(const std::string& ip, USHORT port)
{
	for (Socket* socket : s_socketList)
	{
		if (socket->m_address.ip == ip && socket->m_address.port == port)
			return socket;
	}
	return nullptr;
}


Socket::Socket(SOCKET socket) :
	m_socket(socket),
	m_connectionTime(std::time(nullptr)),
	m_address(GetAddress(socket)),
	m_logStream(),
	m_nonBlocking(false),
	m_socketEvent(),
	m_sendEvent(),
	m_sendBuffer(),
	m_sendBufferMutex(),
	m_lastSendBufferTime(0),
	m_disconnected(false),
	m_type(UNKNOWN),
	m_playerSocket(nullptr)
{
	switch (WaitForSingleObject(s_socketListMutex, SOCKET_LIST_MUTEX_TIMEOUT_MS))
	{
		case WAIT_OBJECT_0: // Acquired ownership of the mutex
			break;
		case WAIT_TIMEOUT:
			throw MutexError("Socket::Socket(): Timed out waiting for socket list mutex: " + std::to_string(GetLastError()));
		case WAIT_ABANDONED: // Acquired ownership of an abandoned mutex
			throw MutexError("Socket::Socket(): Got ownership of an abandoned socket list mutex: " + std::to_string(GetLastError()));
		default:
			throw MutexError("Socket::Socket(): An error occured waiting for socket list mutex: " + std::to_string(GetLastError()));
	}
	s_socketList.push_back(this);
	if (!ReleaseMutex(s_socketListMutex))
		throw MutexError("Socket::Socket(): Couldn't release socket list mutex: " + std::to_string(GetLastError()));
}

Socket::~Socket()
{
	DWORD mutexResult = WaitForSingleObject(s_socketListMutex, SOCKET_LIST_MUTEX_TIMEOUT_MS) == WAIT_OBJECT_0;
	assert(mutexResult && "Socket::~Socket(): Failed to acquire socket list mutex!");
	s_socketList.erase(std::remove(s_socketList.begin(), s_socketList.end(), this), s_socketList.end());
	mutexResult = ReleaseMutex(s_socketListMutex);
	assert(mutexResult && "Socket::~Socket(): Failed to release socket list mutex!");

	// Clean up
	CloseHandle(m_sendBufferMutex);
	CloseHandle(m_sendEvent);
	WSACloseEvent(m_socketEvent);

	Disconnect();
	closesocket(m_socket);
}


// Need a separate initailization function, so that the Socket can be pushed to the list
// as soon as possible in the constructor, without risking undefined behaviour if we wait
// for these operations to complete there.
void
Socket::Initialize()
{
	// Open a stream to log Socket events to
	if (g_config.logsDirectory.empty())
	{
		m_logStream = std::make_unique<NullStream>();
	}
	else
	{
		std::ostringstream logFileName;
		logFileName << g_config.logsDirectory << "\\SOCKET_" << m_address.AsString('_')
			<< "_" << std::time(nullptr) << ".txt";

		m_logStream = std::make_unique<std::ofstream>(logFileName.str());
		if (!static_cast<std::ofstream*>(m_logStream.get())->is_open())
		{
			SessionLog() << "[SOCKET] Failed to open log file \"" << logFileName.str() << "\"!" << std::endl;
			m_logStream = std::make_unique<NullStream>();
		}
	}
}

void
Socket::Disconnect()
{
	if (m_disconnected)
		return;

	m_disconnected = true; // Set early on to prevent another thread from disconnecting this socket again.

	SessionLog() << "[SOCKET] Disconnecting from " << m_address << '.' << std::endl;

	// PlayerSocket expects Socket to exist while it's being disconnected.
	// Ensure it's disconnected first.
	if (m_playerSocket)
		m_playerSocket->Disconnect();

	// Shut down the connection
	if (shutdown(m_socket, SD_BOTH) == SOCKET_ERROR)
		SessionLog() << "[SOCKET] \"shutdown\" failed: " << WSAGetLastError() << std::endl;
}


void
Socket::SetNonBlocking()
{
	if (m_nonBlocking)
		return;

	m_sendEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	m_sendBufferMutex = CreateMutex(NULL, FALSE, NULL);

	m_socketEvent = WSACreateEvent();
	WSAEventSelect(m_socket, m_socketEvent, FD_READ | FD_WRITE | FD_CLOSE);

	m_nonBlocking = true;
}

/* A recv() wrapper which on non-blocking sockets also handles other socket events. */
int
Socket::Receive(char* buf, int len)
{
	if (!m_nonBlocking)
	{
		const int receivedLen = recv(m_socket, buf, len, 0);
		if (receivedLen > 0)
			return receivedLen;
		if (receivedLen == 0)
			throw ClientDisconnected(0);

		throw std::runtime_error("\"recv\" failed: " + std::to_string(WSAGetLastError()));
	}

	const HANDLE events[2] = { m_socketEvent, m_sendEvent };
	while (true)
	{
		const DWORD waitResult = WSAWaitForMultipleEvents(2, events, FALSE, SOCKET_RECV_TIMEOUT, FALSE);

		// Check send timeout every loop
		if (!m_sendBuffer.empty())
		{
			if (m_lastSendBufferTime == 0)
				m_lastSendBufferTime = GetTickCount(); // Start timeout
			else if (GetTickCount() - m_lastSendBufferTime > SOCKET_SEND_TIMEOUT)
				throw ClientDisconnected("Timed out trying to send data!");
		}

		switch (waitResult)
		{
			case WAIT_OBJECT_0:
			{
				WSANETWORKEVENTS netEvents;
				WSAEnumNetworkEvents(m_socket, m_socketEvent, &netEvents);

				if (netEvents.lNetworkEvents & FD_CLOSE) // Closed connection
				{
					throw ClientDisconnected(std::to_string(netEvents.iErrorCode[FD_CLOSE_BIT]));
				}
				if (netEvents.lNetworkEvents & FD_READ) // Received data
				{
					const int receivedLen = recv(m_socket, buf, len, 0);
					if (receivedLen > 0)
						return receivedLen;
					if (receivedLen == 0)
						throw ClientDisconnected(0);

					const int err = WSAGetLastError();
					if (err == WSAEWOULDBLOCK)
						break; // No more data for now
					throw std::runtime_error("\"recv\" failed: " + std::to_string(err));
				}
				if (netEvents.lNetworkEvents & FD_WRITE) // Socket is writeable
				{
					FlushSendBuffer(); // Can send over remaining data in buffer
				}
				break;
			}

			case WAIT_OBJECT_0 + 1: // Send buffer has data
				FlushSendBuffer();
				break;

			case WAIT_TIMEOUT:
				throw ClientDisconnected("Timed out waiting for data!");
		}
	}
}

/* [NON-BLOCKING] Sends over any data left in m_sendBuffer for as long as the socket stays writeable. */
void
Socket::FlushSendBuffer()
{
	if (m_sendBuffer.empty())
		return;

	switch (WaitForSingleObject(m_sendBufferMutex, SOCKET_SEND_BUFFER_TIMEOUT_MS))
	{
		case WAIT_OBJECT_0: // Acquired ownership of the mutex
			break;
		case WAIT_TIMEOUT:
			throw MutexError("Socket::FlushSendBuffer(): Timed out waiting for send buffer mutex: " + std::to_string(GetLastError()));
		case WAIT_ABANDONED: // Acquired ownership of an abandoned mutex
			throw MutexError("Socket::FlushSendBuffer(): Got ownership of an abandoned send buffer mutex: " + std::to_string(GetLastError()));
		default:
			throw MutexError("Socket::FlushSendBuffer(): An error occured waiting for send buffer mutex: " + std::to_string(GetLastError()));
	}

	const int sentLen = send(m_socket, m_sendBuffer.c_str(), static_cast<int>(m_sendBuffer.length()), 0);
	if (sentLen == SOCKET_ERROR)
	{
		if (!ReleaseMutex(m_sendBufferMutex))
			throw MutexError("Socket::FlushSendBuffer(): Couldn't release send buffer mutex: " + std::to_string(GetLastError()));

		const int err = WSAGetLastError();
		if (err == WSAEWOULDBLOCK)
			return; // Try again on FD_WRITE or next buffered message
		if (err == WSAECONNRESET || err == WSAECONNABORTED)
			throw ClientDisconnected(std::to_string(err));

		throw std::runtime_error("\"send\" failed: " + std::to_string(err));
	}

	std::ostream& log = *m_logStream;
	log << "[SENT]: ";
	log.write(m_sendBuffer.c_str(), sentLen);
	log << "\n(BYTES SENT=" << sentLen << ")\n\n" << std::endl;

	m_sendBuffer.erase(0, sentLen);

	// If all data from buffer was sent, reset timeout
	if (m_sendBuffer.empty())
		m_lastSendBufferTime = 0;

	if (!ReleaseMutex(m_sendBufferMutex))
		throw MutexError("Socket::FlushSendBuffer(): Couldn't release send buffer mutex: " + std::to_string(GetLastError()));
}


int
Socket::ReceiveData(char* data, int len)
{
	if (len == 0)
		return 0;

	const int receivedLen = Receive(data, len);

	std::ostream& log = *m_logStream;
	log << "[RECEIVED]: ";
	log.write(data, receivedLen);
	log << "\n\n" << std::endl;

	return receivedLen;
}

void
Socket::SendData(const char* data, int len)
{
	if (m_nonBlocking)
	{
		switch (WaitForSingleObject(m_sendBufferMutex, SOCKET_SEND_BUFFER_TIMEOUT_MS))
		{
			case WAIT_OBJECT_0: // Acquired ownership of the mutex
				break;
			case WAIT_TIMEOUT:
				throw MutexError("Socket::SendData(): Timed out waiting for send buffer mutex: " + std::to_string(GetLastError()));
			case WAIT_ABANDONED: // Acquired ownership of an abandoned mutex
				throw MutexError("Socket::SendData(): Got ownership of an abandoned send buffer mutex: " + std::to_string(GetLastError()));
			default:
				throw MutexError("Socket::SendData(): An error occured waiting for send buffer mutex: " + std::to_string(GetLastError()));
		}
		m_sendBuffer.append(data, len);
		if (!ReleaseMutex(m_sendBufferMutex))
			throw MutexError("Socket::SendData(): Couldn't release send buffer mutex: " + std::to_string(GetLastError()));

		SetEvent(m_sendEvent);
		return;
	}

	const int sentLen = send(m_socket, data, len, 0);
	if (sentLen == SOCKET_ERROR)
	{
		const int err = WSAGetLastError();
		if (err == WSAECONNRESET || err == WSAECONNABORTED)
			throw ClientDisconnected(std::to_string(err));

		throw std::runtime_error("\"send\" failed: " + std::to_string(err));
	}

	std::ostream& log = *m_logStream;
	log << "[SENT]: ";
	log.write(data, len);
	log << "\n(BYTES SENT=" << len << ")\n\n" << std::endl;
}
