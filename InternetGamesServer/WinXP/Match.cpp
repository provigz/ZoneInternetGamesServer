#include "Match.hpp"

#include <algorithm>
#include <iostream>

#include "PlayerSocket.hpp"
#include "../Config.hpp"

namespace WinXP {

Match::Game
Match::GameFromString(const std::string& str)
{
	if (str == "BCKGZM")
		return Game::BACKGAMMON;
	else if (str == "CHKRZM")
		return Game::CHECKERS;
	else if (str == "SHVLZM")
		return Game::SPADES;
	else if (str == "HRTZZM")
		return Game::HEARTS;
	else if (str == "RVSEZM")
		return Game::REVERSI;

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
		case Game::HEARTS:
			return "Hearts";
		case Game::REVERSI:
			return "Reversi";
		default:
			return "Invalid";
	}
}

std::string
Match::SkillLevelToString(Match::SkillLevel level)
{
	switch (level)
	{
	case SkillLevel::BEGINNER:
		return "BEGINNER";
	case SkillLevel::INTERMEDIATE:
		return "INTERMEDIATE";
	case SkillLevel::EXPERT:
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


#define MATCH_NO_DISCONNECT_ON_PLAYER_LEAVE 0 // DEBUG: If a player leaves a match, do not disconnect other players.

Match::Match(unsigned int index, PlayerSocket& player) :
	::Match<PlayerSocket>(index, player),
	m_state(STATE_WAITINGFORPLAYERS),
	m_skillLevel(player.GetSkillLevel()),
	m_mutex(CreateMutex(nullptr, false, nullptr)),
	m_playerComputerIDs({}),
	m_endTime(0)
{
	JoinPlayer(player);
}

Match::~Match()
{
	// Match has ended, so disconnect any remaining players
	for (PlayerSocket* p : m_players)
	{
		try
		{
			p->OnMatchDisconnect();
		}
		catch (const std::exception& err)
		{
			SessionLog() << "[MATCH] " << m_guid
				<< ": Couldn't disconnect socket from this match! Disconnecting from server instead. Error: "
				<< err.what() << std::endl;
			p->Disconnect();
		}
	}

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
			throw MutexError("WinXP::Match::JoinPlayer(): Timed out waiting for mutex: " + std::to_string(GetLastError()));
		case WAIT_ABANDONED: // Acquired ownership of an abandoned mutex
			throw MutexError("WinXP::Match::JoinPlayer(): Got ownership of an abandoned mutex: " + std::to_string(GetLastError()));
		default:
			throw MutexError("WinXP::Match::JoinPlayer(): An error occured waiting for mutex: " + std::to_string(GetLastError()));
	}

	AddPlayer(player);

	MsgServerStatus msgServerStatus;
	msgServerStatus.playersWaiting = static_cast<int>(m_players.size());
	for (PlayerSocket* p : m_players)
		p->OnMatchGenericMessage<MessageServerStatus>(msgServerStatus);

	if (!ReleaseMutex(m_mutex))
		throw MutexError("WinXP::Match::JoinPlayer(): Couldn't release mutex: " + std::to_string(GetLastError()));
}

void
Match::DisconnectedPlayer(PlayerSocket& player)
{
	switch (WaitForSingleObject(m_mutex, MATCH_MUTEX_TIMEOUT_MS))
	{
		case WAIT_OBJECT_0: // Acquired ownership of the mutex
			break;
		case WAIT_TIMEOUT:
			throw MutexError("WinXP::Match::DisconnectedPlayer(): Timed out waiting for mutex: " + std::to_string(GetLastError()));
		case WAIT_ABANDONED: // Acquired ownership of an abandoned mutex
			throw MutexError("WinXP::Match::DisconnectedPlayer(): Got ownership of an abandoned mutex: " + std::to_string(GetLastError()));
		default:
			throw MutexError("WinXP::Match::DisconnectedPlayer(): An error occured waiting for mutex: " + std::to_string(GetLastError()));
	}

	RemovePlayer(player);

	// End the match on no players, marking it as to-be-removed from MatchManager
	if (m_players.empty())
	{
		m_state = STATE_ENDED;
	}
	else if (m_state == STATE_WAITINGFORPLAYERS)
	{
		MsgServerStatus msgServerStatus;
		msgServerStatus.playersWaiting = static_cast<int>(m_players.size());
		BroadcastGenericMessage<MessageServerStatus>(msgServerStatus);
	}
	// Originally, servers replaced players who have left the game with computer (AI) players.
	// Based on the game, either replace with computer player, or end the game directly by disconnecting everyone.
	else if (m_state == STATE_PLAYING || m_state == STATE_GAMEOVER)
	{
		if ((g_config.allowSinglePlayer || m_players.size() > 1) && SupportsComputerPlayers())
		{
			const uint32 userID = std::uniform_int_distribution<uint32>{}(g_rng);

			MsgPlayerReplaced msgPlayerReplaced;
			msgPlayerReplaced.userIDOld = player.GetID();
			msgPlayerReplaced.userIDNew = userID;
			BroadcastGenericMessage<MessagePlayerReplaced>(msgPlayerReplaced);

			m_playerSeatsComputer[player.m_seat] = true;
			m_playerComputerIDs[player.m_seat] = userID;

			OnReplacePlayer(player, userID);
		}
#if not MATCH_NO_DISCONNECT_ON_PLAYER_LEAVE
		else
		{
			SessionLog() << "[MATCH] " << m_guid << ": A player left, closing match!" << std::endl;

			for (PlayerSocket* p : m_players)
			{
				try
				{
					p->OnMatchDisconnect();
				}
				catch (const std::exception& err)
				{
					SessionLog() << "[MATCH] " << m_guid
						<< ": Couldn't disconnect socket from this match! Disconnecting from server instead. Error: "
						<< err.what() << std::endl;
					p->Disconnect();
				}
			}
			m_players.clear();

			m_state = STATE_ENDED;
		}
#endif
	}

	if (!ReleaseMutex(m_mutex))
		throw MutexError("WinXP::Match::DisconnectedPlayer(): Couldn't release mutex: " + std::to_string(GetLastError()));
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
				// Distribute unique IDs for each player, starting from 0
				const int playerCount = static_cast<int>(m_players.size());
				const std::vector<int> seats = GenerateUniqueRandomNums(0, playerCount - 1);
				for (int i = 0; i < playerCount; i++)
					const_cast<int16&>(m_players[i]->m_seat) = seats[i];

				for (PlayerSocket* p : m_players)
					p->OnGameStart(m_players);

				SessionLog() << "[MATCH] " << m_guid << ": Started match!" << std::endl;
				m_state = STATE_PLAYING;
			}
			break;
		}
		case STATE_PLAYING:
		{
			if (m_endTime != 0)
			{
				SessionLog() << "[MATCH] " << m_guid << ": Playing state restored, cancelling game over close timer." << std::endl;
				m_endTime = 0;
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
Match::ProcessMessage(const MsgChatSwitch& msg)
{
	switch (WaitForSingleObject(m_mutex, MATCH_MUTEX_TIMEOUT_MS))
	{
		case WAIT_OBJECT_0: // Acquired ownership of the mutex
			break;
		case WAIT_TIMEOUT:
			throw MutexError("WinXP::Match::ProcessMessage(): Timed out waiting for mutex: " + std::to_string(GetLastError()));
		case WAIT_ABANDONED: // Acquired ownership of an abandoned mutex
			throw MutexError("WinXP::Match::ProcessMessage(): Got ownership of an abandoned mutex: " + std::to_string(GetLastError()));
		default:
			throw MutexError("WinXP::Match::ProcessMessage(): An error occured waiting for mutex: " + std::to_string(GetLastError()));
	}

	BroadcastGenericMessage<MessageChatSwitch>(msg);

	if (!ReleaseMutex(m_mutex))
		throw MutexError("WinXP::Match::ProcessMessage(): Couldn't release mutex: " + std::to_string(GetLastError()));
}

void
Match::ProcessIncomingGameMessage(PlayerSocket& player, uint32 type)
{
	switch (WaitForSingleObject(m_mutex, MATCH_MUTEX_TIMEOUT_MS))
	{
		case WAIT_OBJECT_0: // Acquired ownership of the mutex
			break;
		case WAIT_TIMEOUT:
			throw MutexError("WinXP::Match::ProcessIncomingGameMessage(): Timed out waiting for mutex: " + std::to_string(GetLastError()));
		case WAIT_ABANDONED: // Acquired ownership of an abandoned mutex
			throw MutexError("WinXP::Match::ProcessIncomingGameMessage(): Got ownership of an abandoned mutex: " + std::to_string(GetLastError()));
		default:
			throw MutexError("WinXP::Match::ProcessIncomingGameMessage(): An error occured waiting for mutex: " + std::to_string(GetLastError()));
	}

	try
	{
		ProcessIncomingGameMessageImpl(player, type);
	}
	catch (...)
	{
		ReleaseMutex(m_mutex);
		throw;
	}

	if (!ReleaseMutex(m_mutex))
		throw MutexError("WinXP::Match::ProcessIncomingGameMessage(): Couldn't release mutex: " + std::to_string(GetLastError()));
}


uint8_t // Returns message ID, 0 on failure
Match::ValidateChatMessage(const std::wstring& chatMsg, uint8_t customRangeStart, uint8_t customRangeEnd)
{
	if (chatMsg.empty() || chatMsg[0] != L'/')
		return 0;

	try
	{
		size_t lastIDPos;
		const int msgID = std::stoi(chatMsg.substr(1), &lastIDPos);

		if (lastIDPos + 1 < chatMsg.size() && !std::isspace(chatMsg[lastIDPos + 1]))
			return 0;

		if ((msgID >= 1 && msgID <= 24) || // Common messages
			(msgID >= customRangeStart && msgID <= customRangeEnd)) // Custom (per-game) messages
			return msgID;
	}
	catch (const std::exception&)
	{
		return 0;
	}

	return 0;
}

}
