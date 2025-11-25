#include "PlayerSocket.hpp"

#include <cassert>
#include <stdexcept>
#include <sstream>

#include "../MatchManager.hpp"

namespace Win7 {

std::string
PlayerSocket::StateToString(State state)
{
	switch (state)
	{
		case STATE_INITIALIZED:
			return "STATE_INITIALIZED";
		case STATE_JOINING:
			return "STATE_JOINING";
		case STATE_JOININGCONFIRM:
			return "STATE_JOININGCONFIRM";
		case STATE_WAITINGFOROPPONENTS:
			return "STATE_WAITINGFOROPPONENTS";
		case STATE_PLAYING:
			return "STATE_PLAYING";
		default:
			return "<unknown>";
	}
}


PlayerSocket::PlayerSocket(Socket& socket) :
	::PlayerSocket(socket),
	m_state(STATE_INITIALIZED),
	m_guid(),
	m_puid(),
	m_game(Match::Game::INVALID),
	m_level(Match::Level::INVALID),
	m_match(),
	m_role(-1)
{
}

PlayerSocket::~PlayerSocket()
{
	Destroy();
}


void
PlayerSocket::ProcessMessages()
{
	std::string messageBuffer;
	char receivedBuf[2048];
	while (true)
	{
		const int receivedLen = m_socket.ReceiveData(receivedBuf, sizeof(receivedBuf));
		messageBuffer.append(receivedBuf, receivedLen);

		size_t lineBreakPos;
		while ((lineBreakPos = messageBuffer.find("\r\n")) != std::string::npos)
		{
			const std::string message = messageBuffer.substr(0, lineBreakPos);
			messageBuffer.erase(0, lineBreakPos + 2);

			if (message.empty())
				continue;

			m_socket.SendData(GetResponse(StringSplit(message, "&"))); // Split data by "&" for easier parsing in certain cases
		}

		// Time out the client in states not involving participation in a match
		if (m_state != STATE_WAITINGFOROPPONENTS &&
			m_state != STATE_PLAYING &&
			m_state.GetSecondsSinceLastChange() >= 60)
		{
			throw std::runtime_error("Win7::PlayerSocket::ProcessMessages(): Timeout: Client has not switched from state "
				+ std::to_string(m_state) + " for 60 seconds or more!");
		}
	}
}

std::string
PlayerSocket::GetResponse(const std::vector<std::string>& receivedData)
{
	if (receivedData[0] == "LEAVE\r\n") // The client has requested to be disconnected from the server
	{
		Disconnect();
		return {};
	}

	switch (m_state)
	{
		case STATE_INITIALIZED:
			if (receivedData.size() >= 6 && StartsWith(receivedData[0], "JOIN Session="))
			{
				ParseSasTicket(receivedData[2]);
				ParseGasTicket(receivedData[3]);
				ParsePasTicket(receivedData[4]);

				// Find/Create a lobby (pending match), based on the game to be played and skill level
				m_match = MatchManager::Get().FindLobby(*this);

				m_state = STATE_JOINING;
				m_guid = StringSplit(receivedData[0], "=")[1];
				return ConstructJoinContextMessage();
			}
			break;

		case STATE_JOINING:
			if (StartsWith(receivedData[0], "PLAY match"))
			{
				m_state = STATE_JOININGCONFIRM;
				return {};
			}
			break;
		case STATE_JOININGCONFIRM:
			if (StartsWith(receivedData[0], "AT ")) // TODO: Maybe parse and check the following GUIDs... although it doesn't matter
			{
				m_state = STATE_WAITINGFOROPPONENTS;
				return ConstructReadyMessage() + ConstructStateMessage(m_match->ConstructReadyXML());
			}
			break;

		case STATE_PLAYING:
			if (StartsWith(receivedData[0], "CALL GameReady")) // Game is ready, start it
			{
				// Send a game start message
				const StateSTag startTag = StateSTag::ConstructGameStart();
				std::string messages = ConstructStateMessage(m_match->ConstructStateXML({ &startTag }));

				// Include any additional messages, given on game start, by the match
				const std::vector<std::string> stateXMLs = m_match->ConstructGameStartMessagesXML(*this);
				for (const std::string& stateXML : stateXMLs)
				{
					const StateSTag stateTag = StateSTag::ConstructEventReceive(stateXML);
					messages += ConstructStateMessage(m_match->ConstructStateXML({ &stateTag }));
				}

				return messages;
			}
			else if (receivedData[0] == "CALL EventSend messageID=EventSend" && receivedData.size() > 1 &&
				StartsWith(receivedData[1], "XMLDataString=")) // An event is being sent, let the Match send it to all other players
			{
				m_match->EventSend(*this, DecodeURL(receivedData[1].substr(14))); // Remove "XMLDataString=" from the beginning
				return {};
			}
			else if (StartsWith(receivedData[0], "CALL Chat") && receivedData.size() > 1) // A chat message was sent, let the Match send it to all players
			{
				StateChatTag tag;
				tag.userID = m_guid.substr(1, m_guid.size() - 2); // Remove braces from beginning and end
				tag.nickname = m_puid;
				tag.text = receivedData[0].substr(20); // Remove "CALL Chat sChatText=" from the beginning
				tag.fontFace = DecodeURL(receivedData[1].substr(10)); // Remove "sFontFace=" from the beginning
				tag.fontFlags = receivedData[2].substr(13); // Remove "arfFontFlags=" from the beginning
				tag.fontColor = receivedData[3].substr(11); // Remove "eFontColor=" from the beginning
				tag.fontCharSet = receivedData[4].substr(11); // Remove "eFontCharSet=" from the beginning

				m_match->Chat(std::move(tag));
				return {};
			}
			break;
	}
	if (receivedData[0] == "LEAVE") // Client has left the game, disconnect it from the server
	{
		Disconnect();
		return {};
	}
	throw std::runtime_error("Win7::PlayerSocket::GetResponse(): Invalid message for state " + std::to_string(m_state) + " received!");
}


void
PlayerSocket::OnGameStart()
{
	if (m_state != STATE_WAITINGFOROPPONENTS)
		return;

	m_state = STATE_PLAYING;

	// Send a game initialization message
	const StateSTag tag = StateSTag::ConstructGameInit(m_match->ConstructGameInitXML(this));
	m_socket.SendData(ConstructStateMessage(m_match->ConstructStateXML({ &tag })));
}

void
PlayerSocket::OnDisconnected()
{
	if (m_match)
	{
		m_match->DisconnectedPlayer(*this);
		m_match = nullptr;
	}
}

void
PlayerSocket::OnMatchDisconnect()
{
	m_match = nullptr;
	Disconnect();
}

void
PlayerSocket::OnEventReceive(const std::string& xml) const
{
	// Send an event receive message
	const StateSTag tag = StateSTag::ConstructEventReceive(xml);
	m_socket.SendData(ConstructStateMessage(m_match->ConstructStateXML({ &tag })));
}

void
PlayerSocket::OnChat(const StateChatTag* tag)
{
	// Send the "chatbyid" tag
	m_socket.SendData(ConstructStateMessage(m_match->ConstructStateXML({ tag })));
}


void
PlayerSocket::ParseSasTicket(const std::string& xml)
{
	tinyxml2::XMLDocument doc;
	tinyxml2::XMLError status = doc.Parse(xml.substr(10).c_str()); // Remove "GasTicket=" from the beginning
	if (status != tinyxml2::XML_SUCCESS)
		throw std::runtime_error("Corrupted data received: Error parsing SasTicket: " + status);

	tinyxml2::XMLElement* elAnon = doc.RootElement();
	if (!elAnon)
		throw std::runtime_error("Corrupted data received: No root element in SasTicket.");

	tinyxml2::XMLElement* elPub = elAnon->FirstChildElement("pub");
	if (!elPub)
		throw std::runtime_error("Corrupted data received: No \"<pub>...</pub>\" in SasTicket.");

	// PUID
	tinyxml2::XMLElement* elPUID = elPub->FirstChildElement("PUID");
	if (!elPUID || !elPUID->GetText())
		throw std::runtime_error("Corrupted data received: No \"<PUID>...</PUID>\" in SasTicket.");
	m_puid = elPUID->GetText();
}

void
PlayerSocket::ParseGasTicket(const std::string& xml)
{
	tinyxml2::XMLDocument doc;
	tinyxml2::XMLError status = doc.Parse(xml.substr(10).c_str()); // Remove "GasTicket=" from the beginning
	if (status != tinyxml2::XML_SUCCESS)
		throw std::runtime_error("Corrupted data received: Error parsing GasTicket: " + status);

	tinyxml2::XMLElement* elAnon = doc.RootElement();
	if (!elAnon)
		throw std::runtime_error("Corrupted data received: No root element in GasTicket.");

	tinyxml2::XMLElement* elPub = elAnon->FirstChildElement("pub");
	if (!elPub)
		throw std::runtime_error("Corrupted data received: No \"<pub>...</pub>\" in GasTicket.");

	// Game
	tinyxml2::XMLElement* elGame = elPub->FirstChildElement("Game");
	if (!elGame || !elGame->GetText())
		throw std::runtime_error("Corrupted data received: No \"<Game>...</Game>\" in GasTicket.");
	m_game = Match::GameFromString(elGame->GetText());
}

void
PlayerSocket::ParsePasTicket(const std::string& xml)
{
	tinyxml2::XMLDocument doc;
	tinyxml2::XMLError status = doc.Parse(xml.substr(10).c_str()); // Remove "GasTicket=" from the beginning
	if (status != tinyxml2::XML_SUCCESS)
		throw std::runtime_error("Corrupted data received: Error parsing PasTicket: " + status);

	tinyxml2::XMLElement* elAnon = doc.RootElement();
	if (!elAnon)
		throw std::runtime_error("Corrupted data received: No root element in PasTicket.");

	tinyxml2::XMLElement* elPub = elAnon->FirstChildElement("pub");
	if (!elPub)
		throw std::runtime_error("Corrupted data received: No \"<pub>...</pub>\" in PasTicket.");

	tinyxml2::XMLElement* elMaskedStats = elPub->FirstChildElement("MaskedStats");
	if (!elMaskedStats)
		throw std::runtime_error("Corrupted data received: No \"<MaskedStats>...</MaskedStats>\" in PasTicket.");

	tinyxml2::XMLElement* elNewDataSet = elMaskedStats->FirstChildElement("NewDataSet");
	if (!elNewDataSet)
		throw std::runtime_error("Corrupted data received: No \"<NewDataSet>...</NewDataSet>\" in PasTicket.");

	tinyxml2::XMLElement* elTable = elNewDataSet->FirstChildElement("Table");
	if (!elTable)
		throw std::runtime_error("Corrupted data received: No \"<Table>...</Table>\" in PasTicket.");

	// Difficulty
	tinyxml2::XMLElement* elZSPublicELO = elTable->FirstChildElement("ZS_PublicELO");
	if (!elZSPublicELO || !elZSPublicELO->GetText())
		throw std::runtime_error("Corrupted data received: No \"<ZS_PublicELO>...</ZS_PublicELO>\" in PasTicket.");
	m_level = Match::LevelFromPublicELO(elZSPublicELO->GetText());
}


std::string
PlayerSocket::ConstructJoinContextMessage() const
{
	// Construct JoinContext message
	std::stringstream out;
	out << "JoinContext " << m_match->GetGUID() << ' ' << m_guid << " 38&38&38&\r\n";
	return out.str();
}

std::string
PlayerSocket::ConstructReadyMessage() const
{
	// Construct READY message
	std::stringstream out;
	out << "READY " << m_match->GetGUID() << "\r\n";
	return out.str();
}

std::string
PlayerSocket::ConstructStateMessage(const std::string& xml) const
{
	// Get a hex number of XML string size
	std::stringstream hexsize;
	hexsize << std::hex << xml.length();

	// Construct STATE message
	std::stringstream out;
	out << "STATE " << m_match->GetGUID() << "\r\nLength: " << hexsize.str() << "\r\n\r\n" << xml << "\r\n";
	return out.str();
}

}
