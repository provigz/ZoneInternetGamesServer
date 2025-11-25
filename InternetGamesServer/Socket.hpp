#pragma once

#include <winsock2.h>

#include <cassert>
#include <string>
#include <sstream>
#include <vector>

#include "Util.hpp"

// Keep alive packets are sent every 10 seconds on WinXP/ME
// and a little less than 30 seconds on Win7
#define SOCKET_RECV_TIMEOUT 30000
#define SOCKET_SEND_TIMEOUT 5000

// Timeout for any generic blocking sockets
#define SOCKET_BLOCKING_TIMEOUT 5000

#define SOCKET_SEND_BUFFER_TIMEOUT_MS 1000
#define SOCKET_LIST_MUTEX_TIMEOUT_MS 5000

class PlayerSocket;

void LoadXPAdBannerImage();

/** SOCKET wrapper with general socket handling functions and additional features */
class Socket final
{
	friend class PlayerSocket;

public:
	enum Type
	{
		UNKNOWN,
		WIN7,
		WINXP,
		WINXP_BANNER_AD_REQUEST,
		WINXP_BANNER_AD_IMAGE_REQUEST,
	};
	static std::string TypeToString(Type type);

private:
	static std::vector<Socket*> s_socketList;

public:
	static const HANDLE s_socketListMutex; // Mutex to prevent simultaneous modification/iteration of socket list

public:
	static inline const std::vector<Socket*>& GetList() { return s_socketList; }

	static std::vector<Socket*> GetSocketsByIP(const std::string& ip);
	static Socket* GetSocketByIP(const std::string& ip, USHORT port);

	// Handler for the thread of a socket
	static DWORD WINAPI SocketHandler(void* socket);

	// Used to request disconnection without an actual error having occured
	class DisconnectSocket final : public std::exception
	{
	public:
		DisconnectSocket(const std::string& err) :
			m_err(err)
		{}

		const char* what() const override { return m_err.c_str(); }

	private:
		const std::string m_err;
	};

	struct Address final
	{
		const std::string ip;
		const USHORT port;

		inline std::string AsString(const char portSeparator = ':') const
		{
			return ip + portSeparator + std::to_string(port);
		}
	};
	static Address GetAddress(SOCKET socket);
	static Address GetAddress(const sockaddr_in& socketAddr);
	static char* GetAddressString(IN_ADDR address);

private:
	// Client disconnected exception
	class ClientDisconnected final : public std::exception
	{
	public:
		ClientDisconnected(const std::string& err) throw() :
			m_err(err)
		{}

		const char* what() const { return m_err.c_str(); }

	private:
		const std::string m_err;
	};

public:
	Socket(SOCKET socket);
	~Socket();

	void Disconnect();

	int ReceiveData(char* data, int len);
	void SendData(const char* data, int len);
	inline void SendData(const std::string& data)
	{
		SendData(data.c_str(), static_cast<int>(data.length()));
	}

	inline SOCKET GetRaw() const { return m_socket; }
	inline bool IsNonBlocking() const { return m_nonBlocking; }
	inline bool IsDisconnected() const { return m_disconnected; }
	inline std::time_t GetConnectionTime() const { return m_connectionTime; }
	inline Type GetType() const { return m_type; }
	inline PlayerSocket* GetPlayerSocket() const { return m_playerSocket; }

	inline std::string GetIP() const { return m_address.ip; }
	inline USHORT GetPort() const { return m_address.port; }
	inline Address GetAddress() const { return m_address; }
	inline std::string GetAddressString(const char portSeparator = ':') const
	{
		return m_address.AsString(portSeparator);
	}

private:
	void Initialize();
	void SetNonBlocking();

	/* A recv() wrapper which on non-blocking sockets also handles other socket events. */
	int Receive(char* buf, int len);

	/* [NON-BLOCKING] Sends over any data left in m_sendBuffer for as long as the socket stays writeable. */
	void FlushSendBuffer();

private:
	const SOCKET m_socket;
	const std::time_t m_connectionTime;

	const Address m_address;
	std::unique_ptr<std::ostream> m_logStream;

	/* Non-blocking sockets */
	bool m_nonBlocking;
	WSAEVENT m_socketEvent;
	HANDLE m_sendEvent;
	std::string m_sendBuffer;
	HANDLE m_sendBufferMutex;
	DWORD m_lastSendBufferTime;

	bool m_disconnected;

	Type m_type;
	PlayerSocket* m_playerSocket;

private:
	Socket(const Socket&) = delete;
	Socket operator=(const Socket&) = delete;
};

static inline std::ostream& operator<<(std::ostream& out, const Socket::Address& addr)
{
	return out << addr.ip << ':' << addr.port;
}
