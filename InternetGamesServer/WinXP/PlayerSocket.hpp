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

class PlayerSocket final : public ::PlayerSocket
{
public:
	enum State {
		STATE_INITIALIZED,
		STATE_UNCONFIGURED,
		STATE_PROXY_DISCONNECTED,
		STATE_WAITINGFOROPPONENTS,
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
	PlayerSocket(Socket& socket, std::ostream& logStream, const MsgConnectionHi& hiMessage);
	~PlayerSocket() override;

	void ProcessMessages() override;

	/** Event handling */
	void OnGameStart(const std::vector<PlayerSocket*>& matchPlayers);

	inline void OnMatchReadEmptyGameMessage(uint32 type)
	{
		ReadIncomingEmptyGameMessage(type);
	}
	template<typename T, uint32 Type>
	inline T OnMatchReadGameMessage()
	{
		return ReadIncomingGameMessage<T, Type>();
	}
	template<typename T, uint32 Type, typename M, uint16 MessageLen> // Trailing data array after T
	inline std::pair<T, Array<M, MessageLen>> OnMatchReadGameMessage()
	{
		return ReadIncomingGameMessage<T, Type, M, MessageLen>();
	}
	template<uint32 Type, typename T>
	inline void OnMatchGenericMessage(const T& msgApp, int len = sizeof(T)) noexcept
	{
		SendGenericMessage<Type>(msgApp, len);
	}
	template<uint32 Type, typename T>
	void OnMatchGameMessage(const T& msgGame, int len = sizeof(T)) noexcept
	{
		SendGameMessage<Type>(msgGame, len);
	}
	template<uint32 Type, typename T, typename M, uint16 MessageLen> // Trailing data array after T
	void OnMatchGameMessage(T msgGame, const Array<M, MessageLen>& msgGameSecond) noexcept
	{
		SendGameMessage<Type>(msgGame, msgGameSecond);
	}
	void OnMatchDisconnect() noexcept;

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
	void AwaitGenericMessageHeader();

	/* Reading messages (generic helpers) */
	template<class T>
	void ReadIncomingMessage(T& data, int len = sizeof(T))
	{
		assert(len <= sizeof(T));
		if (len == 0)
			return;

		if (len > m_incomingGenericMsg.GetBufferLength())
			throw std::runtime_error("Requested message is longer than buffer!");

		std::memmove(&data, m_incomingGenericMsg.buffer.data(), len);
		m_incomingGenericMsg.buffer.erase(0, len);

		m_logStream << "[PROCESSED]: " << data << "\n\n" << std::endl;
	}
	template<class T>
	void ReadIncomingMessage(T& data, void(T::*converter)(), char* dataRaw = nullptr, int len = sizeof(T))
	{
		assert(len <= sizeof(T));
		if (len == 0)
			return;

		if (len > m_incomingGenericMsg.GetBufferLength())
			throw std::runtime_error("Requested message is longer than buffer!");

		std::memmove(&data, m_incomingGenericMsg.buffer.data(), len);
		m_incomingGenericMsg.buffer.erase(0, len);

		if (dataRaw)
			std::memcpy(dataRaw, &data, len);
		(data.*converter)();

		m_logStream << "[PROCESSED]: " << data << "\n\n" << std::endl;
	}

