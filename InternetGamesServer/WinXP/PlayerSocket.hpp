#pragma once

#include "../PlayerSocket.hpp"

#include "Defines.hpp"
#include "Match.hpp"
#include "Protocol/Game.hpp"
#include "Protocol/Init.hpp"
#include "Protocol/Proxy.hpp"
#include "Security.hpp"
#include "../Util.hpp"

namespace WinXP {

#define XPPlayerSocketMatchGuard(funcName) \
	if (!m_match) \
		throw std::runtime_error(std::string("WinXP::PlayerSocket::") + funcName + "(): Attempted to access destroyed match!"); \
	if (m_match->GetState() == Match::STATE_ENDED) \
		throw std::runtime_error(std::string("WinXP::PlayerSocket::") + funcName + "(): Attempted to access ended match!"); \
	if (m_state != STATE_PLAYING) \
		throw std::runtime_error(std::string("WinXP::PlayerSocket::") + funcName + "(): Attempted to access match while not in PLAYING state!"); \

class PlayerSocket final : public ::PlayerSocket
{
public:
	enum State {
		STATE_INITIALIZED,
		STATE_UNCONFIGURED,
		STATE_PROXY_DISCONNECTED,
		STATE_WAITINGFOROPPONENTS,
		STATE_STARTING_GAME,
		STATE_PLAYING,
		STATE_DISCONNECTING
	};
	static std::string StateToString(State state);

	enum class ClientVersion
	{
		INVALID,
		WINME,
		WINXP
	};

public:
	PlayerSocket(Socket& socket, const MsgConnectionHi& hiMessage);
	~PlayerSocket() override;

	void ProcessMessages() override;

	/** Event handling */
	void OnGameStart(const std::vector<PlayerSocket*>& matchPlayers);

	inline void OnMatchAwaitEmptyGameMessage(uint32 type)
	{
		AwaitIncomingEmptyGameMessage(type);
	}
	template<typename T, uint32 Type>
	inline T OnMatchAwaitGameMessage()
	{
		return AwaitIncomingGameMessage<T, Type>();
	}
	template<typename T, uint32 Type, typename M, uint16 MessageLen> // Trailing data array after T
	inline std::pair<T, Array<M, MessageLen>> OnMatchAwaitGameMessage()
	{
		return AwaitIncomingGameMessage<T, Type, M, MessageLen>();
	}
	template<uint32 Type, typename T>
	inline void OnMatchGenericMessage(const T& msgApp, int len = sizeof(T))
	{
		SendGenericMessage<Type>(msgApp, len);
	}
	template<uint32 Type, typename T>
	void OnMatchGameMessage(const T& msgGame, int len = sizeof(T))
	{
		switch (WaitForSingleObject(m_acceptsGameMessagesEvent, MATCH_MUTEX_TIMEOUT_MS))
		{
			case WAIT_OBJECT_0:
				SendGameMessage<Type>(msgGame, len);
				break;
			case WAIT_TIMEOUT:
				throw std::runtime_error("WinXP::PlayerSocket::OnMatchGameMessage(): Timed out waiting for \"accepts game messages\" event!");
			default:
				throw std::runtime_error("WinXP::PlayerSocket::OnMatchGameMessage(): An error occured waiting for \"accepts game messages\" event: " + std::to_string(GetLastError()));
		}
	}
	template<uint32 Type, typename T, typename M, uint16 MessageLen> // Trailing data array after T
	void OnMatchGameMessage(T msgGame, const Array<M, MessageLen>& msgGameSecond)
	{
		switch (WaitForSingleObject(m_acceptsGameMessagesEvent, MATCH_MUTEX_TIMEOUT_MS))
		{
			case WAIT_OBJECT_0:
				SendGameMessage<Type>(msgGame, msgGameSecond);
				break;
			case WAIT_TIMEOUT:
				throw std::runtime_error("WinXP::PlayerSocket::OnMatchGameMessage(): Timed out waiting for \"accepts game messages\" event!");
			default:
				throw std::runtime_error("WinXP::PlayerSocket::OnMatchGameMessage(): An error occured waiting for \"accepts game messages\" event: " + std::to_string(GetLastError()));
		}
	}
	void OnMatchDisconnect();

