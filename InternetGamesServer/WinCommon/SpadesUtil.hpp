#pragma once

#include <algorithm>
#include <array>
#include <cassert>
#include <random>
#include <vector>

namespace Spades {

#define SpadesNumPlayers 4
#define SpadesNumCardsInHand 13

#define SpadesUnsetCard 0x7F

static const std::uniform_int_distribution<> s_playerDistribution(0, 3);

enum class CardSuit
{
	DIAMONDS = 0,
	CLUBS = 1,
	HEARTS = 2,
	SPADES = 3
};

template<typename C, typename P, C UnsetVal>
class CardTrick final
{
	using IsCardValidFunc = bool (*)(C);
	using GetCardSuitFunc = CardSuit (*)(C);
	using GetCardRankFunc = uint8_t (*)(C);

public:
	CardTrick(IsCardValidFunc isCardValidFunc,
			GetCardSuitFunc getCardSuitFunc,
			GetCardRankFunc getCardRankFunc) :
		m_leadCard(),
		m_playerCards(),
		m_isCardValidFunc(isCardValidFunc),
		m_getCardSuitFunc(getCardSuitFunc),
		m_getCardRankFunc(getCardRankFunc)
	{
		Reset();
	}

	inline bool IsEmpty() const { return m_leadCard == UnsetVal; }

	void Reset()
	{
		m_leadCard = UnsetVal;
		m_playerCards = { UnsetVal, UnsetVal, UnsetVal, UnsetVal };
	}
	void Set(P player, C card)
	{
		assert(!IsFinished());

		if (IsEmpty())
			m_leadCard = card;
		m_playerCards[player] = card;
	}

	bool FollowsSuit(C card, const std::vector<C>& hand) const
	{
		if (m_leadCard == UnsetVal)
			return true;

		const CardSuit leadSuit = m_getCardSuitFunc(m_leadCard);
		if (m_getCardSuitFunc(card) == leadSuit)
			return true;

		return std::none_of(hand.begin(), hand.end(),
			[this, leadSuit](C pCard)
			{
				return m_getCardSuitFunc(pCard) == leadSuit;
			});
	}

	bool IsFinished() const
	{
		if (std::none_of(m_playerCards.begin(), m_playerCards.end(), [](C card) { return card == UnsetVal; }))
		{
			assert(std::all_of(m_playerCards.begin(), m_playerCards.end(), m_isCardValidFunc));
			return true;
		}
		return false;
	}
	P GetWinner() const
	{
		const bool hasSpades = std::any_of(m_playerCards.begin(), m_playerCards.end(),
			[this](C card) {
				return m_getCardSuitFunc(card) == CardSuit::SPADES;
			});
		const CardSuit targetSuit = hasSpades ? CardSuit::SPADES : m_getCardSuitFunc(m_leadCard);

		uint8_t maxRank = 0;
		P maxRankPlayer = -1;
		for (P i = 0; i < SpadesNumPlayers; ++i)
		{
			const C card = m_playerCards[i];
			if (m_getCardSuitFunc(card) == targetSuit)
			{
				const uint8_t rank = m_getCardRankFunc(card);
				if (rank >= maxRank)
				{
					maxRank = rank;
					maxRankPlayer = i;
				}
			}
		}
		assert(maxRankPlayer >= 0);
		return maxRankPlayer;
	}

	// Not directly related to a card trick.
	// Is a member of this class so it can leverage types and functions.
	int8_t GetAutoBid(const std::vector<C>& hand) const
	{
		// TODO!
		return 4;
	}
	C GetAutoCard(const std::vector<C>& hand, bool spadesBroken) const
	{
		// TODO! This is temporary/unfinished
		for (C card : hand)
		{
			if (!spadesBroken && m_leadCard == UnsetVal && m_getCardSuitFunc(card) == CardSuit::SPADES)
				continue;

			if (FollowsSuit(card, hand))
				return card;
		}
		return 0;
	}

private:
	C m_leadCard;
	std::array<C, SpadesNumPlayers> m_playerCards;

	IsCardValidFunc m_isCardValidFunc;
	GetCardSuitFunc m_getCardSuitFunc;
	GetCardRankFunc m_getCardRankFunc;
};


struct TrickScore final
{
	int16_t points;
	int16_t bags;

	// How points add up:
	int16_t pointsBase = 0;
	int16_t pointsNil = 0;
	int16_t pointsBagBonus = 0;
	int16_t pointsBagPenalty = 0;
};
std::array<TrickScore, 2> CalculateTrickScore(const std::array<int8_t, 4>& playerBids,
	const std::array<int16_t, 4>& playerTricksTaken,
	const std::array<int16_t, 2>& teamBags,
	const int8_t doubleNilBid,
	bool countNilOvertricks);

}
