#include "Match.hpp"

#include <algorithm>
#include <iostream>
#include <synchapi.h>
#include <sstream>

#include "PlayerSocket.hpp"
#include "../Config.hpp"
#include "../Util.hpp"

namespace Win7 {

Match::Game
Match::GameFromString(const std::string& str)
{
	if (str == "wnbk")
		return Game::BACKGAMMON;
	else if (str == "wnck")
		return Game::CHECKERS;
	else if (str == "wnsp")
		return Game::SPADES;

	return Game::INVALID;
}

std::string
Match::GameToNameString(Match::Game game)
{
	switch (game)
	{
		case Game::BACKGAMMON:
			return "Backgammon";
		case Game::CHECKERS:
			return "Checkers";
		case Game::SPADES:
			return "Spades";
		default:
			return "Invalid";
	}
}

Match::Level
Match::LevelFromPublicELO(const std::string& str)
{
	if (str == "1000")
		return Level::BEGINNER;
	else if (str == "2000")
		return Level::INTERMEDIATE;
	else if (str == "3000")
		return Level::EXPERT;

	return Level::INVALID;
}

std::string
Match::LevelToString(Match::Level level)
{
	switch (level)
	{
		case Level::BEGINNER:
			return "BEGINNER";
		case Level::INTERMEDIATE:
			return "INTERMEDIATE";
		case Level::EXPERT:
			return "EXPERT";
		default:
			return "<invalid>";
	}
}

std::string
Match::StateToString(Match::State state)
{
	switch (state)
	{
		case STATE_WAITINGFORPLAYERS:
			return "STATE_WAITINGFORPLAYERS";
		case STATE_PLAYING:
			return "STATE_PLAYING";
		case STATE_GAMEOVER:
			return "STATE_GAMEOVER";
		case STATE_ENDED:
			return "STATE_ENDED";
		default:
			return "<unknown>";
	}
}


Match::QueuedEvent::QueuedEvent(const std::string& xml_, bool includeSender_) :
	xml(xml_),
	xmlSender(),
	includeSender(includeSender_)
{}

Match::QueuedEvent::QueuedEvent(const std::string& xml_, const std::string& xmlSender_) :
	xml(xml_),
	xmlSender(xmlSender_),
	includeSender(false)
{}


Match::Match(unsigned int index, PlayerSocket& player) :
	::Match<PlayerSocket>(index, player),
	m_state(STATE_WAITINGFORPLAYERS),
	m_level(player.GetLevel()),
	m_mutex(CreateMutex(nullptr, false, nullptr)),
	m_endTime(0)
{
	JoinPlayer(player);
}

Match::~Match()
{
	// Match has ended, so disconnect any remaining players
	for (PlayerSocket* p : m_players)
		p->OnMatchDisconnect();

	CloseHandle(m_mutex);
}


void
Match::JoinPlayer(PlayerSocket& player)
{
	if (m_state != STATE_WAITINGFORPLAYERS)
		return;

	switch (WaitForSingleObject(m_mutex, MATCH_MUTEX_TIMEOUT_MS))
	{
		case WAIT_OBJECT_0: // Acquired ownership of the mutex
			break;
		case WAIT_TIMEOUT:
			throw MutexError("Win7::Match::JoinPlayer(): Timed out waiting for mutex: " + std::to_string(GetLastError()));
		case WAIT_ABANDONED: // Acquired ownership of an abandoned mutex
			throw MutexError("Win7::Match::JoinPlayer(): Got ownership of an abandoned mutex: " + std::to_string(GetLastError()));
		default:
			throw MutexError("Win7::Match::JoinPlayer(): An error occured waiting for mutex: " + std::to_string(GetLastError()));
	}

	AddPlayer(player);

	if (!ReleaseMutex(m_mutex))
		throw MutexError("Win7::Match::JoinPlayer(): Couldn't release mutex: " + std::to_string(GetLastError()));
}

void
Match::DisconnectedPlayer(PlayerSocket& player)
{
	switch (WaitForSingleObject(m_mutex, MATCH_MUTEX_TIMEOUT_MS))
	{
		case WAIT_OBJECT_0: // Acquired ownership of the mutex
			break;
		case WAIT_TIMEOUT:
			throw MutexError("Win7::Match::DisconnectedPlayer(): Timed out waiting for mutex: " + std::to_string(GetLastError()));
		case WAIT_ABANDONED: // Acquired ownership of an abandoned mutex
			throw MutexError("Win7::Match::DisconnectedPlayer(): Got ownership of an abandoned mutex: " + std::to_string(GetLastError()));
		default:
			throw MutexError("Win7::Match::DisconnectedPlayer(): An error occured waiting for mutex: " + std::to_string(GetLastError()));
	}

	RemovePlayer(player);

	// End the match on no players, marking it as to-be-removed from MatchManager
	if (m_players.empty())
	{
		m_state = STATE_ENDED;
	}
	// Originally, servers replaced players who have left the game with computer (AI) players.
	// Based on the game, either replace with computer player, or end the game directly by disconnecting everyone.
	else if (m_state == STATE_PLAYING)
	{
		if ((g_config.allowSinglePlayer || m_players.size() > 1) && SupportsComputerPlayers())
		{
			const std::string replaceWithAIXML =
				StateSTag::ConstructMethodMessage("GameManagement", "ReplaceWithAI",
					std::to_string(player.m_role) + ",");
			for (PlayerSocket* p : m_players)
				p->OnEventReceive(replaceWithAIXML);

			m_playerSeatsComputer[player.m_role] = true;

			OnReplacePlayer(player);
		}
#if not MATCH_NO_DISCONNECT_ON_PLAYER_LEAVE
		else
		{
			// Disconnect any remaining players
			for (PlayerSocket* p : m_players)
				p->OnMatchDisconnect();
			m_players.clear();

			m_state = STATE_ENDED;
		}
#endif
	}

	if (!ReleaseMutex(m_mutex))
		throw MutexError("Win7::Match::JoinPlayer(): Couldn't release mutex: " + std::to_string(GetLastError()));
}


void
Match::Update()
{
	switch (m_state)
	{
		case STATE_WAITINGFORPLAYERS:
		{
			// Start the game, if all players are waiting for opponents
			if (m_players.size() == GetRequiredPlayerCount() &&
				std::all_of(m_players.begin(), m_players.end(),
					[](const auto& player) { return player->GetState() == PlayerSocket::STATE_WAITINGFOROPPONENTS; }))
			{
				// Distribute unique role IDs for each player, starting from 0
				const int playerCount = static_cast<int>(m_players.size());
				const std::vector<int> roles = GenerateUniqueRandomNums(0, playerCount - 1);
				for (int i = 0; i < playerCount; i++)
					const_cast<int8_t&>(m_players[i]->m_role) = static_cast<int8_t>(roles[i]);

				for (PlayerSocket* p : m_players)
					p->OnGameStart();

				SessionLog() << "[MATCH] " << m_guid << ": Started match!" << std::endl;
				m_state = STATE_PLAYING;
			}
			break;
		}
		case STATE_GAMEOVER:
		{
			if (m_endTime == 0)
			{
				SessionLog() << "[MATCH] " << m_guid << ": Game over, match will automatically close in 60 seconds!" << std::endl;
				m_endTime = GetTickCount();
			}
			else if (GetTickCount() - m_endTime >= 60000) // A minute has passed since the match ended
			{
				SessionLog() << "[MATCH] " << m_guid << ": Match ended a minute ago, closing!" << std::endl;
				m_state = STATE_ENDED;
			}
			break;
		}

		default:
			break;
	}
}


void
Match::EventSend(const PlayerSocket& caller, const std::string& xml)
{
	if (m_state != STATE_PLAYING)
		return;

	/* Get event XML element */
	tinyxml2::XMLDocument eventDoc;
	tinyxml2::XMLError status = eventDoc.Parse(xml.c_str());
	if (status != tinyxml2::XML_SUCCESS)
		return;

	const tinyxml2::XMLElement* elMessage = eventDoc.RootElement();
	if (!elMessage || strcmp(elMessage->Name(), "Message"))
		return;

	const tinyxml2::XMLElement* elEvent = elMessage->FirstChildElement();
	if (!elEvent || !elEvent->Name())
		return;

	/* Process event */
	switch (WaitForSingleObject(m_mutex, MATCH_MUTEX_TIMEOUT_MS))
	{
		case WAIT_OBJECT_0: // Acquired ownership of the mutex
			break;
		case WAIT_TIMEOUT:
			throw MutexError("Win7::Match::EventSend(): Timed out waiting for mutex: " + std::to_string(GetLastError()));
		case WAIT_ABANDONED: // Acquired ownership of an abandoned mutex
			throw MutexError("Win7::Match::EventSend(): Got ownership of an abandoned mutex: " + std::to_string(GetLastError()));
		default:
			throw MutexError("Win7::Match::EventSend(): An error occured waiting for mutex: " + std::to_string(GetLastError()));
	}
	try
	{
		const std::vector<QueuedEvent> events = ProcessEvent(*elEvent, caller);
		for (const QueuedEvent& ev : events)
		{
			if (!ev.xml.empty())
			{
				const bool includeSender = ev.includeSender && ev.xmlSender.empty();
				for (const PlayerSocket* p : m_players)
				{
					if (includeSender || p != &caller)
						p->OnEventReceive(ev.xml);
				}
			}
			if (!ev.xmlSender.empty())
			{
				caller.OnEventReceive(ev.xmlSender);
			}
		}
	}
	catch (...)
	{
		ReleaseMutex(m_mutex);
		throw;
	}
	if (!ReleaseMutex(m_mutex))
		throw MutexError("Win7::Match::EventSend(): Couldn't release mutex: " + std::to_string(GetLastError()));
}

void
Match::Chat(StateChatTag tag)
{
	if (m_state != STATE_PLAYING && m_state != STATE_GAMEOVER)
		return;

	// Validate the chat event
	if (tag.text != "SYS_CHATON" && // Chat turned on
		tag.text != "SYS_CHATOFF" && // Chat turned off
		!IsValidChatNudgeMessage(tag.text)) // Nudge
	{
		try
		{
			// Ensure the chat message has a valid ID, as clients tend to accept any other custom messages
			const uint8_t msgID = static_cast<uint8_t>(std::stoi(tag.text));
			if (msgID < 1)
				return;
			if (msgID > 24)
			{
				const std::pair<uint8_t, uint8_t> customMsgRange = GetCustomChatMessagesRange();
				if (msgID < customMsgRange.first || msgID > customMsgRange.second)
					return;
			}

			tag.text = std::to_string(msgID);
		}
		catch (const std::exception&)
		{
			return;
		}
	}

	// Send the event to all other players
	switch (WaitForSingleObject(m_mutex, MATCH_MUTEX_TIMEOUT_MS))
	{
		case WAIT_OBJECT_0: // Acquired ownership of the mutex
			break;
		case WAIT_TIMEOUT:
			throw MutexError("Win7::Match::Chat(): Timed out waiting for mutex: " + std::to_string(GetLastError()));
		case WAIT_ABANDONED: // Acquired ownership of an abandoned mutex
			throw MutexError("Win7::Match::Chat(): Got ownership of an abandoned mutex: " + std::to_string(GetLastError()));
		default:
			throw MutexError("Win7::Match::Chat(): An error occured waiting for mutex: " + std::to_string(GetLastError()));
	}
	for (PlayerSocket* p : m_players)
	{
		p->OnChat(&tag);
	}
	if (!ReleaseMutex(m_mutex))
		throw MutexError("Win7::Match::Chat(): Couldn't release mutex: " + std::to_string(GetLastError()));
}


std::string
Match::ConstructReadyXML() const
{
	XMLPrinter printer;
	printer.OpenElement("ReadyMessage");

	NewElementWithText(printer, "eStatus", "Ready");
	NewElementWithText(printer, "sMode", "normal");

	printer.CloseElement("ReadyMessage");
	return printer;
}

std::string
Match::ConstructStateXML(const std::vector<const StateTag*> tags) const
{
	XMLPrinter printer;
	printer.OpenElement("StateMessage");

	NewElementWithText(printer, "nSeq", "4"); // TODO: Figure out what "nSeq" is for. Currently it doesn't seem to matter
	NewElementWithText(printer, "nRole", "0"); // TODO: Figure out what "nRole" is for. Currently it doesn't seem to matter
	NewElementWithText(printer, "eStatus", "Ready");
	NewElementWithText(printer, "nTimestamp", std::time(nullptr) - m_creationTime);
	NewElementWithText(printer, "sMode", "normal");

	// Tags
	printer.OpenElement("arTags");
	for (const StateTag* tag : tags)
		tag->AppendToTags(printer);
	printer.CloseElement("arTags");

	printer.CloseElement("StateMessage");
	return printer;
}

std::string
Match::ConstructGameInitXML(PlayerSocket* caller) const
{
	XMLPrinter printer;
	printer.OpenElement("GameInit");

	NewElementWithText(printer, "Role", caller->m_role);

	// Players
	printer.OpenElement("Players");
	for (PlayerSocket* player : m_players)
	{
		printer.OpenElement("Player");
		NewElementWithText(printer, "Role", player->m_role);
		NewElementWithText(printer, "Name", player->GetPUID());
		NewElementWithText(printer, "Type", "Human");
		printer.CloseElement("Player");
	}
	printer.CloseElement("Players");

	AppendToGameInitXML(printer, caller);

	printer.CloseElement("GameInit");
	return printer;
}

std::vector<std::string>
Match::ConstructGameStartMessagesXML(const PlayerSocket&)
{
	return {};
}


bool
Match::IsValidChatNudgeMessage(const std::string& msg) const
{
	return msg == "1400_12345" || // Nudge (player 1)
		msg == "1400_12346"; // Nudge (player 2)
}

}
