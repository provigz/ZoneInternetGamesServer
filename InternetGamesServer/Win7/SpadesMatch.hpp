#pragma once

#include "Match.hpp"

#include "..\WinCommon\SpadesUtil.hpp"

namespace Win7 {

using namespace Spades;

constexpr uint16_t ZPA_UNSET_CARD = 0xFFFF;

class SpadesMatch final : public Match
{
public:
	SpadesMatch(unsigned int index, PlayerSocket& player);

	Game GetGame() const override { return Game::SPADES; }
	int8_t GetRequiredPlayerCount() const override { return 4; }
	bool SupportsComputerPlayers() const override { return true; }

	std::vector<std::string> ConstructGameStartMessagesXML(const PlayerSocket& caller) const override;

protected:
	std::pair<uint8_t, uint8_t> GetCustomChatMessagesRange() const override { return { 50, 54 }; }
	bool IsValidChatNudgeMessage(const std::string& msg) const override;

	std::vector<QueuedEvent> ProcessEvent(const tinyxml2::XMLElement& elEvent, const PlayerSocket& caller) override;

	void OnReplacePlayer(const PlayerSocket& player) override;

private:
	void ResetHand();

private:
	enum BidValues
	{
		BID_HAND_START = -4,
		BID_SHOWN_CARDS = -3,
		BID_DOUBLE_NIL = -2
	};

private:
	typedef uint16_t Card;
	typedef std::vector<Card> CardArray;

private:
	enum class MatchState
	{
		BIDDING,
		PLAYING
	};
	MatchState m_matchState;

	std::array<int16_t, 2> m_teamPoints;
	std::array<int16_t, 2> m_teamBags;

	int8_t m_handDealer;
	int8_t m_nextBidPlayer;
	std::array<int8_t, 4> m_playerBids;
	std::array<CardArray, 4> m_playerCards;
	int8_t m_playerTurn;
	int8_t m_playerTrickTurn;
	std::array<int16_t, 4> m_playerTricksTaken;

	CardTrick<Card, int8_t, ZPA_UNSET_CARD> m_currentTrick;

private:
	SpadesMatch(const SpadesMatch&) = delete;
	SpadesMatch operator=(const SpadesMatch&) = delete;
};

}