	/* Reading messages (generic) */
	template<typename T, uint32 Type>
	T ReadIncomingGenericMessage(bool forceProxySig = false)
	{
		static_assert(sizeof(T) % sizeof(uint32) == 0, "Size of T must be divisible by 4! Add STRUCT_PADDING at the end, if required.");

		assert(!m_incomingGenericMsg.buffer.empty() && !m_incomingGameMsg.valid);

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
		ReadIncomingMessage(msgApp);

		ReadIncomingGenericFooter();

		// Validate checksum
		const uint32 checksum = GenerateChecksum({
				{ &msgBaseApp, sizeof(msgBaseApp) },
				{ &msgApp, sizeof(msgApp) }
			});
		if (checksum != msgBaseGeneric.checksum)
			throw std::runtime_error("MsgBaseGeneric: Checksums don't match! Generated: " + std::to_string(checksum));

		return msgApp;
	}
	void ReadIncomingGameMessageHeader();
	void ReadIncomingEmptyGameMessage(uint32 type);
	template<typename T, uint32 Type>
	T ReadIncomingGameMessage()
	{
		static_assert(sizeof(T) % sizeof(uint32) == 0, "Size of T must be divisible by 4! Add STRUCT_PADDING at the end, if required.");

		assert(!m_incomingGenericMsg.buffer.empty() && m_incomingGameMsg.valid);

		const MsgBaseGeneric& msgBaseGeneric = m_incomingGenericMsg.base;
		const MsgBaseApplication& msgBaseApp = m_incomingGenericMsg.info;
		const MsgGameMessage& msgGameMessage = m_incomingGameMsg.info;

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
		ReadIncomingMessage(msgGame, &T::ConvertToHostEndian, msgGameRaw);

		ReadIncomingGenericFooter();
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
	ReadIncomingGameMessage()
	{
		static_assert(sizeof(T) % sizeof(uint32) == 0, "Size of T must be divisible by 4! Add STRUCT_PADDING at the end, if required.");

		assert(!m_incomingGenericMsg.buffer.empty() && m_incomingGameMsg.valid);

		const MsgBaseGeneric& msgBaseGeneric = m_incomingGenericMsg.base;
		const MsgBaseApplication& msgBaseApp = m_incomingGenericMsg.info;
		const MsgGameMessage& msgGameMessage = m_incomingGameMsg.info;

		if (msgGameMessage.gameID != m_match->GetGameID())
			throw std::runtime_error("MsgGameMessage: Incorrect game ID!");
		if (msgGameMessage.type != Type)
			throw std::runtime_error("MsgGameMessage: Incorrect message type! Expected: " + std::to_string(Type));
		if (msgGameMessage.length != msgBaseApp.dataLength - sizeof(msgGameMessage))
			throw std::runtime_error("MsgGameMessage: length is invalid!");

		T msgGame;
		char msgGameRaw[sizeof(T)];
		ReadIncomingMessage(msgGame, &T::ConvertToHostEndian, msgGameRaw);

		if (msgGameMessage.length != AdjustedSize<T> + msgGame.messageLength * sizeof(M))
			throw std::runtime_error("MsgGameMessage: Data is of incorrect size! Expected: " + std::to_string(AdjustedSize<T> + msgGame.messageLength * sizeof(M)));
		if (msgGame.messageLength > MessageLen)
			throw std::runtime_error("MsgGameMessage: Child message is too long! Expected less or equal than: " + std::to_string(MessageLen));

		const int rawMsgLength = static_cast<int>(msgGame.messageLength * sizeof(M));

		Array<M, MessageLen> msgGameSecond;
		msgGameSecond.SetLength(static_cast<int>(msgGame.messageLength));
		ReadIncomingMessage(msgGameSecond, rawMsgLength);

		ReadIncomingGenericFooter();
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
	ReadIncomingGameMessage()
	{
		static_assert(sizeof(T) % sizeof(uint32) == 0, "Size of T must be divisible by 4! Add STRUCT_PADDING at the end, if required.");

		assert(!m_incomingGenericMsg.buffer.empty() && m_incomingGameMsg.valid);

		const MsgBaseGeneric& msgBaseGeneric = m_incomingGenericMsg.base;
		const MsgBaseApplication& msgBaseApp = m_incomingGenericMsg.info;
		const MsgGameMessage& msgGameMessage = m_incomingGameMsg.info;

		if (msgGameMessage.gameID != m_match->GetGameID())
			throw std::runtime_error("MsgGameMessage: Incorrect game ID!");
		if (msgGameMessage.type != Type)
			throw std::runtime_error("MsgGameMessage: Incorrect message type! Expected: " + std::to_string(Type));
		if (msgGameMessage.length != msgBaseApp.dataLength - sizeof(msgGameMessage))
			throw std::runtime_error("MsgGameMessage: length is invalid!");

		T msgGame;
		char msgGameRaw[sizeof(T)];
		ReadIncomingMessage(msgGame, &T::ConvertToHostEndian, msgGameRaw);

		if (msgGameMessage.length != AdjustedSize<T> + msgGame.messageLength * sizeof(M))
			throw std::runtime_error("MsgGameMessage: Data is of incorrect size! Expected: " + std::to_string(AdjustedSize<T> + msgGame.messageLength * sizeof(M)));
		if (msgGame.messageLength > MessageLen)
			throw std::runtime_error("MsgGameMessage: Child message is too long! Expected less or equal than: " + std::to_string(MessageLen));

		const int rawMsgLengthRounded = ROUND_DATA_LENGTH_UINT32(static_cast<int>(msgGame.messageLength));

		// First, receive the full message, including additional data due to uint32 rounding.
		// Must be in one buffer as to not split DWORD blocks for checksum generation.
		Array<char, ROUND_DATA_LENGTH_UINT32(MessageLen * sizeof(M))> msgGameSecondFull;
		msgGameSecondFull.SetLength(rawMsgLengthRounded);
		ReadIncomingMessage(msgGameSecondFull, rawMsgLengthRounded);

		ReadIncomingGenericFooter();
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
	void ReadIncomingGenericFooter();

	/* Reading messages */
	void ReadProxyHiMessages();
	void ReadClientConfig();

	/* Sending messages (generic helpers) */
	template<class T>
	int QueueMessageForSend(char* buf, const T& data, int len = sizeof(T))
	{
		assert(len <= sizeof(T));

		std::memcpy(buf, &data, len);

		m_logStream << "[QUEUED]: " << data << "\n(BYTES QUEUED=" << len << ")\n\n" << std::endl;
		return len;
	}
	template<class T>
	int QueueMessageForSend(char* buf, const T& data, void(T::*converter)(), int len = sizeof(T))
	{
		assert(len <= sizeof(T));

		T dataConverted = data;
		(dataConverted.*converter)();
		std::memmove(buf, &dataConverted, len);

		m_logStream << "[QUEUED]: " << data << "\n(BYTES QUEUED=" << len << ")\n\n" << std::endl;
		return len;
	}

	/* Sending messages (generic) */
	template<uint32 Type, typename T>
	void SendGenericMessage(const T& msgApp, int len = AdjustedSize<T>, bool forceProxySig = false) noexcept
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

		char dataBuf[sizeof(MsgBaseGeneric) + sizeof(MsgBaseApplication) + sizeof(T) + sizeof(MsgFooterGeneric)];
		int dataBufLen = 0;
		dataBufLen += QueueMessageForSend(dataBuf, msgBaseGeneric);
		dataBufLen += QueueMessageForSend(dataBuf + dataBufLen, msgBaseApp);
		dataBufLen += QueueMessageForSend(dataBuf + dataBufLen, msgApp, len);
		dataBufLen += QueueMessageForSend(dataBuf + dataBufLen, msgFooterGeneric);
		assert(msgBaseGeneric.totalLength == dataBufLen);
		EncryptMessage(dataBuf, dataBufLen - sizeof(MsgFooterGeneric), m_securityKey); // MsgFooterGeneric is not encrypted
		m_socket.SendData(dataBuf, dataBufLen);
	}
	template<uint32 Type, typename T>
	void SendGameMessage(const T& msgGame, int len = AdjustedSize<T>) noexcept
	{
		static_assert(sizeof(T) % sizeof(uint32) == 0, "Size of T must be divisible by 4! Add STRUCT_PADDING at the end, if required.");
		assert(len % sizeof(uint32) == 0);

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

		char dataBuf[sizeof(MsgBaseGeneric) + sizeof(MsgBaseApplication) + sizeof(MsgGameMessage) + sizeof(T) + sizeof(MsgFooterGeneric)];
		int dataBufLen = 0;
		dataBufLen += QueueMessageForSend(dataBuf, msgBaseGeneric);
		dataBufLen += QueueMessageForSend(dataBuf + dataBufLen, msgBaseApp);
		dataBufLen += QueueMessageForSend(dataBuf + dataBufLen, msgGameMessage);
		dataBufLen += QueueMessageForSend(dataBuf + dataBufLen, msgGame, &T::ConvertToNetworkEndian, len);
		dataBufLen += QueueMessageForSend(dataBuf + dataBufLen, msgFooterGeneric);
		assert(msgBaseGeneric.totalLength == dataBufLen);
		EncryptMessage(dataBuf, dataBufLen - sizeof(MsgFooterGeneric), m_securityKey); // MsgFooterGeneric is not encrypted
		m_socket.SendData(dataBuf, dataBufLen);
	}
	template<uint32 Type, typename T, typename M, uint16 MessageLen> // Trailing data array after T
	typename std::enable_if_t<(sizeof(M) % sizeof(uint32) == 0), void>
	SendGameMessage(const T& msgGame, Array<M, MessageLen> msgGameSecond) noexcept
	{
		static_assert(sizeof(T) % sizeof(uint32) == 0, "Size of T must be divisible by 4! Add STRUCT_PADDING at the end, if required.");

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
		msgBaseGeneric.totalLength = sizeof(MsgBaseGeneric) + sizeof(MsgBaseApplication) + sizeof(MsgGameMessage) + sizeof(msgGame) + rawMsgLength + sizeof(MsgFooterGeneric);
		msgBaseGeneric.sequenceID = m_sequenceID++;
		msgBaseGeneric.checksum = GenerateChecksum({
				{ &msgBaseApp, sizeof(msgBaseApp) },
				{ &msgGameMessage, sizeof(msgGameMessage) },
				{ &msgGameNetworkEndian, sizeof(msgGameNetworkEndian) },
				{ msgGameSecond.raw, rawMsgLength }
			});

		MsgFooterGeneric msgFooterGeneric;
		msgFooterGeneric.status = MsgFooterGeneric::STATUS_OK;

		char dataBuf[sizeof(MsgBaseGeneric) + sizeof(MsgBaseApplication) + sizeof(MsgGameMessage) + sizeof(msgGame) + MessageLen * sizeof(M) + sizeof(MsgFooterGeneric)];
		int dataBufLen = 0;
		dataBufLen += QueueMessageForSend(dataBuf, msgBaseGeneric);
		dataBufLen += QueueMessageForSend(dataBuf + dataBufLen, msgBaseApp);
		dataBufLen += QueueMessageForSend(dataBuf + dataBufLen, msgGameMessage);
		dataBufLen += QueueMessageForSend(dataBuf + dataBufLen, msgGame, &T::ConvertToNetworkEndian);
		dataBufLen += QueueMessageForSend(dataBuf + dataBufLen, msgGameSecond, rawMsgLength);
		dataBufLen += QueueMessageForSend(dataBuf + dataBufLen, msgFooterGeneric);
		assert(msgBaseGeneric.totalLength == dataBufLen);
		EncryptMessage(dataBuf, dataBufLen - sizeof(MsgFooterGeneric), m_securityKey); // MsgFooterGeneric is not encrypted
		m_socket.SendData(dataBuf, dataBufLen);
	}
	template<uint32 Type, typename T, typename M, uint16 MessageLen> // Trailing data array after T
	typename std::enable_if_t<(sizeof(M) % sizeof(uint32) != 0), void>
	SendGameMessage(const T& msgGame, const Array<M, MessageLen>& msgGameSecond) noexcept
	{
		static_assert(sizeof(T) % sizeof(uint32) == 0, "Size of T must be divisible by 4! Add STRUCT_PADDING at the end, if required.");

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
		msgBaseGeneric.totalLength = sizeof(MsgBaseGeneric) + sizeof(MsgBaseApplication) + sizeof(MsgGameMessage) + sizeof(msgGame) + rawMsgLengthRounded + sizeof(MsgFooterGeneric);
		msgBaseGeneric.sequenceID = m_sequenceID++;
		msgBaseGeneric.checksum = GenerateChecksum({
				{ &msgBaseApp, sizeof(msgBaseApp) },
				{ &msgGameMessage, sizeof(msgGameMessage) },
				{ &msgGameNetworkEndian, sizeof(msgGameNetworkEndian) },
				{ msgGameSecondFull.raw, msgGameSecondFull.GetLength() }
			});

		MsgFooterGeneric msgFooterGeneric;
		msgFooterGeneric.status = MsgFooterGeneric::STATUS_OK;

		char dataBuf[sizeof(MsgBaseGeneric) + sizeof(MsgBaseApplication) + sizeof(MsgGameMessage) + sizeof(msgGame) + ROUND_DATA_LENGTH_UINT32(MessageLen * sizeof(M)) + sizeof(MsgFooterGeneric)];
		int dataBufLen = 0;
		dataBufLen += QueueMessageForSend(dataBuf, msgBaseGeneric);
		dataBufLen += QueueMessageForSend(dataBuf + dataBufLen, msgBaseApp);
		dataBufLen += QueueMessageForSend(dataBuf + dataBufLen, msgGameMessage);
		dataBufLen += QueueMessageForSend(dataBuf + dataBufLen, msgGame, &T::ConvertToNetworkEndian);
		dataBufLen += QueueMessageForSend(dataBuf + dataBufLen, msgGameSecondFull, rawMsgLengthRounded);
		dataBufLen += QueueMessageForSend(dataBuf + dataBufLen, msgFooterGeneric);
		assert(msgBaseGeneric.totalLength == dataBufLen);
		EncryptMessage(dataBuf, dataBufLen - sizeof(MsgFooterGeneric), m_securityKey); // MsgFooterGeneric is not encrypted
		m_socket.SendData(dataBuf, dataBufLen);
	}

	/* Sending messages */
	void SendProxyHelloMessages();
	void SendProxyServiceInfoMessages(MsgProxyServiceInfo::Reason reason);

protected:
	void OnDisconnected() override;

private:
	struct IncomingGenericMessage final
	{
		std::string buffer;
		MsgBaseGeneric base;
		MsgBaseApplication info;

		inline int GetBufferLength() const { return static_cast<int>(buffer.length()); }
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

	std::ostream& m_logStream;
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
