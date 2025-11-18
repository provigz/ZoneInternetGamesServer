#include "SpadesMatch.hpp"

#include <cassert>

#include "PlayerSocket.hpp"
#include "../Util.hpp"

static inline uint16_t MakeZPACardValue(uint8_t suit, uint8_t rank)
{
	assert(suit < 4 && rank < 13);
	return (suit << 8) | rank;
}

static Spades::CardSuit GetZPACardValueSuit(uint16_t value)
{
	return static_cast<Spades::CardSuit>(value >> 8);
}

static uint8_t GetZPACardValueRank(uint16_t value)
{
	return value & 0xFF;
}

static bool IsValidZPACardValue(uint16_t value)
{
	return (value >> 8) < 4 && // Suit
		(value & 0xFF) < 13; // Rank
}


namespace Win7 {

SpadesMatch::SpadesMatch(unsigned int index, PlayerSocket& player) :
	Match(index, player),
	m_matchState(MatchState::BIDDING),
	m_teamPoints({ 0, 0 }),
	m_teamBags({ 0, 0 }),
	m_handDealer(s_playerDistribution(g_rng)),
	m_nextBidPlayer(),
	m_playerBids(),
	m_playerCards(),
	m_playerTurn(),
	m_playerTrickTurn(),
	m_playerTricksTaken(),
	m_currentTrick(IsValidZPACardValue, GetZPACardValueSuit, GetZPACardValueRank)
{
	ResetHand();
}

void
SpadesMatch::ResetHand()
{
	m_matchState = MatchState::BIDDING;

	if (++m_handDealer >= 4)
		m_handDealer = 0;
	m_nextBidPlayer = m_handDealer;

	m_playerBids = { BID_HAND_START, BID_HAND_START, BID_HAND_START, BID_HAND_START };
	if ((m_playerTurn = m_handDealer + 1) >= 4)
		m_playerTurn = 0;
	m_playerTrickTurn = m_playerTurn;
	m_playerTricksTaken = { 0, 0, 0, 0 };
	m_currentTrick.Reset();

	std::array<Card, 52> allCards;
	for (uint8_t i = 0; i < allCards.size(); ++i)
		allCards[i] = MakeZPACardValue(i / SpadesNumCardsInHand, i % SpadesNumCardsInHand);

	std::shuffle(allCards.begin(), allCards.end(), g_rng);

	for (size_t i = 0; i < m_playerCards.size(); ++i)
		m_playerCards[i] = CardArray(allCards.begin() + i * SpadesNumCardsInHand,
			allCards.begin() + (i + 1) * SpadesNumCardsInHand);
}


std::vector<SpadesMatch::QueuedEvent>
SpadesMatch::ProcessEvent(const tinyxml2::XMLElement& elEvent, const PlayerSocket& caller)
{
	if (!strcmp(elEvent.Name(), "Move"))
	{
		switch (m_matchState)
		{
			case MatchState::BIDDING:
			{
				const tinyxml2::XMLElement* elBid = elEvent.FirstChildElement("Bid");
				if (!elBid || !elBid->GetText())
					throw std::runtime_error("SpadesMatch::ProcessEvent(): \"Move\": No 'Bid' element or inner text!");

				int bid;
				try
				{
					bid = std::stoi(elBid->GetText());
				}
				catch (const std::exception& err)
				{
					throw std::runtime_error("SpadesMatch::ProcessEvent(): \"Move\": Couldn't parse bid value as integer: " + std::string(err.what()));
				}
				if (bid == BID_SHOWN_CARDS)
				{
					if (m_playerBids[caller.m_role] == BID_HAND_START)
						m_playerBids[caller.m_role] = BID_SHOWN_CARDS;

					const CardArray& cards = m_playerCards.at(caller.m_role);
					return {
						QueuedEvent(
							{},
							StateSTag::ConstructMethodMessage("GameLogic", "DealCards",
								[&cards](XMLPrinter& printer)
								{
									printer.OpenElement("Cards");
									for (Card card : cards)
										NewElementWithText(printer, "C", card);
									printer.CloseElement("Cards");
								}, true))
					};
				}
				if (bid == BID_DOUBLE_NIL || bid >= 0)
				{
					const bool bidDoubleNil = bid == BID_DOUBLE_NIL;
					if (m_playerBids[caller.m_role] != (bidDoubleNil ? BID_HAND_START : BID_SHOWN_CARDS))
						throw std::runtime_error("SpadesMatch::ProcessEvent(): \"Move\": Player is not in a proper start bid state!");

					bool moreBidsToSend = m_nextBidPlayer != m_handDealer ||
						std::all_of(m_playerBids.begin(), m_playerBids.end(), [](int bid) { return bid <= BID_SHOWN_CARDS; });

					m_playerBids[caller.m_role] = bid;

					while (moreBidsToSend && m_playerBids[m_nextBidPlayer] > BID_SHOWN_CARDS)
					{
						XMLPrinter sanitizedBidMessage;
						sanitizedBidMessage.OpenElement("Message");
						sanitizedBidMessage.OpenElement("Move");

						NewElementWithText(sanitizedBidMessage, "Role", m_nextBidPlayer);
						NewElementWithText(sanitizedBidMessage, "Bid", m_playerBids[m_nextBidPlayer]);

						sanitizedBidMessage.CloseElement("Move");
						sanitizedBidMessage.CloseElement("Message");

						for (const PlayerSocket* player : m_players)
						{
							if (player->m_role != m_nextBidPlayer)
								player->OnEventReceive(sanitizedBidMessage);
						}

						if (++m_nextBidPlayer >= 4)
							m_nextBidPlayer = 0;
						moreBidsToSend = m_nextBidPlayer != m_handDealer;
					}

					std::vector<QueuedEvent> eventQueue;
					if (bidDoubleNil)
					{
						const CardArray& cards = m_playerCards.at(caller.m_role);
						eventQueue.emplace_back(
							"",
							StateSTag::ConstructMethodMessage("GameLogic", "DealCards",
								[&cards](XMLPrinter& printer)
								{
									printer.OpenElement("Cards");
									for (Card card : cards)
										NewElementWithText(printer, "C", card);
									printer.CloseElement("Cards");
								}, true));
					}
					if (!moreBidsToSend)
					{
						m_matchState = MatchState::PLAYING;
						eventQueue.emplace_back(
							StateSTag::ConstructMethodMessage("GameLogic", "StartPlay",
								[this](XMLPrinter& printer) {
									NewElementWithText(printer, "Player", m_playerTurn);
								}, true),
							true);
					}
					return eventQueue;
				}
				throw std::runtime_error("SpadesMatch::ProcessEvent(): \"Move\": Invalid bid value!");
			}
			case MatchState::PLAYING:
			{
				if (m_playerTrickTurn != caller.m_role)
					throw std::runtime_error("SpadesMatch::ProcessEvent(): \"Move\": Not this player's turn!");

				const tinyxml2::XMLElement* elCard = elEvent.FirstChildElement("Card");
				if (!elCard)
					throw std::runtime_error("SpadesMatch::ProcessEvent(): \"Move\": No 'Card' element!");

				const tinyxml2::XMLElement* elSrc = elCard->FirstChildElement("Src");
				if (!elSrc || !elSrc->GetText())
					throw std::runtime_error("SpadesMatch::ProcessEvent(): \"Move\": No 'Src' field or inner text!");

				const tinyxml2::XMLElement* elDest = elCard->FirstChildElement("Dest");
				if (!elDest || !elDest->GetText())
					throw std::runtime_error("SpadesMatch::ProcessEvent(): \"Move\": No 'Dest' field or inner text!");

				const tinyxml2::XMLElement* elVal = elCard->FirstChildElement("Val");
				if (!elVal || !elVal->GetText())
					throw std::runtime_error("SpadesMatch::ProcessEvent(): \"Move\": No 'Val' field or inner text!");

				const auto srcSplit = StringSplit(elSrc->GetText(), ",");
				const auto destSplit = StringSplit(elDest->GetText(), ",");
				if (srcSplit.size() != 2 || srcSplit.at(1) != "0")
					throw std::runtime_error("SpadesMatch::ProcessEvent(): \"Move\": Invalid 'Src' format!");
				if (destSplit.size() != 2 || destSplit.at(1) != "0")
					throw std::runtime_error("SpadesMatch::ProcessEvent(): \"Move\": Invalid 'Dest' format!");

				int src, dest;
				try
				{
					src = std::stoi(srcSplit.at(0));
					dest = std::stoi(destSplit.at(0));
				}
				catch (const std::exception& err)
				{
					throw std::runtime_error("SpadesMatch::ProcessEvent(): \"Move\": Couldn't parse source/target value(s) as integer(s): " + std::string(err.what()));
				}
				if (src != caller.m_role)
					throw std::runtime_error("SpadesMatch::ProcessEvent(): \"Move\": Source is not this player!");
				if (dest - src != 4)
					throw std::runtime_error("SpadesMatch::ProcessEvent(): \"Move\": Invalid source/target value(s)!");

				uint16_t cardValue;
				try
				{
					cardValue = static_cast<uint16_t>(std::stoi(elVal->GetText()));
				}
				catch (const std::exception& err)
				{
					throw std::runtime_error("SpadesMatch::ProcessEvent(): \"Move\": Couldn't parse card value as integer: " + std::string(err.what()));
				}
				if (!IsValidZPACardValue(cardValue))
					throw std::runtime_error("SpadesMatch::ProcessEvent(): \"Move\": Invalid card value!");

				CardArray& cards = m_playerCards[caller.m_role];
				if (std::find(cards.begin(), cards.end(), cardValue) == cards.end())
					throw std::runtime_error("SpadesMatch::ProcessEvent(): \"Move\": Player does not possess provided card!");
				if (!m_currentTrick.FollowsSuit(cardValue, cards))
					throw std::runtime_error("SpadesMatch::ProcessEvent(): \"Move\": Card does not follow suit!");

				cards.erase(std::remove(cards.begin(), cards.end(), cardValue), cards.end());

				m_currentTrick.Set(caller.m_role, cardValue);
				if (++m_playerTrickTurn >= 4)
					m_playerTrickTurn = 0;

				XMLPrinter sanitizedMoveMessage;
				sanitizedMoveMessage.OpenElement("Message");
				sanitizedMoveMessage.OpenElement("Move");

				NewElementWithText(sanitizedMoveMessage, "Role", caller.m_role);

				sanitizedMoveMessage.OpenElement("Card");
				NewElementWithText(sanitizedMoveMessage, "Src", std::to_string(src) + ",");
				NewElementWithText(sanitizedMoveMessage, "Val", cardValue);
				sanitizedMoveMessage.CloseElement("Card");

				sanitizedMoveMessage.CloseElement("Move");
				sanitizedMoveMessage.CloseElement("Message");

				std::vector<QueuedEvent> eventQueue = {
					sanitizedMoveMessage.print()
				};
				if (m_currentTrick.IsFinished())
				{
					m_playerTurn = m_currentTrick.GetWinner();
					++m_playerTricksTaken[m_playerTurn];

					if (cards.empty())
					{
						const std::array<TrickScore, 2> score =
							CalculateTrickScore(m_playerBids, m_playerTricksTaken, m_teamBags, BID_DOUBLE_NIL, true);
						m_teamPoints[0] += score[0].points;
						m_teamPoints[1] += score[1].points;
						m_teamBags[0] = score[0].bags;
						m_teamBags[1] = score[1].bags;

						if (m_teamPoints[0] >= 500 || m_teamPoints[1] <= -200 ||
							m_teamPoints[1] >= 500 || m_teamPoints[0] <= -200)
						{
							m_state = STATE_GAMEOVER;

							eventQueue.emplace_back(
								StateSTag::ConstructMethodMessage("GameLogic", "StartEndOfGame", "", true),
								true);
							eventQueue.emplace_back(
								StateSTag::ConstructMethodMessage("GameManagement", "ServerGameOver"),
								true);
						}
						else
						{
							ResetHand();

							eventQueue.emplace_back(
								StateSTag::ConstructMethodMessage("GameLogic", "StartEndOfHand", "", true),
								true);
							eventQueue.emplace_back(
								StateSTag::ConstructMethodMessage("GameLogic", "StartBid",
									[this](XMLPrinter& printer) {
										NewElementWithText(printer, "Player", m_handDealer);
									}, true),
								true);
						}
					}
					else
					{
						m_playerTrickTurn = m_playerTurn;
						m_currentTrick.Reset();

						eventQueue.emplace_back(
							StateSTag::ConstructMethodMessage("GameLogic", "StartPlay",
								[this](XMLPrinter& printer) {
									NewElementWithText(printer, "Player", m_playerTurn);
								}, true),
							true);
					}
				}
				return eventQueue;
			}
		}
		throw std::runtime_error("SpadesMatch::ProcessEvent(): Invalid message for match state " + std::to_string(static_cast<int>(m_matchState)) + " received!");
	}
	throw std::runtime_error("SpadesMatch::ProcessEvent(): Unknown event!");
}


void
SpadesMatch::OnReplacePlayer(const PlayerSocket& player)
{
	// TODO
}


std::vector<std::string>
SpadesMatch::ConstructGameStartMessagesXML(const PlayerSocket& caller) const
{
	return {
		StateSTag::ConstructMethodMessage("GameLogic", "StartBid",
			[this](XMLPrinter& printer) {
				NewElementWithText(printer, "Player", m_handDealer);
			}, true)
	};
}


bool
SpadesMatch::IsValidChatNudgeMessage(const std::string& msg) const
{
	return (((m_matchState == MatchState::BIDDING && m_nextBidPlayer == 0) || m_playerTrickTurn == 0) &&
			msg == "1400_12345") || // Nudge (player 1)
		(((m_matchState == MatchState::BIDDING && m_nextBidPlayer == 1) || m_playerTrickTurn == 1) &&
			msg == "1400_12346") || // Nudge (player 2)
		(((m_matchState == MatchState::BIDDING && m_nextBidPlayer == 2) || m_playerTrickTurn == 2) &&
			msg == "1400_12347") || // Nudge (player 3)
		(((m_matchState == MatchState::BIDDING && m_nextBidPlayer == 3) || m_playerTrickTurn == 3) &&
			msg == "1400_12348"); // Nudge (player 4)
}

}
