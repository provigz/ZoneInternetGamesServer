#include "SpadesMatch.hpp"

#include "PlayerSocket.hpp"
#include "Protocol/Spades.hpp"
#include "..\Resource.h"

static Spades::CardSuit GetXPCardValueSuit(int8_t value)
{
	return static_cast<Spades::CardSuit>(value / SpadesNumCardsInHand);
}

static uint8_t GetXPCardValueRank(int8_t value)
{
	return static_cast<uint8_t>(value % SpadesNumCardsInHand);
}

static bool IsValidXPCardValue(int8_t value)
{
	return value >= 0 && value < SpadesNumCardsInHand * SpadesNumSuits;
}

namespace WinXP {

static const std::uniform_int_distribution<> s_playerDistribution(0, SpadesNumPlayers - 1);

SpadesMatch::SpadesMatch(unsigned int index, PlayerSocket& player) :
	Match(index, player),
	m_matchState(),
	m_playersCheckedIn({}),
	m_playerCards(),
	m_teamPoints(),
	m_teamBags(),
	m_handDealer(),
	m_nextBidPlayer(),
	m_playerBids(),
	m_spadesBroken(),
	m_playerTurn(),
	m_playerTrickTurn(),
	m_playerTricksTaken(),
	m_currentTrick(IsValidXPCardValue, GetXPCardValueSuit, GetXPCardValueRank)
{
	Reset();
	m_matchState = MatchState::INITIALIZING;
}


void
SpadesMatch::Reset()
{
	m_teamPoints = { 0, 0 };
	m_teamBags = { 0, 0 };
	m_handDealer = s_playerDistribution(g_rng);
	ResetHand();
}

void
SpadesMatch::ResetHand()
{
	m_matchState = MatchState::BIDDING;

	if (++m_handDealer >= 4)
		m_handDealer = 0;
	m_nextBidPlayer = m_handDealer;

	m_spadesBroken = false;
	m_playerBids = { BID_HAND_START, BID_HAND_START, BID_HAND_START, BID_HAND_START };
	if ((m_playerTurn = m_handDealer + 1) >= 4)
		m_playerTurn = 0;
	m_playerTrickTurn = m_playerTurn;
	m_playerTricksTaken = { 0, 0, 0, 0 };
	m_currentTrick.Reset();

	std::array<Card, SpadesNumCardsInHand * 4> allCards;
	for (Card i = 0; i < allCards.size(); ++i)
		allCards[i] = i;

	std::shuffle(allCards.begin(), allCards.end(), g_rng);

	for (size_t i = 0; i < m_playerCards.size(); ++i)
		m_playerCards[i] = CardArray(allCards.begin() + i * SpadesNumCardsInHand,
			allCards.begin() + (i + 1) * SpadesNumCardsInHand);
}


void
SpadesMatch::RegisterCheckIn(int16 seat)
{
	using namespace Spades;

	assert(m_matchState == MatchState::INITIALIZING ||
		m_matchState == MatchState::ENDED);

	m_playersCheckedIn[seat] = true;
	if (ARRAY_EACH_TRUE(m_playersCheckedIn))
	{
		m_playersCheckedIn = {};
		m_state = STATE_PLAYING;

		Reset();

		MsgStartGame msgStartGame;
		for (PlayerSocket* p : m_players)
		{
			msgStartGame.playerIDs[p->m_seat] = p->GetID();
		}
		if (m_players.size() < SpadesNumPlayers)
		{
			for (int16 seat = 0; seat < SpadesNumPlayers; ++seat)
			{
				if (m_playerSeatsComputer[seat])
					msgStartGame.playerIDs[seat] = GetComputerPlayerID(seat);
			}
		}
		BroadcastGameMessage<MessageStartGame>(msgStartGame);

		MsgStartBid msgStartBid;
		msgStartBid.dealer = m_handDealer;
		for (PlayerSocket* player : m_players)
		{
			const std::vector<Card>& cards = m_playerCards.at(player->m_seat);
			for (BYTE y = 0; y < SpadesNumCardsInHand; ++y)
				msgStartBid.hand[y] = cards[y];

			player->OnMatchGameMessage<MessageStartBid>(msgStartBid);
		}

		// Bid on behalf of computer player
		if (m_playerSeatsComputer[m_handDealer])
			ProcessBid(m_handDealer, m_currentTrick.GetAutoBid(m_playerCards[m_handDealer]));
	}
}

void
SpadesMatch::ProcessBid(int16 seat, int8_t bid)
{
	using namespace Spades;

	assert(m_matchState == MatchState::BIDDING);

	bool moreBidsToSend = m_nextBidPlayer != m_handDealer ||
		std::all_of(m_playerBids.begin(), m_playerBids.end(),
			[](char bid)
			{
				return bid == BID_SHOWN_CARDS || bid == BID_HAND_START;
			});
	if (!moreBidsToSend)
	{
		MsgStartPlay msgStartPlay;
		msgStartPlay.leader = m_playerTurn;
		BroadcastGameMessage<MessageStartPlay>(msgStartPlay);

		m_matchState = MatchState::PLAYING;

		// Play card on behalf of computer player
		if (m_playerSeatsComputer[m_playerTurn])
			ProcessPlayCard(m_playerTurn,
				m_currentTrick.GetAutoCard<MsgBid::BID_DOUBLE_NIL>(m_playerCards[m_playerTurn], m_playerBids[m_playerTurn], m_spadesBroken));
		return;
	}

	m_playerBids[seat] = bid;

	MsgBid msgBid;
	while (moreBidsToSend &&
		m_playerBids[m_nextBidPlayer] != BID_SHOWN_CARDS &&
		m_playerBids[m_nextBidPlayer] != BID_HAND_START)
	{
		msgBid.seat = m_nextBidPlayer;
		msgBid.bid = m_playerBids[m_nextBidPlayer];

		if (++m_nextBidPlayer >= 4)
			m_nextBidPlayer = 0;

		msgBid.nextBidder = m_nextBidPlayer;
		BroadcastGameMessage<MessageBid>(msgBid);

		moreBidsToSend = m_nextBidPlayer != m_handDealer;
	}

	// Bid on behalf of computer player
	if (moreBidsToSend && m_playerSeatsComputer[m_nextBidPlayer])
	{
		ProcessBid(m_nextBidPlayer, m_currentTrick.GetAutoBid(m_playerCards[m_nextBidPlayer]));
		return;
	}
	if (!moreBidsToSend && m_playerSeatsComputer[m_handDealer])
	{
		MsgStartPlay msgStartPlay;
		msgStartPlay.leader = m_playerTurn;
		BroadcastGameMessage<MessageStartPlay>(msgStartPlay);

		m_matchState = MatchState::PLAYING;

		// Play card on behalf of computer player
		if (m_playerSeatsComputer[m_playerTurn])
			ProcessPlayCard(m_playerTurn,
				m_currentTrick.GetAutoCard<MsgBid::BID_DOUBLE_NIL>(m_playerCards[m_playerTurn], m_playerBids[m_playerTurn], m_spadesBroken));
	}
}

void
SpadesMatch::ProcessPlayCard(int16 seat, Card card)
{
	using namespace Spades;

	assert(m_matchState == MatchState::PLAYING);

	CardArray& cards = m_playerCards[seat];

	assert(!(!m_spadesBroken && m_currentTrick.IsEmpty() &&
		GetXPCardValueSuit(card) == CardSuit::SPADES &&
		std::any_of(cards.begin(), cards.end(), [](Card c) { return GetXPCardValueSuit(c) != CardSuit::SPADES; })));
	assert(m_currentTrick.FollowsSuit(card, cards));

	cards.erase(std::remove(cards.begin(), cards.end(), card), cards.end());

	if (!m_spadesBroken && !m_currentTrick.IsEmpty() && GetXPCardValueSuit(card) == CardSuit::SPADES)
		m_spadesBroken = true;

	m_currentTrick.Set(seat, card);
	if (++m_playerTrickTurn >= 4)
		m_playerTrickTurn = 0;

	MsgPlay msgPlay;
	msgPlay.seat = seat;
	msgPlay.nextPlayer = m_playerTrickTurn;
	msgPlay.card = card;
	BroadcastGameMessage<MessagePlay>(msgPlay);

	if (m_currentTrick.IsFinished())
	{
		m_playerTurn = m_currentTrick.GetWinner();
		++m_playerTricksTaken[m_playerTurn];

		if (cards.empty())
		{
			const std::array<TrickScore, 2> score =
				CalculateTrickScore(m_playerBids, m_playerTricksTaken, m_teamBags, MsgBid::BID_DOUBLE_NIL, false);
			m_teamPoints[0] += score[0].points;
			m_teamPoints[1] += score[1].points;
			m_teamBags[0] = score[0].bags;
			m_teamBags[1] = score[1].bags;

			MsgEndHand msgEndHand;
			msgEndHand.points[0] = score[0].points;
			msgEndHand.points[1] = score[1].points;
			msgEndHand.bags[0] = score[0].bags;
			msgEndHand.bags[1] = score[1].bags;
			msgEndHand.pointsBase[0] = score[0].pointsBase;
			msgEndHand.pointsBase[1] = score[1].pointsBase;
			msgEndHand.pointsNil[0] = score[0].pointsNil;
			msgEndHand.pointsNil[1] = score[1].pointsNil;
			msgEndHand.pointsBagBonus[0] = score[0].pointsBagBonus;
			msgEndHand.pointsBagBonus[1] = score[1].pointsBagBonus;
			msgEndHand.pointsBagPenalty[0] = score[0].pointsBagPenalty;
			msgEndHand.pointsBagPenalty[1] = score[1].pointsBagPenalty;
			BroadcastGameMessage<MessageEndHand>(msgEndHand);

			ResetHand();

			bool team1Winner = m_teamPoints[0] >= 500 || m_teamPoints[1] <= -200;
			bool team2Winner = m_teamPoints[1] >= 500 || m_teamPoints[0] <= -200;
			if (team1Winner || team2Winner)
			{
				if (team1Winner && team2Winner)
					team1Winner = m_teamPoints[0] > m_teamPoints[1];

				MsgEndGame msgEndGame;
				msgEndGame.winners[team1Winner ? 0 : 1] = 1;
				msgEndGame.winners[team1Winner ? 2 : 3] = 1;
				BroadcastGameMessage<MessageEndGame>(msgEndGame);

				m_matchState = MatchState::ENDED;
				m_state = STATE_GAMEOVER;

				// Request new game on behalf of computer players
				if (m_players.size() < SpadesNumPlayers)
				{
					MsgNewGameVote msgNewGameVote;
					for (int16 seat = 0; seat < SpadesNumPlayers; ++seat)
					{
						if (m_playerSeatsComputer[seat])
						{
							msgNewGameVote.seat = seat;
							BroadcastGameMessage<MessageNewGameVote>(msgNewGameVote);

							m_playersCheckedIn[seat] = true;
						}
					}
				}
			}
			else
			{
				MsgStartBid msgStartBid;
				msgStartBid.dealer = m_handDealer;
				for (PlayerSocket* player : m_players)
				{
					const std::vector<Card>& cards = m_playerCards.at(player->m_seat);
					for (BYTE y = 0; y < SpadesNumCardsInHand; ++y)
						msgStartBid.hand[y] = cards[y];

					player->OnMatchGameMessage<MessageStartBid>(msgStartBid);
				}

				m_matchState = MatchState::BIDDING;

				// Bid on behalf of computer player
				if (m_playerSeatsComputer[m_handDealer])
					ProcessBid(m_handDealer, m_currentTrick.GetAutoBid(m_playerCards[m_handDealer]));
			}
			return;
		}
		else
		{
			m_playerTrickTurn = m_playerTurn;
			m_currentTrick.Reset();
		}
	}

	// Play card on behalf of computer player
	if (m_playerSeatsComputer[m_playerTrickTurn])
		ProcessPlayCard(m_playerTrickTurn,
			m_currentTrick.GetAutoCard<MsgBid::BID_DOUBLE_NIL>(m_playerCards[m_playerTrickTurn], m_playerBids[m_playerTrickTurn], m_spadesBroken));
}


void
SpadesMatch::ProcessIncomingGameMessageImpl(PlayerSocket& player, uint32 type)
{
	using namespace Spades;

	switch (m_matchState)
	{
		case MatchState::INITIALIZING:
		{
			if (m_playersCheckedIn[player.m_seat])
				break;

			const MsgCheckIn msgCheckIn = player.OnMatchAwaitGameMessage<MsgCheckIn, MessageCheckIn>();
			if (msgCheckIn.protocolSignature != XPSpadesProtocolSignature)
				throw std::runtime_error("Spades::MsgCheckIn: Invalid protocol signature!");
			if (msgCheckIn.protocolVersion != XPSpadesProtocolVersion)
				throw std::runtime_error("Spades::MsgCheckIn: Incorrect protocol version!");
			if (msgCheckIn.clientVersion != XPSpadesClientVersion)
				throw std::runtime_error("Spades::MsgCheckIn: Incorrect client version!");
			if (msgCheckIn.playerID != player.GetID())
				throw std::runtime_error("Spades::MsgCheckIn: Incorrect player ID!");
			if (msgCheckIn.seat != player.m_seat)
				throw std::runtime_error("Spades::MsgCheckIn: Incorrect player seat!");

			RegisterCheckIn(player.m_seat);
			return;
		}
		case MatchState::BIDDING:
		{
			switch (type)
			{
				case MessageShowCards:
				{
					const MsgShowCards msgShowCards = player.OnMatchAwaitGameMessage<MsgShowCards, MessageShowCards>();
					if (msgShowCards.seat != player.m_seat)
						throw std::runtime_error("Spades::MsgShowCards: Incorrect player seat!");

					if (m_playerBids[player.m_seat] != BID_HAND_START)
						throw std::runtime_error("Spades::MessageShowCards: Not the start of a hand!");

					m_playerBids[player.m_seat] = BID_SHOWN_CARDS;
					return;
				}
				case MessageBid:
				{
					const MsgBid msgBid = player.OnMatchAwaitGameMessage<MsgBid, MessageBid>();
					if (msgBid.seat != player.m_seat)
						throw std::runtime_error("Spades::MsgBid: Incorrect player seat!");
					if ((msgBid.bid < 0 || msgBid.bid > 13) && msgBid.bid != MsgBid::BID_DOUBLE_NIL)
						throw std::runtime_error("Spades::MsgBid: Invalid bid value!");

					bool moreBidsToSend = m_nextBidPlayer != m_handDealer ||
						std::all_of(m_playerBids.begin(), m_playerBids.end(),
							[](char bid)
							{
								return bid == BID_SHOWN_CARDS || bid == BID_HAND_START;
							});
					if (!moreBidsToSend)
					{
						if (player.m_seat != m_handDealer)
							throw std::runtime_error("Spades::MsgBid: Final message is not by hand dealer!");
						if (m_playerBids[m_handDealer] != msgBid.bid)
							throw std::runtime_error("Spades::MsgBid: Final message by hand dealer does not contain the same bid!");

						MsgStartPlay msgStartPlay;
						msgStartPlay.leader = m_playerTurn;
						BroadcastGameMessage<MessageStartPlay>(msgStartPlay);

						m_matchState = MatchState::PLAYING;

						// Play card on behalf of computer player
						if (m_playerSeatsComputer[m_playerTurn])
							ProcessPlayCard(m_playerTurn,
								m_currentTrick.GetAutoCard<MsgBid::BID_DOUBLE_NIL>(m_playerCards[m_playerTurn], m_playerBids[m_playerTurn], m_spadesBroken));
						return;
					}
					else if (m_playerBids[player.m_seat] != (msgBid.bid == MsgBid::BID_DOUBLE_NIL ? BID_HAND_START : BID_SHOWN_CARDS))
					{
						throw std::runtime_error("Spades::MsgBid: Incorrect bid state!");
					}

					ProcessBid(player.m_seat, msgBid.bid);
					return;
				}
			}
			break;
		}
		case MatchState::PLAYING:
		{
			if (type == MessagePlay)
			{
				const MsgPlay msgPlay = player.OnMatchAwaitGameMessage<MsgPlay, MessagePlay>();
				if (msgPlay.seat != player.m_seat)
					throw std::runtime_error("Spades::MsgPlay: Incorrect player seat!");

				if (player.m_seat != m_playerTrickTurn)
					throw std::runtime_error("Spades::MessagePlay: Not this player's turn!");

				if (!IsValidXPCardValue(msgPlay.card))
					throw std::runtime_error("Spades::MsgPlay: Invalid card!");

				const CardArray& cards = m_playerCards[player.m_seat];
				if (std::find(cards.begin(), cards.end(), msgPlay.card) == cards.end())
					throw std::runtime_error("Spades::MessagePlay: Player does not possess provided card!");

				if (!m_spadesBroken && m_currentTrick.IsEmpty() &&
					GetXPCardValueSuit(msgPlay.card) == CardSuit::SPADES &&
					std::any_of(cards.begin(), cards.end(), [](Card c) { return GetXPCardValueSuit(c) != CardSuit::SPADES; }))
				{
					throw std::runtime_error("SpadesMatch::ProcessEvent(): \"Move\": Tried to lead a Spade before Spades are broken!");
				}

				if (!m_currentTrick.FollowsSuit(msgPlay.card, cards))
					throw std::runtime_error("Spades::MessagePlay: Card does not follow suit!");

				ProcessPlayCard(player.m_seat, msgPlay.card);
				return;
			}
			break;
		}
		case MatchState::ENDED:
		{
			if (type == MessageNewGameVote)
			{
				const MsgNewGameVote msgNewGameVote = player.OnMatchAwaitGameMessage<MsgNewGameVote, MessageNewGameVote>();
				if (msgNewGameVote.seat != player.m_seat)
					throw std::runtime_error("Spades::MsgNewGameVote: Incorrect player seat!");

				BroadcastGameMessage<MessageNewGameVote>(msgNewGameVote);

				RegisterCheckIn(player.m_seat);
				return;
			}
		}
	}

	// Miscellaneous messages
	switch (type)
	{
		case MessageChatMessage:
		{
			std::pair<MsgChatMessage, Array<char, 128>> msgChat =
				player.OnMatchAwaitGameMessage<MsgChatMessage, MessageChatMessage, char, 128>();
			if (msgChat.first.userID != player.GetID())
				throw std::runtime_error("Spades::MsgChatMessage: Incorrect user ID!");

			const WCHAR* chatMsgRaw = reinterpret_cast<const WCHAR*>(msgChat.second.raw);
			const size_t chatMsgLen = msgChat.second.GetLength() / sizeof(WCHAR) - 1;
			if (chatMsgLen <= 1)
				throw std::runtime_error("Spades::MsgChatMessage: Empty chat message!");
			if (!player.IsWinME() && chatMsgRaw[chatMsgLen - 1] != L'\0')
				throw std::runtime_error("Spades::MsgChatMessage: Non-null-terminated chat message!");

			const std::wstring chatMsg(chatMsgRaw, chatMsgLen);
			const uint8_t msgID = ValidateChatMessage(chatMsg, 50, 54);
			if (!msgID)
				throw std::runtime_error("Spades::MsgChatMessage: Invalid chat message!");


			constexpr int extraMsgChars = 2; // +1 for null-separator, +1 for ID character at the end (read below)
			msgChat.second = {};

			// Windows ME versions of these games directly utilize the provided message string.
			// A good compromise between safety and support is to send a ready message, based on its ID.
			// Drawback of this is that no chat language other than English would be supported for ME.
			const int msgLenW = LoadStringW(GetModuleHandle(NULL), IDS_XPCHAT_BEGIN + msgID,
				reinterpret_cast<LPWSTR>(msgChat.second.raw),
				static_cast<int>((msgChat.second.GetSize() - sizeof(WCHAR) * extraMsgChars) / sizeof(WCHAR)));
			if (!msgLenW)
				throw std::runtime_error("Backgammon::MessageChatMessage: Failed to load message string from resource!");

			// Windows XP versions initially check for a wide character at the end of the message string, which is equal to the message ID.
			// Since the end character and its ID will be prioritized over the actual string we provide,
			// we should append it to the string to also send it over, so that at least XP can have multi-language support.
			msgChat.second[(msgLenW + 1) * sizeof(WCHAR)] = msgID; // +1 because there should be a null-separator between the string and the ID

			const int msgLen = (msgLenW + extraMsgChars) * sizeof(WCHAR);
			msgChat.second.SetLength(msgLen);
			msgChat.first.messageLength = static_cast<uint16>(msgLen);
			BroadcastGameMessage<MessageChatMessage>(msgChat.first, msgChat.second);
			break;
		}
		default:
			throw std::runtime_error("SpadesMatch::ProcessIncomingGameMessageImpl(): Game message of unknown type received: " + std::to_string(type));
	}
}


void
SpadesMatch::OnReplacePlayer(const PlayerSocket& player, uint32 userIDNew)
{
	using namespace Spades;

	MsgReplacePlayer msgReplacePlayer;
	msgReplacePlayer.userIDNew = userIDNew;
	msgReplacePlayer.seat = player.m_seat;
	BroadcastGameMessage<MessageReplacePlayer>(msgReplacePlayer);

	// If needed, interact with the match on behalf of the new computer player
	switch (m_matchState)
	{
		case MatchState::INITIALIZING:
		{
			RegisterCheckIn(player.m_seat);
			break;
		}
		case MatchState::BIDDING:
		{
			if (m_nextBidPlayer == player.m_seat)
				ProcessBid(player.m_seat, m_currentTrick.GetAutoBid(m_playerCards[player.m_seat]));
			break;
		}
		case MatchState::PLAYING:
		{
			if (m_playerTrickTurn == player.m_seat)
				ProcessPlayCard(player.m_seat,
					m_currentTrick.GetAutoCard<MsgBid::BID_DOUBLE_NIL>(m_playerCards[player.m_seat], m_playerBids[player.m_seat], m_spadesBroken));
			break;
		}
		case MatchState::ENDED:
		{
			MsgNewGameVote msgNewGameVote;
			msgNewGameVote.seat = player.m_seat;
			BroadcastGameMessage<MessageNewGameVote>(msgNewGameVote);

			RegisterCheckIn(player.m_seat);
			break;
		}
	}
}

}
