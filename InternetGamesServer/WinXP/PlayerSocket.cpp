#include "PlayerSocket.hpp"

#include <array>
#include <cassert>
#include <stdexcept>
#include <sstream>
#include <iostream>

#include "../MatchManager.hpp"
#include "../Util.hpp"

namespace WinXP {

std::string
PlayerSocket::StateToString(State state)
{
	switch (state)
	{
		case STATE_INITIALIZED:
			return "STATE_INITIALIZED";
		case STATE_UNCONFIGURED:
			return "STATE_UNCONFIGURED";
		case STATE_PROXY_DISCONNECTED:
			return "STATE_PROXY_DISCONNECTED";
		case STATE_WAITINGFOROPPONENTS:
			return "STATE_WAITINGFOROPPONENTS";
		case STATE_PLAYING:
			return "STATE_PLAYING";
		case STATE_DISCONNECTING:
			return "STATE_DISCONNECTING";
		default:
			return "<unknown>";
	}
}


PlayerSocket::PlayerSocket(Socket& socket, std::ostream& logStream, const MsgConnectionHi& hiMessage) :
	::PlayerSocket(socket),
	m_state(STATE_INITIALIZED),
	m_ID(std::uniform_int_distribution<uint32>{}(g_rng)),
	m_game(Match::Game::INVALID),
	m_machineGUID(hiMessage.machineGUID),
	m_securityKey(std::uniform_int_distribution<uint32>{}(g_rng)),
	m_clientVersion(ClientVersion::INVALID),
	m_sequenceID(0),
	m_proxyConnected(false),
	m_logStream(logStream),
	m_incomingGenericMsg(),
	m_incomingGameMsg(),
	m_serviceName(),
	m_match(),
	m_seat(-1)
{
}

PlayerSocket::~PlayerSocket()
{
	Destroy();
}


#define XPPlayerSocketMatchGuard(funcName) \
	if (!m_match) \
		throw std::runtime_error(std::string("WinXP::PlayerSocket::") + funcName + "(): Attempted to access destroyed match!"); \
	if (m_match->GetState() == Match::STATE_ENDED) \
		throw std::runtime_error(std::string("WinXP::PlayerSocket::") + funcName + "(): Attempted to access ended match!"); \
	if (m_state != STATE_PLAYING) \
		throw std::runtime_error(std::string("WinXP::PlayerSocket::") + funcName + "(): Attempted to access match while not in PLAYING state!");

void
PlayerSocket::ProcessMessages()
{
	while (true)
	{
		AwaitGenericMessageHeader();
		if (m_incomingGenericMsg.GetType() == MessageConnectionKeepAlive)
		{
			ReadIncomingGenericFooter();
		}
		else
		{
			switch (m_state)
			{
				case STATE_INITIALIZED:
				{
					ReadProxyHiMessages();
					SendProxyHelloMessages();

					assert(m_clientVersion != ClientVersion::INVALID);
					break;
				}
				case STATE_UNCONFIGURED:
				{
					ReadClientConfig();

					MsgUserInfoResponse msgUserInfo;
					msgUserInfo.ID = m_ID;
					msgUserInfo.language = m_config.userLanguage;
					SendGenericMessage<MessageUserInfoResponse>(std::move(msgUserInfo));

					m_match = MatchManager::Get().FindLobby(*this);
					m_state = STATE_WAITINGFOROPPONENTS;
					break;
				}
				case STATE_PROXY_DISCONNECTED: // Disconnected proxy (after: disconnected from match, find new opponent, received corrupted data)
				{
					assert(!m_proxyConnected);

					/* Process connect request and send response */
					const MsgProxyServiceRequest msgServiceRequestConnect = ReadIncomingGenericMessage<MsgProxyServiceRequest, 1>();
					if (!ValidateProxyMessage<MessageProxyServiceRequest>(msgServiceRequestConnect))
						throw std::runtime_error("MsgProxyServiceRequest: Message is invalid!");

					if (msgServiceRequestConnect.reason != MsgProxyServiceRequest::REASON_CONNECT)
					{
						if (msgServiceRequestConnect.reason == MsgProxyServiceRequest::REASON_DISCONNECT)
							break;
						throw std::runtime_error("MsgProxyServiceRequest: Reason is not client connection or disconnection!");
					}

					SendProxyServiceInfoMessages(MsgProxyServiceInfo::SERVICE_CONNECT);
					break;
				}
				case STATE_PLAYING:
				{
					switch (m_incomingGenericMsg.GetType())
					{
						case MessageGameMessage:
						{
							ReadIncomingGameMessageHeader();

							XPPlayerSocketMatchGuard("ProcessMessages")
							m_match->ProcessIncomingGameMessage(*this, m_incomingGameMsg.GetType());
							break;
						}
						case MessageChatSwitch:
						{
							const MsgChatSwitch msgChatSwitch = ReadIncomingGenericMessage<MsgChatSwitch, MessageChatSwitch>();
							if (msgChatSwitch.userID != m_ID)
								throw std::runtime_error("MsgChatSwitch: Incorrect user ID!");

							if (m_config.chatEnabled == msgChatSwitch.chatEnabled)
								break;
							m_config.chatEnabled = msgChatSwitch.chatEnabled;

							XPPlayerSocketMatchGuard("ProcessMessages")
							m_match->ProcessMessage(msgChatSwitch);
							break;
						}
						case 1: // Disconnect proxy (find new opponent, received corrupted data)
						{
							assert(m_proxyConnected);

							/* Process disconnect request and send response */
							const MsgProxyServiceRequest msgServiceRequestDisconnect = ReadIncomingGenericMessage<MsgProxyServiceRequest, 1>(true);
							if (!ValidateProxyMessage<MessageProxyServiceRequest>(msgServiceRequestDisconnect))
								throw std::runtime_error("MsgProxyServiceRequest: Message is invalid!");

							if (msgServiceRequestDisconnect.reason != MsgProxyServiceRequest::REASON_DISCONNECT)
								throw std::runtime_error("MsgProxyServiceRequest: Reason is not client disconnection!");

							SendProxyServiceInfoMessages(MsgProxyServiceInfo::SERVICE_DISCONNECT);
							break;
						}
						default:
							throw std::runtime_error("WinXP::PlayerSocket::ProcessMessages(): Generic message of unknown type received: " + std::to_string(m_incomingGenericMsg.GetType()));
					}
					break;
				}
				default:
					throw std::runtime_error("WinXP::PlayerSocket::ProcessMessages(): Message was received, but current state (" + StateToString(m_state) + ") does not process any!");
			}
		}

		// Time out the client in states not involving participation in a match
		if (m_state != STATE_WAITINGFOROPPONENTS &&
			m_state != STATE_PLAYING &&
			m_state.GetSecondsSinceLastChange() >= 60)
		{
			throw std::runtime_error("WinXP::PlayerSocket::ProcessMessages(): Timeout: Client has not switched from state \""
				+ StateToString(m_state) + "\" for 60 seconds or more!");
		}
	}
}

#undef XPPlayerSocketMatchGuard


void
PlayerSocket::OnGameStart(const std::vector<PlayerSocket*>& matchPlayers)
{
	if (m_state != STATE_WAITINGFOROPPONENTS)
		return;

	const int16 totalPlayerCount = static_cast<int16>(m_match->GetRequiredPlayerCount());
	MsgGameStart msgGameStart;
	msgGameStart.gameID = m_match->GetGameID();
	msgGameStart.seat = m_seat;
	msgGameStart.totalSeats = totalPlayerCount;

	assert(matchPlayers.size() == totalPlayerCount);
	for (int16 i = 0; i < totalPlayerCount; ++i)
	{
		MsgGameStart::User& user = msgGameStart.users[matchPlayers[i]->m_seat];
		const Config& config = matchPlayers[i]->m_config;

		user.ID = matchPlayers[i]->GetID();
		user.language = config.userLanguage;
		user.chatEnabled = config.chatEnabled;
		user.skill = static_cast<int16>(config.skillLevel);

		assert(config.skillLevel != Match::SkillLevel::INVALID);
	}

	SendGenericMessage<MessageGameStart>(std::move(msgGameStart),
		sizeof(MsgGameStart) - sizeof(MsgGameStart::User) * (MATCH_MAX_PLAYERS - totalPlayerCount));

	// The match will now be able to send game messages to this client
	m_state = STATE_PLAYING;
}

void
PlayerSocket::OnDisconnected()
{
	if (m_match)
	{
		m_match->DisconnectedPlayer(*this);
		m_match = nullptr;
	}
	m_proxyConnected = false;
	m_state = STATE_DISCONNECTING;
}

void
PlayerSocket::OnMatchDisconnect() noexcept
{
	if (!m_match)
		return;

	m_match = nullptr;
	m_proxyConnected = false;
	m_state = STATE_PROXY_DISCONNECTED;

	SendProxyServiceInfoMessages(MsgProxyServiceInfo::SERVICE_DISCONNECT);
}


void
PlayerSocket::AwaitGenericMessageHeader()
{
	assert(m_incomingGenericMsg.buffer.empty() && !m_incomingGameMsg.valid);

	int receivedLen = 0;
	while (receivedLen < sizeof(MsgBaseGeneric))
		receivedLen += m_socket.ReceiveData(reinterpret_cast<char*>(&m_incomingGenericMsg.base) + receivedLen,
			sizeof(m_incomingGenericMsg.base) - receivedLen);
	DecryptMessage(&m_incomingGenericMsg.base, sizeof(m_incomingGenericMsg.base), m_securityKey);
	m_logStream << "[PROCESSED]: " << m_incomingGenericMsg.base << "\n\n" << std::endl;

	if (!ValidateInternalMessageNoTotalLength<MessageConnectionGeneric>(m_incomingGenericMsg.base))
		throw std::runtime_error("MsgBaseGeneric: Message is invalid!");

	const int dataLen = m_incomingGenericMsg.base.totalLength - sizeof(MsgBaseGeneric);
	if (dataLen < sizeof(MsgBaseApplication) + sizeof(MsgFooterGeneric))
		throw std::runtime_error("MsgBaseGeneric: totalLength is invalid!");
	char dataBuf[2048];
	if (dataLen > sizeof(dataBuf))
		throw std::runtime_error("MsgBaseApplication: Incoming data is longer than buffer!");
	receivedLen = 0;
	while (receivedLen < dataLen)
		receivedLen += m_socket.ReceiveData(dataBuf + receivedLen, dataLen - receivedLen);
	DecryptMessage(dataBuf, dataLen - sizeof(MsgFooterGeneric), m_securityKey); // MsgFooterGeneric is not encrypted
	m_incomingGenericMsg.buffer.append(dataBuf, dataLen);

	ReadIncomingMessage(m_incomingGenericMsg.info);
}


void
PlayerSocket::ReadIncomingGameMessageHeader()
{
	assert(!m_incomingGenericMsg.buffer.empty() && !m_incomingGameMsg.valid);

	assert(m_proxyConnected);

	const MsgBaseGeneric& msgBaseGeneric = m_incomingGenericMsg.base;
	const MsgBaseApplication& msgBaseApp = m_incomingGenericMsg.info;

	if (msgBaseGeneric.totalLength != sizeof(MsgBaseGeneric) + sizeof(MsgBaseApplication) + ROUND_DATA_LENGTH_UINT32(msgBaseApp.dataLength) + sizeof(MsgFooterGeneric))
		throw std::runtime_error("MsgBaseGeneric: totalLength is invalid!");

	if (msgBaseApp.signature != XPLobbyProtocolSignature)
		throw std::runtime_error("MsgBaseApplication: Invalid protocol signature!");
	if (msgBaseApp.messageType != MessageGameMessage)
		throw std::runtime_error("MsgBaseApplication: Incorrect message type! Expected: Game message");
	if (msgBaseApp.dataLength < sizeof(MsgGameMessage))
		throw std::runtime_error("MsgBaseApplication: Data is of incorrect size! Expected: equal or more than " + std::to_string(sizeof(MsgGameMessage)));

	ReadIncomingMessage(m_incomingGameMsg.info);

	m_incomingGameMsg.valid = true;
}

void
PlayerSocket::ReadIncomingEmptyGameMessage(uint32 type)
{
	assert(!m_incomingGenericMsg.buffer.empty() && m_incomingGameMsg.valid);

	const MsgBaseGeneric& msgBaseGeneric = m_incomingGenericMsg.base;
	const MsgBaseApplication& msgBaseApp = m_incomingGenericMsg.info;
	const MsgGameMessage& msgGameMessage = m_incomingGameMsg.info;

	if (msgGameMessage.gameID != m_match->GetGameID())
		throw std::runtime_error("MsgGameMessage: Incorrect game ID!");
	if (msgGameMessage.type != type)
		throw std::runtime_error("MsgGameMessage: Incorrect message type! Expected: " + std::to_string(type));
	if (msgGameMessage.length != 0)
		throw std::runtime_error("MsgGameMessage: length is invalid: Expected empty message!");

	ReadIncomingGenericFooter();
	m_incomingGameMsg.valid = false;

	// Validate checksum
	const uint32 checksum = GenerateChecksum({
			{ &msgBaseApp, sizeof(msgBaseApp) },
			{ &msgGameMessage, sizeof(msgGameMessage) }
		});
	if (checksum != msgBaseGeneric.checksum)
		throw std::runtime_error("MsgBaseGeneric: Checksums don't match! Generated: " + std::to_string(checksum));
}

void
PlayerSocket::ReadIncomingGenericFooter()
{
	MsgFooterGeneric msgFooterGeneric;
	ReadIncomingMessage(msgFooterGeneric);
	if (msgFooterGeneric.status == MsgFooterGeneric::STATUS_CANCELLED)
		throw std::runtime_error("MsgFooterGeneric: Status is CANCELLED!");
	else if (msgFooterGeneric.status != MsgFooterGeneric::STATUS_OK)
		throw std::runtime_error("MsgFooterGeneric: Status is not OK! - " + std::to_string(msgFooterGeneric.status));

	if (!m_incomingGenericMsg.buffer.empty())
		throw std::runtime_error("Garbage data after footer!");

	m_incomingGenericMsg.buffer.clear();
}


void
PlayerSocket::ReadProxyHiMessages()
{
	Util::MsgProxyHiCollection msg = ReadIncomingGenericMessage<Util::MsgProxyHiCollection, 3>();
	if (!ValidateProxyMessage<MessageProxyHi>(msg.hi))
		throw std::runtime_error("MsgProxyHi: Message is invalid!");
	if (!ValidateProxyMessage<MessageProxyID>(msg.ID))
		throw std::runtime_error("MsgProxyID: Message is invalid!");
	if (!ValidateProxyMessage<MessageProxyServiceRequest>(msg.serviceRequest))
		throw std::runtime_error("MsgProxyServiceRequest: Message is invalid!");

	if (msg.hi.protocolVersion != XPProxyProtocolVersion)
		throw std::runtime_error("MsgProxyHi: Incorrect protocol version!");

	switch (msg.hi.clientVersion)
	{
		case XPProxyClientVersion:
			m_clientVersion = ClientVersion::WINXP;
			break;
		case MEProxyClientVersion:
			m_clientVersion = ClientVersion::WINME;
			break;
		default:
			throw std::runtime_error("MsgProxyHi: Unsupported client version!");
	}

	if (msg.serviceRequest.reason != MsgProxyServiceRequest::REASON_CONNECT)
		throw std::runtime_error("MsgProxyServiceRequest: Reason is not client connection!");

	// Determine game
	const std::string gameToken(msg.hi.gameToken, 6);
	m_game = Match::GameFromString(gameToken);
	if (m_game == Match::Game::INVALID)
		throw std::runtime_error("Invalid game token: " + gameToken + "!");

	memmove(m_serviceName, msg.serviceRequest.serviceName, sizeof(msg.serviceRequest.serviceName));
}

void
PlayerSocket::ReadClientConfig()
{
	MsgClientConfig msg = ReadIncomingGenericMessage<MsgClientConfig, MessageClientConfig>();
	if (msg.protocolSignature != XPRoomProtocolSignature)
		throw std::runtime_error("MsgClientConfig: Invalid protocol signature!");
	if (msg.protocolVersion != XPRoomClientVersion)
		throw std::runtime_error("MsgClientConfig: Unsupported client version!");

	// Parse config string
	const char* ch = msg.config;
	while (*ch)
	{
		const char* keyStart = ch;
		const char* equalChar = strchr(ch, '=');
		if (!equalChar)
			throw std::runtime_error("MsgClientConfig: Config parse error: Expected '='!");
		const std::string key(keyStart, equalChar);

		if (*(equalChar + 1) != '<')
			throw std::runtime_error("MsgClientConfig: Config parse error: Expected '<' after '='!");
		const char* valStart = equalChar + 2;
		const char* valEnd = strchr(valStart, '>');
		if (!valEnd)
			throw std::runtime_error("MsgClientConfig: Config parse error: Expected '>'!");
		const std::string value(valStart, valEnd);

		if (key == "SLCID" || key == "ILCID") // System and application LCIDs
		{
			// Ignore
		}
		else if (key == "ULCID") // User LCID
		{
			m_config.userLanguage = std::stoi(value);
		}
		else if (key == "UTCOFFSET") // UTC offset
		{
			m_config.offsetUTC = std::stoi(value);
		}
		else if (key == "Skill") // Skill level
		{
			if (value == "Beginner")
				m_config.skillLevel = Match::SkillLevel::BEGINNER;
			else if (value == "Intermediate")
				m_config.skillLevel = Match::SkillLevel::INTERMEDIATE;
			else if (value == "Expert")
				m_config.skillLevel = Match::SkillLevel::EXPERT;
			else
				throw std::runtime_error("MsgClientConfig: Config: Unknown value for 'Skill': '" + value + "'!");
		}
		else if (key == "Chat") // Chat enabled
		{
			if (value == "Off")
				m_config.chatEnabled = false;
			else if (value == "On")
				m_config.chatEnabled = true;
			else
				throw std::runtime_error("MsgClientConfig: Config: Unknown value for 'Chat': '" + value + "'!");
		}
		else if (key == "Exit")
		{
			// Ignore
		}
		else
		{
			throw std::runtime_error("MsgClientConfig: Config: Unknown property '" + key + "'!");
		}

		ch = valEnd + 1; // Parse next entry
	}
}


void
PlayerSocket::SendProxyHelloMessages()
{
	Util::MsgProxyHelloCollection msgsHello;
	msgsHello.settings.chat = MsgProxySettings::CHAT_FULL;
	msgsHello.settings.statistics = MsgProxySettings::STATS_ALL;
	msgsHello.basicServiceInfo.flags = MsgProxyServiceInfo::SERVICE_AVAILABLE | MsgProxyServiceInfo::SERVICE_LOCAL;
	msgsHello.basicServiceInfo.minutesRemaining = 0;
	std::memcpy(msgsHello.basicServiceInfo.serviceName, m_serviceName, sizeof(m_serviceName));

	Util::MsgProxyServiceInfoCollection msgsServiceInfo;
	msgsServiceInfo.serviceInfo = msgsHello.basicServiceInfo;
	msgsServiceInfo.serviceInfo.reason = MsgProxyServiceInfo::SERVICE_CONNECT;
	msgsServiceInfo.basicServiceInfo = msgsHello.basicServiceInfo;

	SendGenericMessage<3>(std::move(msgsHello));
	SendGenericMessage<2>(std::move(msgsServiceInfo));

	m_proxyConnected = true;
	m_state = STATE_UNCONFIGURED;
}

void
PlayerSocket::SendProxyServiceInfoMessages(MsgProxyServiceInfo::Reason reason)
{
	Util::MsgProxyServiceInfoCollection msgsServiceInfo;

	msgsServiceInfo.basicServiceInfo.flags = MsgProxyServiceInfo::SERVICE_AVAILABLE | MsgProxyServiceInfo::SERVICE_LOCAL;
	msgsServiceInfo.basicServiceInfo.minutesRemaining = 0;
	std::memcpy(msgsServiceInfo.basicServiceInfo.serviceName, m_serviceName, sizeof(m_serviceName));

	msgsServiceInfo.serviceInfo = msgsServiceInfo.basicServiceInfo;
	msgsServiceInfo.serviceInfo.reason = reason;

	SendGenericMessage<2>(std::move(msgsServiceInfo), AdjustedSize<Util::MsgProxyServiceInfoCollection>, true);

	if (reason == MsgProxyServiceInfo::SERVICE_CONNECT)
	{
		assert(m_state != STATE_PLAYING);

		m_proxyConnected = true;
		m_state = STATE_UNCONFIGURED;
	}
	else if (reason == MsgProxyServiceInfo::SERVICE_DISCONNECT)
	{
		if (m_match)
		{
			m_match->DisconnectedPlayer(*this);
			m_match = nullptr;
		}
		m_proxyConnected = false;
		m_state = STATE_PROXY_DISCONNECTED;
	}
}


MsgConnectionHello
PlayerSocket::ConstructHelloMessage() const
{
	WinXP::MsgConnectionHello helloMessage;
	helloMessage.key = m_securityKey;
	helloMessage.machineGUID = m_machineGUID;
	return helloMessage;
}

}
