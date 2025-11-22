#pragma once

#include <algorithm>
#include <array>
#include <cassert>
#include <random>
#include <vector>

namespace Spades {

#define SpadesNumPlayers 4
#define SpadesNumSuits 4
#define SpadesNumCardsInHand 13

#define SpadesUnsetCard 0x7F

static const std::uniform_int_distribution<> s_playerDistribution(0, 3);

enum class CardSuit
{
	NONE = -1,
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
		assert(hand.size() == SpadesNumCardsInHand);

		float bid = 0;

		std::array<std::vector<C>, SpadesNumSuits> cardsBySuits;
		for (C c : hand)
			cardsBySuits[static_cast<int8_t>(m_getCardSuitFunc(c))].push_back(c);

		bool hasAceOrHighSpade = false;

		for (int8_t suit = 0; suit < SpadesNumSuits; ++suit)
		{
			const auto& suitCards = cardsBySuits[suit];
			const bool isSpade = (suit == static_cast<int8_t>(CardSuit::SPADES));

			// Void/Singleton only for non-Spades
			if (!isSpade)
			{
				if (suitCards.empty())
				{
					++bid;
					continue;
				}
				if (suitCards.size() == 1)
					bid += 0.5f;
			}

			// Count high cards
			for (C c : suitCards)
			{
				const uint8_t rank = m_getCardRankFunc(c);

				// Ace: always +1
				if (rank == 12)
				{
					++bid;

					// Track spade control
					if (isSpade)
						hasAceOrHighSpade = true;
					continue;
				}

				// Spades logic
				if (isSpade)
				{
					if (rank == 11 || // King
						(rank == 10 && suitCards.size() >= 3)) // Queen
					{
						++bid;
						hasAceOrHighSpade = true;
					}
				}
				else
				{
					// Non-spade K/Q logic
					if ((rank == 11 && suitCards.size() >= 2) || // King
						(rank == 10 && suitCards.size() >= 3)) // Queen
					{
						++bid;
					}
				}
			}
		}

		// Extra spade length bonus
		const size_t s = cardsBySuits[static_cast<int8_t>(CardSuit::SPADES)].size();
		if (s > 3)
			bid += static_cast<float>(s - 3) * 0.5f;

		// Safety penalty
		if (!hasAceOrHighSpade)
			--bid;

