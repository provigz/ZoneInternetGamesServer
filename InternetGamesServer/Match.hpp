#pragma once

#define WIN32_LEAN_AND_MEAN

#include <array>
#include <ctime>
#include <string>
#include <vector>

#include <windows.h>
#include <rpc.h>
#include <rpcdce.h>

#define MATCH_NO_DISCONNECT_ON_PLAYER_LEAVE 0 // DEBUG: If a player leaves a match, do not disconnect other players.

#define MATCH_MAX_PLAYERS 4

// General timeout time for mutexes related to matches
#define MATCH_MUTEX_TIMEOUT_MS 5000

template<typename P>
class Match
{
public:
	Match(unsigned int index, P& player) :
		m_guid(),
		m_index(index),
		m_creationTime(std::time(nullptr)),
		m_players(),
		m_playerSeatsComputer({})
	{
		// Generate a unique GUID for the match
		UuidCreate(const_cast<GUID*>(&m_guid));
	}
	virtual ~Match() = default;

	virtual int8_t GetRequiredPlayerCount() const { return 2; }
	virtual bool SupportsComputerPlayers() const { return false; }

	inline unsigned int GetIndex() const { return m_index; }
	inline GUID GetGUID() const { return m_guid; }
	inline std::time_t GetCreationTime() const { return m_creationTime; }

protected:
	void AddPlayer(P& player)
	{
		// Add to players array
		m_players.push_back(&player);
	}
	void RemovePlayer(const P& player)
	{
		// Remove from players array
		if (!m_players.empty())
			m_players.erase(std::remove(m_players.begin(), m_players.end(), &player), m_players.end());
	}

protected:
	const GUID m_guid;
	const unsigned int m_index;
	const std::time_t m_creationTime;

	std::vector<P*> m_players; // Must always contain valid, non-null pointers! (for easy indexing)
	std::array<bool, MATCH_MAX_PLAYERS> m_playerSeatsComputer; // Indicates whether a seat is for a computer player.

private:
	Match(const Match&) = delete;
	Match operator=(const Match&) = delete;
};
