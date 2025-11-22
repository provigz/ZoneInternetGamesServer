#pragma once

#include "Match.hpp"

#include <array>
#include <random>

namespace WinXP {

#define HeartsNumPlayers 4
#define HeartsNumCardsInPass 3

class HeartsMatch final : public Match
{
public:
	HeartsMatch(unsigned int index, PlayerSocket& player);

	int8_t GetRequiredPlayerCount() const override { return HeartsNumPlayers; }
	bool SupportsComputerPlayers() const override { return true; }

	Game GetGame() const override { return Game::HEARTS; }

protected:
	/** Processing messages */
	void ProcessIncomingGameMessageImpl(PlayerSocket& player, uint32 type) override;

	void OnReplacePlayer(const PlayerSocket& player, uint32 userIDNew) override;

public:
	typedef int8_t Card;
	typedef std::vector<Card> CardArray;

private:
	void Reset();
	void ResetHand();

	void RegisterCheckIn(int16 seat);
	void ProcessPass(int16 seat, std::array<Card, HeartsNumCardsInPass> passCards);
	void ProcessPlayCard(int16 seat, Card card);

	class CardTrick final
	{
	public:
		CardTrick();

		bool IsEmpty() const;

		void Reset();
		void Set(int16 player, Card card);

		bool FollowsSuit(Card card, const CardArray& hand) const;

		bool IsFinished() const;
		int16 GetWinner() const;
		int16 GetPoints() const;

		Card GetAutoCard(const std::vector<Card>& hand, bool pointsBroken) const;

	private:
		uint8_t GetCardHighestPlayedRank(int8_t suit) const;

	private:
		Card m_leadCard;
		std::array<Card, HeartsNumPlayers> m_playerCards;
	};

private:
	enum class MatchState
	{
		INITIALIZING,
		PASSING,
		PLAYING,
		ENDED
	};
	MatchState m_matchState;

	std::array<bool, 4> m_playersCheckedIn;
	std::array<CardArray, 4> m_playerCards;

	int16 m_passDirection;
	std::array<bool, 4> m_playersPassedCards;

	int16 m_playerTurn;
	bool m_pointsBroken;
	std::array<int16, 4> m_playerHandPoints;
	std::array<int16, 4> m_playerTotalPoints;

	CardTrick m_currentTrick;

private:
	HeartsMatch(const HeartsMatch&) = delete;
	HeartsMatch operator=(const HeartsMatch&) = delete;
};

}