	inline ClientVersion GetClientVersion() const { return m_clientVersion; }
	inline bool IsWinME() const { return m_clientVersion == ClientVersion::WINME; }

	inline uint32 GetID() const { return m_ID; }
	inline State GetState() const { return m_state; }
	inline Match::Game GetGame() const { return m_game; }
	inline Match::SkillLevel GetSkillLevel() const { return m_config.skillLevel; }
	inline uint32 GetSecurityKey() const { return m_securityKey; }
	inline Match* GetMatch() const { return m_match; }

	MsgConnectionHello ConstructHelloMessage() const;

private:
	/* Awaiting utilities */
	void AwaitGenericMessageHeader();
	template<typename T, uint32 Type>
	T AwaitIncomingGenericMessage(bool forceProxySig = false)
	{
		static_assert(sizeof(T) % sizeof(uint32) == 0, "Size of T must be divisible by 4! Add STRUCT_PADDING at the end, if required.");

		assert(m_incomingGenericMsg.valid && !m_incomingGameMsg.valid);

		const MsgBaseGeneric& msgBaseGeneric = m_incomingGenericMsg.base;
		const MsgBaseApplication& msgBaseApp = m_incomingGenericMsg.info;

		if (msgBaseGeneric.totalLength != sizeof(MsgBaseGeneric) + sizeof(MsgBaseApplication) + ROUND_DATA_LENGTH_UINT32(msgBaseApp.dataLength) + sizeof(MsgFooterGeneric))
			throw std::runtime_error("MsgBaseGeneric: totalLength is invalid!");

		if (msgBaseApp.signature != (m_proxyConnected && !forceProxySig ? XPLobbyProtocolSignature : XPProxyProtocolSignature))
			throw std::runtime_error("MsgBaseApplication: Invalid protocol signature!");
		if (msgBaseApp.messageType != Type)
			throw std::runtime_error("MsgBaseApplication: Incorrect message type! Expected: " + std::to_string(Type));
		if (msgBaseApp.dataLength != AdjustedSize<T>)
			throw std::runtime_error("MsgBaseApplication: Data is of incorrect size! Expected: " + std::to_string(AdjustedSize<T>));

		T msgApp;
		try
		{
			m_socket.ReceiveData(msgApp, DecryptMessage, m_securityKey);
		}
		catch (...)
		{
			ReleaseMutex(m_genericMessageMutex);
			throw;
		}

		AwaitIncomingGenericFooter();

		// Validate checksum
		const uint32 checksum = GenerateChecksum({
				{ &msgBaseApp, sizeof(msgBaseApp) },
				{ &msgApp, sizeof(msgApp) }
			});
		if (checksum != msgBaseGeneric.checksum)
			throw std::runtime_error("MsgBaseGeneric: Checksums don't match! Generated: " + std::to_string(checksum));

		return msgApp;
	}
	void AwaitIncomingGameMessageHeader();
	void AwaitIncomingEmptyGameMessage(uint32 type);
	template<typename T, uint32 Type>
	T AwaitIncomingGameMessage()
	{
		static_assert(sizeof(T) % sizeof(uint32) == 0, "Size of T must be divisible by 4! Add STRUCT_PADDING at the end, if required.");

		assert(m_incomingGenericMsg.valid && m_incomingGameMsg.valid);

		const MsgBaseGeneric& msgBaseGeneric = m_incomingGenericMsg.base;
		const MsgBaseApplication& msgBaseApp = m_incomingGenericMsg.info;
		const MsgGameMessage& msgGameMessage = m_incomingGameMsg.info;

		XPPlayerSocketMatchGuard("AwaitIncomingGameMessage")
		if (msgGameMessage.gameID != m_match->GetGameID())
			throw std::runtime_error("MsgGameMessage: Incorrect game ID!");
		if (msgGameMessage.type != Type)
			throw std::runtime_error("MsgGameMessage: Incorrect message type! Expected: " + std::to_string(Type));
		if (msgGameMessage.length != msgBaseApp.dataLength - sizeof(msgGameMessage))
			throw std::runtime_error("MsgGameMessage: length is invalid!");
		if (msgGameMessage.length != AdjustedSize<T>)
			throw std::runtime_error("MsgGameMessage: Data is of incorrect size! Expected: " + std::to_string(AdjustedSize<T>));

		T msgGame;
		char msgGameRaw[sizeof(T)];
		try
		{
			m_socket.ReceiveData(msgGame, &T::ConvertToHostEndian, DecryptMessage, m_securityKey, msgGameRaw);
		}
		catch (...)
		{
			ReleaseMutex(m_genericMessageMutex);
			throw;
		}

		AwaitIncomingGenericFooter();
		m_incomingGameMsg.valid = false;

		// Validate checksum
		const uint32 checksum = GenerateChecksum({
				{ &msgBaseApp, sizeof(msgBaseApp) },
				{ &msgGameMessage, sizeof(msgGameMessage) },
				{ msgGameRaw, sizeof(msgGameRaw) }
			});
		if (checksum != msgBaseGeneric.checksum)
			throw std::runtime_error("MsgBaseGeneric: Checksums don't match! Generated: " + std::to_string(checksum));

		return msgGame;
	}
	template<typename T, uint32 Type, typename M, uint16 MessageLen> // Trailing data array after T
	typename std::enable_if_t<(sizeof(M) % sizeof(uint32) == 0), std::pair<T, Array<M, MessageLen>>>
	AwaitIncomingGameMessage()
	{
		static_assert(sizeof(T) % sizeof(uint32) == 0, "Size of T must be divisible by 4! Add STRUCT_PADDING at the end, if required.");

		assert(m_incomingGenericMsg.valid && m_incomingGameMsg.valid);

		const MsgBaseGeneric& msgBaseGeneric = m_incomingGenericMsg.base;
		const MsgBaseApplication& msgBaseApp = m_incomingGenericMsg.info;
		const MsgGameMessage& msgGameMessage = m_incomingGameMsg.info;

		XPPlayerSocketMatchGuard("AwaitIncomingGameMessage")
		if (msgGameMessage.gameID != m_match->GetGameID())
			throw std::runtime_error("MsgGameMessage: Incorrect game ID!");
		if (msgGameMessage.type != Type)
			throw std::runtime_error("MsgGameMessage: Incorrect message type! Expected: " + std::to_string(Type));
		if (msgGameMessage.length != msgBaseApp.dataLength - sizeof(msgGameMessage))
			throw std::runtime_error("MsgGameMessage: length is invalid!");

		T msgGame;
		char msgGameRaw[sizeof(T)];
		try
		{
			m_socket.ReceiveData(msgGame, &T::ConvertToHostEndian, DecryptMessage, m_securityKey, msgGameRaw);
		}
		catch (...)
		{
			ReleaseMutex(m_genericMessageMutex);
			throw;
		}

		if (msgGameMessage.length != AdjustedSize<T> + msgGame.messageLength * sizeof(M))
			throw std::runtime_error("MsgGameMessage: Data is of incorrect size! Expected: " + std::to_string(AdjustedSize<T> + msgGame.messageLength * sizeof(M)));
		if (msgGame.messageLength > MessageLen)
			throw std::runtime_error("MsgGameMessage: Child message is too long! Expected less or equal than: " + std::to_string(MessageLen));

		const int rawMsgLength = static_cast<int>(msgGame.messageLength * sizeof(M));

		Array<M, MessageLen> msgGameSecond;
		msgGameSecond.SetLength(static_cast<int>(msgGame.messageLength));
		try
		{
			m_socket.ReceiveData(msgGameSecond, DecryptMessage, m_securityKey, rawMsgLength);
		}
		catch (...)
		{
			ReleaseMutex(m_genericMessageMutex);
			throw;
		}

		AwaitIncomingGenericFooter();
		m_incomingGameMsg.valid = false;

		// Validate checksum
		const uint32 checksum = GenerateChecksum({
				{ &msgBaseApp, sizeof(msgBaseApp) },
				{ &msgGameMessage, sizeof(msgGameMessage) },
				{ msgGameRaw, sizeof(msgGameRaw) },
				{ msgGameSecond.raw, rawMsgLength }
			});
		if (checksum != msgBaseGeneric.checksum)
			throw std::runtime_error("MsgBaseGeneric: Checksums don't match! Generated: " + std::to_string(checksum));

		return {
			std::move(msgGame),
			std::move(msgGameSecond)
		};
	}
	template<typename T, uint32 Type, typename M, uint16 MessageLen> // Trailing data array after T
	typename std::enable_if_t<(sizeof(M) % sizeof(uint32) != 0), std::pair<T, Array<M, MessageLen>>>
	AwaitIncomingGameMessage()
	{
		static_assert(sizeof(T) % sizeof(uint32) == 0, "Size of T must be divisible by 4! Add STRUCT_PADDING at the end, if required.");

		assert(m_incomingGenericMsg.valid && m_incomingGameMsg.valid);

		const MsgBaseGeneric& msgBaseGeneric = m_incomingGenericMsg.base;
		const MsgBaseApplication& msgBaseApp = m_incomingGenericMsg.info;
		const MsgGameMessage& msgGameMessage = m_incomingGameMsg.info;

		XPPlayerSocketMatchGuard("AwaitIncomingGameMessage")
		if (msgGameMessage.gameID != m_match->GetGameID())
			throw std::runtime_error("MsgGameMessage: Incorrect game ID!");
		if (msgGameMessage.type != Type)
			throw std::runtime_error("MsgGameMessage: Incorrect message type! Expected: " + std::to_string(Type));
		if (msgGameMessage.length != msgBaseApp.dataLength - sizeof(msgGameMessage))
			throw std::runtime_error("MsgGameMessage: length is invalid!");

		T msgGame;
		char msgGameRaw[sizeof(T)];
		try
		{
			m_socket.ReceiveData(msgGame, &T::ConvertToHostEndian, DecryptMessage, m_securityKey, msgGameRaw);
		}
		catch (...)
		{
			ReleaseMutex(m_genericMessageMutex);
			throw;
		}

		if (msgGameMessage.length != AdjustedSize<T> + msgGame.messageLength * sizeof(M))
			throw std::runtime_error("MsgGameMessage: Data is of incorrect size! Expected: " + std::to_string(AdjustedSize<T> + msgGame.messageLength * sizeof(M)));
		if (msgGame.messageLength > MessageLen)
			throw std::runtime_error("MsgGameMessage: Child message is too long! Expected less or equal than: " + std::to_string(MessageLen));

		const int rawMsgLengthRounded = ROUND_DATA_LENGTH_UINT32(static_cast<int>(msgGame.messageLength));

		// First, receive the full message, including additional data due to uint32 rounding.
		// Must be in one buffer as to not split DWORD blocks for checksum generation.
		Array<char, ROUND_DATA_LENGTH_UINT32(MessageLen * sizeof(M))> msgGameSecondFull;
		msgGameSecondFull.SetLength(rawMsgLengthRounded);
		try
		{
			m_socket.ReceiveData(msgGameSecondFull, DecryptMessage, m_securityKey, rawMsgLengthRounded);
		}
		catch (...)
		{
			ReleaseMutex(m_genericMessageMutex);
			throw;
		}

		AwaitIncomingGenericFooter();
		m_incomingGameMsg.valid = false;

		// Validate checksum
		const uint32 checksum = GenerateChecksum({
				{ &msgBaseApp, sizeof(msgBaseApp) },
				{ &msgGameMessage, sizeof(msgGameMessage) },
				{ msgGameRaw, sizeof(msgGameRaw) },
				{ msgGameSecondFull.raw, msgGameSecondFull.GetLength() }
			});
		if (checksum != msgBaseGeneric.checksum)
			throw std::runtime_error("MsgBaseGeneric: Checksums don't match! Generated: " + std::to_string(checksum));

		const int msgLength = static_cast<int>(msgGame.messageLength);

		// Move the actual message data to the array we're about to return
		Array<M, MessageLen> msgGameSecond;
		msgGameSecond.SetLength(msgLength);
		std::memmove(msgGameSecond.raw, msgGameSecondFull.raw, msgLength);

		return {
			std::move(msgGame),
			std::move(msgGameSecond)
		};
	}
	void AwaitIncomingGenericFooter();

