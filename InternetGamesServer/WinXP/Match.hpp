#pragma once

#include "../Match.hpp"

#include "Protocol/Game.hpp"
#include "../Util.hpp"

class MatchManager;

namespace WinXP {

class PlayerSocket;

class Match : public ::Match<PlayerSocket>
{
	friend class ::MatchManager;

public:
	enum class Game {
		INVALID = 0,
		BACKGAMMON,
		CHECKERS,
		SPADES,
		HEARTS,
		REVERSI
	};
	static Game GameFromString(const std::string& str);
	static std::string GameToNameString(Game game);

	enum class SkillLevel {
		INVALID = -1,
		BEGINNER,
		INTERMEDIATE,
		EXPERT
	};
	static std::string SkillLevelToString(SkillLevel level);

	enum State {
		STATE_WAITINGFORPLAYERS,
		STATE_PLAYING,
		STATE_GAMEOVER,
		STATE_ENDED
	};
	static std::string StateToString(State state);

public:
	Match(unsigned int index, PlayerSocket& player);
	~Match() override;

	void JoinPlayer(PlayerSocket& player);
	void DisconnectedPlayer(PlayerSocket& player);

	/** Update match logic */
	void Update();

	virtual Game GetGame() const = 0;
	inline State GetState() const { return m_state; }
	inline SkillLevel GetSkillLevel() const { return m_skillLevel; }
	inline uint32 GetGameID() const { return m_guid.Data1; }

	inline uint32 GetComputerPlayerID(int16 seat) const { return m_playerComputerIDs.at(seat); }

	/** Processing messages */
	void ProcessMessage(const MsgChatSwitch& msg);
	void ProcessIncomingGameMessage(PlayerSocket& player, uint32 type);

protected:
	/** Processing messages */
	virtual void ProcessIncomingGameMessageImpl(PlayerSocket& player, uint32 type) = 0;

	virtual void OnReplacePlayer(const PlayerSocket& player, uint32 userIDNew) {}

	// Returns message ID, 0 on failure
	static uint8_t ValidateChatMessage(const std::wstring& chatMsg, uint8_t customRangeStart, uint8_t customRangeEnd);

protected:
	/** Sending utilities */
	template<uint32 Type, typename T>
	void BroadcastGenericMessage(const T& msgApp, int excludePlayerSeat = -1, int len = sizeof(T))
	{
		for (PlayerSocket* player : m_players)
		{
			if (player->m_seat != excludePlayerSeat)
				player->OnMatchGenericMessage<Type>(msgApp, len);
		}
	}
	template<uint32 Type, typename T>
	void BroadcastGameMessage(const T& msgGame, int excludePlayerSeat = -1, int len = sizeof(T))
	{
		for (PlayerSocket* player : m_players)
		{
			if (player->m_seat != excludePlayerSeat)
				player->OnMatchGameMessage<Type>(msgGame, len);
		}
	}
	template<uint32 Type, typename T, typename M, uint16 MessageLen> // Trailing data array after T
	void BroadcastGameMessage(const T& msgGame, const Array<M, MessageLen>& msgGameSecond, int excludePlayerSeat = -1)
	{
		for (PlayerSocket* player : m_players)
		{
			if (player->m_seat != excludePlayerSeat)
				player->OnMatchGameMessage<Type>(msgGame, msgGameSecond);
		}
	}

protected:
	State m_state;

	const SkillLevel m_skillLevel;

private:
	// Mutex to prevent simultaneous match processes, like adding/removing players and processing messages and removal of the match
	HANDLE m_mutex;

	std::array<uint32, MATCH_MAX_PLAYERS> m_playerComputerIDs; // IDs of computer players by seat.

	std::time_t m_endTime;

private:
	Match(const Match&) = delete;
	Match operator=(const Match&) = delete;
};

}