		return bid < 0 ? 0 : static_cast<int8_t>(floor(bid));
	}
	template<int8_t DoubleNilBid>
	C GetAutoCard(const std::vector<C>& hand, int8_t bid, bool spadesBroken) const
	{
		assert(!IsFinished());

		if (bid == DoubleNilBid)
			bid = 0;

		if (m_leadCard != UnsetVal) // Leading trick?
		{
			const CardSuit leadSuit = m_getCardSuitFunc(m_leadCard);
			const size_t handCountLeadSuit = std::count_if(hand.begin(), hand.end(),
				[this, leadSuit](C c) {
					return m_getCardSuitFunc(c) == leadSuit;
				});

			if (handCountLeadSuit > 0)
			{
				/* We have card of lead suit in hand, so we must follow suit */
				if (bid == 0)
				{
					// If a Spade was played in trick, play highest card from lead suit,
					// since we won't win the trick anyways, and will also get rid of a high card
					if (GetCardHighestPlayedRank(CardSuit::SPADES) != UnsetVal)
						return GetCardHighest(hand, leadSuit);

					// Otherwise, play highest card of lead suit,
					// preferrably one which is of rank below the highest in trick
					const C card = GetCardHighest(hand, leadSuit, GetCardHighestPlayedRank(leadSuit));
					return card == UnsetVal ? GetCardHighest(hand, leadSuit) : card;
				}
				else
				{
					// If a Spade was played in trick, play lowest card from lead suit,
					// effectively losing the trick but preserving Spades for other tricks
					if (GetCardHighestPlayedRank(CardSuit::SPADES) != UnsetVal)
						return GetCardLowest(hand, leadSuit);

					// Otherwise, play highest card of lead suit,
					// preferrably one which is of rank higher than or equal to the highest in the trick
					const C card = GetCardHighest(hand, leadSuit);
					return m_getCardRankFunc(card) < GetCardHighestPlayedRank(leadSuit) ? GetCardLowest(hand, leadSuit) : card;
				}
			}

			/* No card of lead suit in hand */
			const size_t handCountSpades = std::count_if(hand.begin(), hand.end(),
				[this, leadSuit](C c) {
					return m_getCardSuitFunc(c) == CardSuit::SPADES;
				});

			if (bid == 0)
			{
				if (handCountSpades > 0)
				{
					// If a Spade was played in the trick, play a lower Spade
					const uint8_t highestPlayedSpade = GetCardHighestPlayedRank(CardSuit::SPADES);
					if (highestPlayedSpade != UnsetVal)
					{
						const C card = GetCardHighest(hand, CardSuit::SPADES, highestPlayedSpade);
						if (card != UnsetVal)
							return card;
					}
				}

				// If hand has only Spades, play highest Spade
				if (handCountSpades == hand.size())
					return GetCardHighest(hand, CardSuit::SPADES);

				const C card = GetCardHighestNotOfSuit(hand, CardSuit::SPADES);
				assert(card != UnsetVal);
				return card;
			}

			if (handCountSpades > 0)
			{
				// Play highest Spade in hand, if no higher was played in trick, otherwise play lowest non-Spade
				const uint8_t highestPlayedSpade = GetCardHighestPlayedRank(CardSuit::SPADES);
				C card = GetCardHighest(hand, CardSuit::SPADES);
				if (highestPlayedSpade != UnsetVal && m_getCardRankFunc(card) < highestPlayedSpade)
				{
					card = GetCardLowestNotOfSuit(hand, CardSuit::SPADES);
					return card == UnsetVal ? GetCardLowest(hand, CardSuit::SPADES) : card;
				}
				return card;
			}

			// Play lowest card
			C card = UnsetVal;
			int8_t lowestRank = SpadesNumCardsInHand;
			for (C c : hand)
			{
				const uint8_t rank = m_getCardRankFunc(c);
				if (rank < lowestRank)
				{
					card = c;
					lowestRank = rank;
				}
			}
			assert(card != UnsetVal);
			return card;
		}
		else if (bid == 0)
		{
			const C card = GetCardLowestNotOfSuit(hand, CardSuit::SPADES);
			return card == UnsetVal ? GetCardLowest(hand, CardSuit::SPADES) : card;
		}

		// Play highest card
		CardSuit excludeSuit = CardSuit::NONE;
		if (!spadesBroken &&
			!std::all_of(hand.begin(), hand.end(),
				[this](C card) { return m_getCardSuitFunc(card) == CardSuit::SPADES; }))
		{
			excludeSuit = CardSuit::SPADES;
		}
		C card = UnsetVal;
		int8_t highestRank = -1;
		for (C c : hand)
		{
			if (excludeSuit != CardSuit::NONE && m_getCardSuitFunc(c) == excludeSuit)
				continue;

			const uint8_t rank = m_getCardRankFunc(c);
			if (rank > highestRank)
			{
				card = c;
				highestRank = rank;
			}
		}
		assert(card != UnsetVal);
		return card;
	}

private:
	C GetCardHighest(const std::vector<C>& cards, CardSuit suit, int8_t rankUnder = SpadesNumCardsInHand) const
	{
		if (cards.empty())
			return UnsetVal;

		C card = UnsetVal;
		int8_t highestRank = -1;
		for (C c : cards)
		{
			if (m_getCardSuitFunc(c) != suit)
				continue;

			const uint8_t rank = m_getCardRankFunc(c);
			if (rank < rankUnder && rank > highestRank)
			{
				card = c;
				highestRank = rank;
			}
		}
		return card;
	}
	C GetCardLowest(const std::vector<C>& cards, CardSuit suit) const
	{
		if (cards.empty())
			return UnsetVal;

		C card = UnsetVal;
		int8_t lowestRank = SpadesNumCardsInHand;
		for (C c : cards)
		{
			if (m_getCardSuitFunc(c) != suit)
				continue;

			const uint8_t rank = m_getCardRankFunc(c);
			if (rank < lowestRank)
			{
				card = c;
				lowestRank = rank;
			}
		}
		return card;
	}
	C GetCardHighestNotOfSuit(const std::vector<C>& cards, CardSuit suit) const
	{
		if (cards.empty())
			return UnsetVal;

		C card = UnsetVal;
		int8_t highestRank = -1;
		for (C c : cards)
		{
			if (m_getCardSuitFunc(c) == suit)
				continue;

			const uint8_t rank = m_getCardRankFunc(c);
			if (rank > highestRank)
			{
				card = c;
				highestRank = rank;
			}
		}
		return card;
	}
	C GetCardLowestNotOfSuit(const std::vector<C>& cards, CardSuit suit) const
	{
		if (cards.empty())
			return UnsetVal;

		C card = UnsetVal;
		int8_t lowestRank = SpadesNumCardsInHand;
		for (C c : cards)
		{
			if (m_getCardSuitFunc(c) == suit)
				continue;

			const uint8_t rank = m_getCardRankFunc(c);
			if (rank < lowestRank)
			{
				card = c;
				lowestRank = rank;
			}
		}
		return card;
	}

	uint8_t GetCardHighestPlayedRank(CardSuit suit) const
	{
		if (m_leadCard == UnsetVal)
			return -1;

		int8_t highestRank = -1;
		for (C c : m_playerCards)
		{
			if (m_getCardSuitFunc(c) != suit)
				continue;

			const uint8_t rank = m_getCardRankFunc(c);
			if (rank > highestRank)
				highestRank = rank;
		}
		return highestRank;
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