	/* Awaiting messages */
	void AwaitProxyHiMessages();
	void AwaitClientConfig();

	/* Sending utilities */
	template<uint32 Type, typename T>
	void SendGenericMessage(T msgApp, int len = AdjustedSize<T>, bool forceProxySig = false)
	{
		static_assert(sizeof(T) % sizeof(uint32) == 0, "Size of T must be divisible by 4! Add STRUCT_PADDING at the end, if required.");
		assert(len % sizeof(uint32) == 0);

		MsgBaseApplication msgBaseApp;
		msgBaseApp.signature = (m_proxyConnected && !forceProxySig ? XPLobbyProtocolSignature : XPProxyProtocolSignature);
		msgBaseApp.messageType = Type;
		msgBaseApp.dataLength = len;

		MsgBaseGeneric msgBaseGeneric;
		msgBaseGeneric.totalLength = sizeof(MsgBaseGeneric) + sizeof(MsgBaseApplication) + len + sizeof(MsgFooterGeneric);
		msgBaseGeneric.sequenceID = m_sequenceID++;
		msgBaseGeneric.checksum = GenerateChecksum({
				{ &msgBaseApp, sizeof(msgBaseApp) },
				{ &msgApp, len }
			});

		MsgFooterGeneric msgFooterGeneric;
		msgFooterGeneric.status = MsgFooterGeneric::STATUS_OK;

		switch (WaitForSingleObject(m_genericMessageMutex, MATCH_MUTEX_TIMEOUT_MS))
		{
			case WAIT_OBJECT_0: // Acquired ownership of the mutex
				break;
			case WAIT_TIMEOUT:
				throw MutexError("WinXP::PlayerSocket::SendGenericMessage(): Timed out waiting for generic message mutex: " + std::to_string(GetLastError()));
			case WAIT_ABANDONED: // Acquired ownership of an abandoned mutex
				throw MutexError("WinXP::PlayerSocket::SendGenericMessage(): Got ownership of an abandoned generic message mutex: " + std::to_string(GetLastError()));
			default:
				throw MutexError("WinXP::PlayerSocket::SendGenericMessage(): An error occured waiting for generic message mutex: " + std::to_string(GetLastError()));
		}

		try
		{
			m_socket.SendData(std::move(msgBaseGeneric), EncryptMessage, m_securityKey);
			m_socket.SendData(std::move(msgBaseApp), EncryptMessage, m_securityKey);
			m_socket.SendData(std::move(msgApp), EncryptMessage, m_securityKey, len);
			m_socket.SendData(msgFooterGeneric);
		}
		catch (...)
		{
			ReleaseMutex(m_genericMessageMutex);
			throw;
		}

		if (!ReleaseMutex(m_genericMessageMutex))
			throw MutexError("WinXP::PlayerSocket::SendGenericMessage(): Couldn't release generic message mutex: " + std::to_string(GetLastError()));
	}
	template<uint32 Type, typename T>
	void SendGameMessage(T msgGame, int len = AdjustedSize<T>)
	{
		static_assert(sizeof(T) % sizeof(uint32) == 0, "Size of T must be divisible by 4! Add STRUCT_PADDING at the end, if required.");
		assert(len % sizeof(uint32) == 0);

		XPPlayerSocketMatchGuard("SendGameMessage")

		assert(m_proxyConnected);

		MsgBaseApplication msgBaseApp;
		msgBaseApp.signature = XPLobbyProtocolSignature;
		msgBaseApp.messageType = MessageGameMessage;
		msgBaseApp.dataLength = sizeof(MsgGameMessage) + len;

		MsgGameMessage msgGameMessage;
		msgGameMessage.gameID = m_match->GetGameID();
		msgGameMessage.type = Type;
		msgGameMessage.length = len;

		// Convert T to network endian so we can properly calculate the checksum.
		T msgGameNetworkEndian = msgGame;
		msgGameNetworkEndian.ConvertToNetworkEndian();

		MsgBaseGeneric msgBaseGeneric;
		msgBaseGeneric.totalLength = sizeof(MsgBaseGeneric) + sizeof(MsgBaseApplication) + sizeof(MsgGameMessage) + len + sizeof(MsgFooterGeneric);
		msgBaseGeneric.sequenceID = m_sequenceID++;
		msgBaseGeneric.checksum = GenerateChecksum({
				{ &msgBaseApp, sizeof(msgBaseApp) },
				{ &msgGameMessage, sizeof(msgGameMessage) },
				{ &msgGameNetworkEndian, len }
			});

		MsgFooterGeneric msgFooterGeneric;
		msgFooterGeneric.status = MsgFooterGeneric::STATUS_OK;

		switch (WaitForSingleObject(m_genericMessageMutex, MATCH_MUTEX_TIMEOUT_MS))
		{
			case WAIT_OBJECT_0: // Acquired ownership of the mutex
				break;
			case WAIT_TIMEOUT:
				throw MutexError("WinXP::PlayerSocket::SendGameMessage(): Timed out waiting for generic message mutex: " + std::to_string(GetLastError()));
			case WAIT_ABANDONED: // Acquired ownership of an abandoned mutex
				throw MutexError("WinXP::PlayerSocket::SendGameMessage(): Got ownership of an abandoned generic message mutex: " + std::to_string(GetLastError()));
			default:
				throw MutexError("WinXP::PlayerSocket::SendGameMessage(): An error occured waiting for generic message mutex: " + std::to_string(GetLastError()));
		}

		try
		{
			m_socket.SendData(std::move(msgBaseGeneric), EncryptMessage, m_securityKey);
			m_socket.SendData(std::move(msgBaseApp), EncryptMessage, m_securityKey);
			m_socket.SendData(std::move(msgGameMessage), EncryptMessage, m_securityKey);
			m_socket.SendData(std::move(msgGame), &T::ConvertToNetworkEndian, EncryptMessage, m_securityKey, len);
			m_socket.SendData(msgFooterGeneric);
		}
		catch (...)
		{
			ReleaseMutex(m_genericMessageMutex);
			throw;
		}

		if (!ReleaseMutex(m_genericMessageMutex))
			throw MutexError("WinXP::PlayerSocket::SendGameMessage(): Couldn't release generic message mutex: " + std::to_string(GetLastError()));
	}
	template<uint32 Type, typename T, typename M, uint16 MessageLen> // Trailing data array after T
	typename std::enable_if_t<(sizeof(M) % sizeof(uint32) == 0), void>
	SendGameMessage(T msgGame, Array<M, MessageLen> msgGameSecond)
	{
		static_assert(sizeof(T) % sizeof(uint32) == 0, "Size of T must be divisible by 4! Add STRUCT_PADDING at the end, if required.");

		XPPlayerSocketMatchGuard("SendGameMessage")

		assert(m_proxyConnected);

		MsgBaseApplication msgBaseApp;
		msgBaseApp.signature = XPLobbyProtocolSignature;
		msgBaseApp.messageType = MessageGameMessage;
		msgBaseApp.dataLength = static_cast<uint32>(sizeof(MsgGameMessage) + AdjustedSize<T> + msgGameSecond.GetLength() * sizeof(M));

		MsgGameMessage msgGameMessage;
		msgGameMessage.gameID = m_match->GetGameID();
		msgGameMessage.type = Type;
		assert(AdjustedSize<T> + msgGameSecond.GetLength() * sizeof(M) <= UINT16_MAX);
		msgGameMessage.length = static_cast<uint16>(AdjustedSize<T> + msgGameSecond.GetLength() * sizeof(M));

		assert(msgGame.messageLength == msgGameSecond.GetLength());

		const int rawMsgLength = static_cast<int>(msgGameSecond.GetLength() * sizeof(M));

		// Convert T to network endian so we can properly calculate the checksum.
		T msgGameNetworkEndian = msgGame;
		msgGameNetworkEndian.ConvertToNetworkEndian();

		MsgBaseGeneric msgBaseGeneric;
		msgBaseGeneric.totalLength = sizeof(MsgBaseGeneric) + sizeof(MsgBaseApplication) + sizeof(MsgGameMessage) + sizeof(msgGame) + static_cast<uint32>(msgGameSecond.GetLength()) * sizeof(M) + sizeof(MsgFooterGeneric);
		msgBaseGeneric.sequenceID = m_sequenceID++;
		msgBaseGeneric.checksum = GenerateChecksum({
				{ &msgBaseApp, sizeof(msgBaseApp) },
				{ &msgGameMessage, sizeof(msgGameMessage) },
				{ &msgGameNetworkEndian, sizeof(msgGameNetworkEndian) },
				{ msgGameSecond.raw, rawMsgLength }
			});

		MsgFooterGeneric msgFooterGeneric;
		msgFooterGeneric.status = MsgFooterGeneric::STATUS_OK;

		switch (WaitForSingleObject(m_genericMessageMutex, MATCH_MUTEX_TIMEOUT_MS))
		{
			case WAIT_OBJECT_0: // Acquired ownership of the mutex
				break;
			case WAIT_TIMEOUT:
				throw MutexError("WinXP::PlayerSocket::SendGameMessage(): Timed out waiting for generic message mutex: " + std::to_string(GetLastError()));
			case WAIT_ABANDONED: // Acquired ownership of an abandoned mutex
				throw MutexError("WinXP::PlayerSocket::SendGameMessage(): Got ownership of an abandoned generic message mutex: " + std::to_string(GetLastError()));
			default:
				throw MutexError("WinXP::PlayerSocket::SendGameMessage(): An error occured waiting for generic message mutex: " + std::to_string(GetLastError()));
		}

		try
		{
			m_socket.SendData(std::move(msgBaseGeneric), EncryptMessage, m_securityKey);
			m_socket.SendData(std::move(msgBaseApp), EncryptMessage, m_securityKey);
			m_socket.SendData(std::move(msgGameMessage), EncryptMessage, m_securityKey);
			m_socket.SendData(std::move(msgGame), &T::ConvertToNetworkEndian, EncryptMessage, m_securityKey);
			m_socket.SendData(std::move(msgGameSecond), EncryptMessage, m_securityKey, rawMsgLength);
			m_socket.SendData(msgFooterGeneric);
		}
		catch (...)
		{
			ReleaseMutex(m_genericMessageMutex);
			throw;
		}

		if (!ReleaseMutex(m_genericMessageMutex))
			throw MutexError("WinXP::PlayerSocket::SendGameMessage(): Couldn't release generic message mutex: " + std::to_string(GetLastError()));
	}
	template<uint32 Type, typename T, typename M, uint16 MessageLen> // Trailing data array after T
	typename std::enable_if_t<(sizeof(M) % sizeof(uint32) != 0), void>
	SendGameMessage(T msgGame, const Array<M, MessageLen>& msgGameSecond)
	{
		static_assert(sizeof(T) % sizeof(uint32) == 0, "Size of T must be divisible by 4! Add STRUCT_PADDING at the end, if required.");

		XPPlayerSocketMatchGuard("SendGameMessage")

		assert(m_proxyConnected);

		MsgBaseApplication msgBaseApp;
		msgBaseApp.signature = XPLobbyProtocolSignature;
		msgBaseApp.messageType = MessageGameMessage;
		msgBaseApp.dataLength = static_cast<uint32>(sizeof(MsgGameMessage) + AdjustedSize<T> + msgGameSecond.GetLength() * sizeof(M));

		MsgGameMessage msgGameMessage;
		msgGameMessage.gameID = m_match->GetGameID();
		msgGameMessage.type = Type;
		assert(AdjustedSize<T> + msgGameSecond.GetLength() * sizeof(M) <= UINT16_MAX);
		msgGameMessage.length = static_cast<uint16>(AdjustedSize<T> + msgGameSecond.GetLength() * sizeof(M));

		assert(msgGame.messageLength == msgGameSecond.GetLength());

		const int rawMsgLengthRounded = ROUND_DATA_LENGTH_UINT32(static_cast<int>(msgGameSecond.GetLength() * sizeof(M)));

		// Copy the message data to an array which has additional space for padding due to uint32 rounding.
		// Must be in one buffer as to not split DWORD blocks for checksum generation.
		Array<char, ROUND_DATA_LENGTH_UINT32(MessageLen * sizeof(M))> msgGameSecondFull;
		msgGameSecondFull.SetLength(rawMsgLengthRounded);
		std::memcpy(msgGameSecondFull.raw, msgGameSecond.raw, rawMsgLengthRounded);

		// Convert T to network endian so we can properly calculate the checksum.
		T msgGameNetworkEndian = msgGame;
		msgGameNetworkEndian.ConvertToNetworkEndian();

		MsgBaseGeneric msgBaseGeneric;
		msgBaseGeneric.totalLength = sizeof(MsgBaseGeneric) + sizeof(MsgBaseApplication) + sizeof(MsgGameMessage) + sizeof(msgGame) + static_cast<uint32>(msgGameSecondFull.GetLength()) + sizeof(MsgFooterGeneric);
		msgBaseGeneric.sequenceID = m_sequenceID++;
		msgBaseGeneric.checksum = GenerateChecksum({
				{ &msgBaseApp, sizeof(msgBaseApp) },
				{ &msgGameMessage, sizeof(msgGameMessage) },
				{ &msgGameNetworkEndian, sizeof(msgGameNetworkEndian) },
				{ msgGameSecondFull.raw, msgGameSecondFull.GetLength() }
			});

		MsgFooterGeneric msgFooterGeneric;
		msgFooterGeneric.status = MsgFooterGeneric::STATUS_OK;

		switch (WaitForSingleObject(m_genericMessageMutex, MATCH_MUTEX_TIMEOUT_MS))
		{
			case WAIT_OBJECT_0: // Acquired ownership of the mutex
				break;
			case WAIT_TIMEOUT:
				throw MutexError("WinXP::PlayerSocket::SendGameMessage(): Timed out waiting for generic message mutex: " + std::to_string(GetLastError()));
			case WAIT_ABANDONED: // Acquired ownership of an abandoned mutex
				throw MutexError("WinXP::PlayerSocket::SendGameMessage(): Got ownership of an abandoned generic message mutex: " + std::to_string(GetLastError()));
			default:
				throw MutexError("WinXP::PlayerSocket::SendGameMessage(): An error occured waiting for generic message mutex: " + std::to_string(GetLastError()));
		}

		try
		{
			m_socket.SendData(std::move(msgBaseGeneric), EncryptMessage, m_securityKey);
			m_socket.SendData(std::move(msgBaseApp), EncryptMessage, m_securityKey);
			m_socket.SendData(std::move(msgGameMessage), EncryptMessage, m_securityKey);
			m_socket.SendData(std::move(msgGame), &T::ConvertToNetworkEndian, EncryptMessage, m_securityKey);
			m_socket.SendData(std::move(msgGameSecondFull), EncryptMessage, m_securityKey, rawMsgLengthRounded);
			m_socket.SendData(msgFooterGeneric);
		}
		catch (...)
		{
			ReleaseMutex(m_genericMessageMutex);
			throw;
		}

		if (!ReleaseMutex(m_genericMessageMutex))
			throw MutexError("WinXP::PlayerSocket::SendGameMessage(): Couldn't release generic message mutex: " + std::to_string(GetLastError()));
	}

