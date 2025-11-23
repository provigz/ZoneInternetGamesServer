#pragma once

#include <winsock2.h>

#include <cassert>
#include <string>
#include <sstream>
#include <vector>

// Keep alive packets are sent every 10 (WinXP/ME) and 18 (Win7) seconds
#define SOCKET_RECV_TIMEOUT 20000
#define SOCKET_SEND_TIMEOUT 1000

#define SOCKET_LIST_MUTEX_TIMEOUT_MS 5000

class PlayerSocket;

void LoadXPAdBannerImage();

/** SOCKET wrapper, featuring general socket handling functions and a few additional features */
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
		ClientDisconnected() throw() {}
	};

public:
	Socket(SOCKET socket);
	~Socket();

	void Disconnect();

	/** Receive data */
	int ReceiveData(char* data, int len);
	std::vector<std::vector<std::string>> ReceiveData();
	template<class T>
	int ReceiveData(T& data, int len = sizeof(T))
	{
		assert(len <= sizeof(T));
		if (len == 0)
			return 0;

		const int receivedLen = recv(m_socket, reinterpret_cast<char*>(&data), len, 0);
		if (receivedLen == 0)
			throw ClientDisconnected();
		else if (receivedLen < 0)
			throw std::runtime_error("\"recv\" failed: " + std::to_string(WSAGetLastError()));

		*m_logStream << "[RECEIVED]: " << data << "\n\n" << std::endl;

		return receivedLen;
	}
	template<class T, typename Key>
	int ReceiveData(T& data, void(*decryptor)(void*, int, Key), Key decryptKey, int len = sizeof(T))
	{
		assert(len <= sizeof(T));
		if (len == 0)
			return 0;

		const int receivedLen = recv(m_socket, reinterpret_cast<char*>(&data), len, 0);
		if (receivedLen == 0)
			throw ClientDisconnected();
		else if (receivedLen < 0)
			throw std::runtime_error("\"recv\" failed: " + std::to_string(WSAGetLastError()));

		decryptor(&data, len, decryptKey);

		*m_logStream << "[RECEIVED]: " << data << "\n\n" << std::endl;

		return receivedLen;
	}
	template<class T, typename Key>
	int ReceiveData(T& data, void(T::*converter)(), void(*decryptor)(void*, int, Key), Key decryptKey,
		char* dataRaw = nullptr, int len = sizeof(T))
	{
		assert(len <= sizeof(T));
		if (len == 0)
			return 0;

		const int receivedLen = recv(m_socket, reinterpret_cast<char*>(&data), len, 0);
		if (receivedLen == 0)
			throw ClientDisconnected();
		else if (receivedLen < 0)
			throw std::runtime_error("\"recv\" failed: " + std::to_string(WSAGetLastError()));

		decryptor(&data, len, decryptKey);

		if (dataRaw)
			std::memcpy(dataRaw, &data, len);
		(data.*converter)();

		*m_logStream << "[RECEIVED]: " << data << "\n\n" << std::endl;

		return receivedLen;
	}

	/** Send data */
	int SendData(const char* data, int len);
	void SendData(std::vector<std::string> data);
	template<class T>
	int SendData(const T& data, int len = sizeof(T))
	{
		assert(len <= sizeof(T));

		const int sentLen = send(m_socket, reinterpret_cast<const char*>(&data), len, 0);
		if (sentLen == SOCKET_ERROR)
			throw std::runtime_error("\"send\" failed: " + std::to_string(WSAGetLastError()));

		*m_logStream << "[SENT]: " << data << "\n(BYTES SENT=" << len << ")\n\n" << std::endl;

		return sentLen;
	}
	template<class T, typename Key>
	int SendData(T data, void(*encryptor)(void*, int, Key), Key encryptKey, int len = sizeof(T))
	{
		assert(len <= sizeof(T));

		std::ostringstream logBuf;
		logBuf << "[SENT]: " << data;

		encryptor(&data, len, encryptKey);

		const int sentLen = send(m_socket, reinterpret_cast<const char*>(&data), len, 0);
		if (sentLen == SOCKET_ERROR)
			throw std::runtime_error("\"send\" failed: " + std::to_string(WSAGetLastError()));

		*m_logStream << logBuf.str() << "\n(BYTES SENT=" << len << ")\n\n" << std::endl;

		return sentLen;
	}
	template<class T, typename Key>
	int SendData(T data, void(T::*converter)(), void(*encryptor)(void*, int, Key), Key encryptKey, int len = sizeof(T))
	{
		assert(len <= sizeof(T));

		std::ostringstream logBuf;
		logBuf << "[SENT]: " << data;

		(data.*converter)();
		encryptor(&data, len, encryptKey);

		const int sentLen = send(m_socket, reinterpret_cast<const char*>(&data), len, 0);
		if (sentLen == SOCKET_ERROR)
			throw std::runtime_error("\"send\" failed: " + std::to_string(WSAGetLastError()));

		*m_logStream << logBuf.str() << "\n(BYTES SENT=" << len << ")\n\n" << std::endl;

		return sentLen;
	}

	inline SOCKET GetRaw() const { return m_socket; }
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

private:
	const SOCKET m_socket;
	const std::time_t m_connectionTime;

	const Address m_address;
	std::unique_ptr<std::ostream> m_logStream;

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