	/* Sending messages */
	void SendProxyHelloMessages();
	void SendProxyServiceInfoMessages(MsgProxyServiceInfo::Reason reason);

protected:
	void OnDisconnected() override;

private:
	struct IncomingGenericMessage final
	{
		bool valid = false;
		MsgBaseGeneric base;
		MsgBaseApplication info;

		inline uint32 GetType() const { return info.messageType; }
	};
	struct IncomingGameMessage final
	{
		bool valid = false;
		MsgGameMessage info;

		inline uint32 GetType() const { return info.type; }
	};

	struct Config final
	{
		LCID userLanguage = 0;
		LONG offsetUTC = 0;
		Match::SkillLevel skillLevel = Match::SkillLevel::INVALID;
		bool chatEnabled = false;
	};

private:
	ChangeTimeTracker<State> m_state;

	const uint32 m_ID;
	Match::Game m_game;
	const GUID m_machineGUID;
	const uint32 m_securityKey;
	ClientVersion m_clientVersion;
	uint32 m_sequenceID;
	bool m_proxyConnected;

	HANDLE m_genericMessageMutex; // Mutex to prevent simultaneous receiving/sending generic messages
	HANDLE m_acceptsGameMessagesEvent; // Signaled when the client is ready to accept game messages. Only set in STATE_PLAYING

	IncomingGenericMessage m_incomingGenericMsg;
	IncomingGameMessage m_incomingGameMsg;

	char m_serviceName[16];
	Config m_config;

	Match* m_match;

public:
	// Variables, set by the match
	const int16 m_seat;

private:
	PlayerSocket(const PlayerSocket&) = delete;
	PlayerSocket operator=(const PlayerSocket&) = delete;
};

}
