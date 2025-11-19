#include "HeartsMatch.hpp"

#include "PlayerSocket.hpp"
#include "Protocol/Hearts.hpp"
#include "..\Resource.h"

typedef int8_t Card;

enum class CardSuit
{
	CLUBS = 0,
	DIAMONDS = 1,
	SPADES = 2,
	HEARTS = 3
};

static CardSuit GetXPCardValueSuit(Card value)
{
	return static_cast<CardSuit>(value / HeartsNumCardsInHand);
}

static uint8_t GetXPCardValueRank(Card value)
{
	return static_cast<uint8_t>(value % HeartsNumCardsInHand);
}

static bool IsValidXPCardValue(Card value)
{
	return value >= 0 && value < HeartsNumCardsInHand * 4;
}


namespace WinXP {

static std::array<Card, HeartsNumCardsInPass> HeartsGetAutoPass(const std::vector<Card>& hand)
{
	// TODO!
	return { hand[0], hand[1], hand[2] };
}


HeartsMatch::CardTrick::CardTrick() :
	m_leadCard(),
	m_playerCards()
{
	Reset();
}

bool
HeartsMatch::CardTrick::IsEmpty() const
{
	return m_leadCard == HeartsUnsetCard;
}

void
HeartsMatch::CardTrick::Reset()
{
	m_leadCard = HeartsUnsetCard;
	m_playerCards = { HeartsUnsetCard, HeartsUnsetCard, HeartsUnsetCard, HeartsUnsetCard };
}

void
HeartsMatch::CardTrick::Set(int16 player, Card card)
{
	assert(!IsFinished());

	if (IsEmpty())
		m_leadCard = card;
	m_playerCards[player] = card;
}

bool
HeartsMatch::CardTrick::FollowsSuit(Card card, const CardArray& hand) const
{
	if (m_leadCard == HeartsUnsetCard)
		return true;

	const CardSuit leadSuit = GetXPCardValueSuit(m_leadCard);
	if (GetXPCardValueSuit(card) == leadSuit)
		return true;

	return std::none_of(hand.begin(), hand.end(),
		[this, leadSuit](Card pCard)
		{
			return GetXPCardValueSuit(pCard) == leadSuit;
		});
}

bool
HeartsMatch::CardTrick::IsFinished() const
{
	if (std::none_of(m_playerCards.begin(), m_playerCards.end(), [](Card card) { return card == HeartsUnsetCard; }))
	{
		assert(std::all_of(m_playerCards.begin(), m_playerCards.end(), IsValidXPCardValue));
		return true;
	}
	return false;
}

int16
HeartsMatch::CardTrick::GetWinner() const
{
	const CardSuit targetSuit = GetXPCardValueSuit(m_leadCard);

	uint8_t maxRank = 0;
	int16 maxRankPlayer = -1;
	for (int16 i = 0; i < HeartsNumPlayers; ++i)
	{
		const Card card = m_playerCards[i];
		if (GetXPCardValueSuit(card) == targetSuit)
		{
			const uint8_t rank = GetXPCardValueRank(card);
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

int16
HeartsMatch::CardTrick::GetPoints() const
{
	int16 points = 0;
	for (Card card : m_playerCards)
	{
		if (card == HeartsCardQS)
			points += 13;
		else if (GetXPCardValueSuit(card) == CardSuit::HEARTS)
			++points;
	}
	return points;
}

Card
HeartsMatch::CardTrick::GetAutoCard(const std::vector<Card>& hand, bool pointsBroken) const
{
	// TODO! This is temporary/unfinished
	for (Card card : hand)
	{
		if (!FollowsSuit(card, hand))
			continue;

		if (m_leadCard == HeartsUnsetCard) // Playing lead card?
		{
			if (hand.size() >= HeartsNumCardsInHand) // First trick?
			{
				if (card != HeartsCard2C)
					continue;
			}
			else if (!pointsBroken && GetXPCardValueSuit(card) == CardSuit::HEARTS)
			{
				if (std::any_of(hand.begin(), hand.end(), [](Card c) { return GetXPCardValueSuit(c) != CardSuit::HEARTS; }))
					continue;
			}
		}
		else if (hand.size() >= HeartsNumCardsInHand) // Not playing lead card. First trick?
		{
			if (card == HeartsCardQS)
			{
				continue;
			}
			else if (GetXPCardValueSuit(card) == CardSuit::HEARTS)
			{
				if (std::any_of(hand.begin(), hand.end(), [](Card c) { return GetXPCardValueSuit(c) != CardSuit::HEARTS; }))
					continue;
			}
		}

		return card;
	}
	return 0;
}


HeartsMatch::HeartsMatch(unsigned int index, PlayerSocket& player) :
	Match(index, player),
	m_matchState(),
	m_playersCheckedIn({}),
	m_playerCards(),
	m_passDirection(),
	m_playersPassedCards(),
	m_playerTurn(),
	m_pointsBroken(),
	m_playerHandPoints(),
	m_playerTotalPoints(),
	m_currentTrick()
{
	Reset();
	m_matchState = MatchState::INITIALIZING;
}


void
HeartsMatch::Reset()
{
	m_matchState = MatchState::PASSING;
	m_passDirection = Hearts::MsgStartHand::PASS_NONE;
	m_playerTotalPoints = {};
	ResetHand();
}

void
HeartsMatch::ResetHand()
{
	if (++m_passDirection >= HeartsPassDirections)
		m_passDirection = 0;
	m_playersPassedCards = {};
	m_pointsBroken = false;
	m_playerHandPoints = {};
	m_currentTrick.Reset();

	std::array<Card, HeartsNumCardsInHand * 4> allCards;
	for (Card i = 0; i < allCards.size(); ++i)
		allCards[i] = i;

	std::shuffle(allCards.begin(), allCards.end(), g_rng);

	for (size_t i = 0; i < m_playerCards.size(); ++i)
		m_playerCards[i] = CardArray(allCards.begin() + i * HeartsNumCardsInHand,
			allCards.begin() + (i + 1) * HeartsNumCardsInHand);
}


void
HeartsMatch::RegisterCheckIn(int16 seat)
{
	using namespace Hearts;

	m_playersCheckedIn[seat] = true;
	if (ARRAY_EACH_TRUE(m_playersCheckedIn))
	{
		m_playersCheckedIn = {};
		m_state = STATE_PLAYING;
		m_matchState = MatchState::PASSING;

		MsgStartGame msgStartGame;
		for (PlayerSocket* p : m_players)
		{
			msgStartGame.playerIDs[p->m_seat] = p->GetID();
		}
		if (m_players.size() < HeartsNumPlayers)
		{
			for (int16 seat = 0; seat < HeartsNumPlayers; ++seat)
			{
				if (m_playerSeatsComputer[seat])
					msgStartGame.playerIDs[seat] = GetComputerPlayerID(seat);
			}
		}
		BroadcastGameMessage<MessageStartGame>(msgStartGame);

		MsgStartHand msgStartHand;
		msgStartHand.passDirection = m_passDirection;
		for (PlayerSocket* player : m_players)
		{
			const std::vector<Card>& cards = m_playerCards.at(player->m_seat);
			for (BYTE y = 0; y < HeartsNumCardsInHand; ++y)
				msgStartHand.hand[y] = cards[y];

			player->OnMatchGameMessage<MessageStartHand>(msgStartHand);
		}
	}
}

void
HeartsMatch::ProcessPass(int16 seat, std::array<Card, HeartsNumCardsInPass> passCards)
{
	using namespace Hearts;

	assert(m_matchState == MatchState::PASSING);

	const int16 passReceiver = (seat + m_passDirection) % HeartsNumPlayers;
	CardArray& cards = m_playerCards[seat];
	for (int8_t i = 0; i < HeartsNumCardsInPass; ++i)
	{
		const char card = passCards[i];
		if (!IsValidXPCardValue(card))
			throw std::runtime_error("Hearts::MsgPass: Invalid card!");
		if (std::find(cards.begin(), cards.begin() + min(cards.size(), HeartsNumCardsInHand), card) == cards.end())
			throw std::runtime_error("Hearts::MessagePass: Player does not possess provided card!");

		cards.erase(std::remove(cards.begin(), cards.end(), card), cards.end());
		m_playerCards[passReceiver].push_back(card);
	}

	MsgPass msgPass;
	msgPass.seat = seat;
	std::copy(passCards.begin(), passCards.end(), std::begin(msgPass.cards));
	BroadcastGameMessage<MessagePass>(msgPass);

	m_playersPassedCards[seat] = true;
	if (ARRAY_EACH_TRUE(m_playersPassedCards))
	{
		m_playerTurn = -1;
		for (int16 seat = 0; seat < HeartsNumPlayers; ++seat)
		{
			const CardArray& playerCards = m_playerCards[seat];
			if (std::find(playerCards.begin(), playerCards.end(), HeartsCard2C) != playerCards.end())
			{
				m_playerTurn = seat;
				break;
			}
		}
		if (m_playerTurn < 0)
			throw std::runtime_error("Hearts::MessagePass: Could not find player with card 2C!");

		MsgStartPlay msgStartPlay;
		msgStartPlay.seat = m_playerTurn;
		BroadcastGameMessage<MessageStartPlay>(msgStartPlay);

		m_matchState = MatchState::PLAYING;

		// Play card on behalf of computer player
		if (m_playerSeatsComputer[m_playerTurn])
			ProcessPlayCard(m_playerTurn, HeartsCard2C);
	}
	// Pass cards on behalf of computer players
	else if (m_players.size() < HeartsNumPlayers)
	{
		for (int16 otherSeat = 0; otherSeat < HeartsNumPlayers; ++otherSeat)
		{
			if (m_playerSeatsComputer[otherSeat] && !m_playersPassedCards[otherSeat])
			{
				ProcessPass(otherSeat, HeartsGetAutoPass(m_playerCards[otherSeat]));
				break; // Recursion has taken care of any others
			}
		}
	}
}

void
HeartsMatch::ProcessPlayCard(int16 seat, Card card)
{
	using namespace Hearts;

	assert(m_matchState == MatchState::PLAYING);

	CardArray& cards = m_playerCards[seat];

	assert(!(!m_pointsBroken && m_currentTrick.IsEmpty() && GetXPCardValueSuit(card) == CardSuit::HEARTS));
	assert(m_currentTrick.FollowsSuit(card, cards));

	cards.erase(std::remove(cards.begin(), cards.end(), card), cards.end());

	if (!m_pointsBroken && (card == HeartsCardQS || GetXPCardValueSuit(card) == CardSuit::HEARTS))
		m_pointsBroken = true;

	m_currentTrick.Set(seat, card);
	if (++m_playerTurn >= 4)
		m_playerTurn = 0;

	MsgPlay msgPlay;
	msgPlay.seat = seat;
	msgPlay.card = card;
	BroadcastGameMessage<MessagePlay>(msgPlay);

	if (m_currentTrick.IsFinished())
	{
		m_playerTurn = m_currentTrick.GetWinner();
		m_playerHandPoints[m_playerTurn] += m_currentTrick.GetPoints();

		if (cards.empty())
		{
			// "Shooting the Moon" strategy: If a player has collected all points from a hand,
			// all other players will get them instead. Said player will be left with 0.
			for (int16 otherSeat = 0; otherSeat < HeartsNumPlayers; ++otherSeat)
			{
				if (m_playerHandPoints[otherSeat] > 0)
				{
					if (m_playerHandPoints[otherSeat] >= HeartsNumPointsInHand)
					{
						m_playerHandPoints.fill(HeartsNumPointsInHand);
						m_playerHandPoints[otherSeat] = 0;
					}
					break;
				}
			}

			for (int16 otherSeat = 0; otherSeat < HeartsNumPlayers; ++otherSeat)
				m_playerTotalPoints[otherSeat] += m_playerHandPoints[otherSeat];

			MsgEndHand msgEndHand;
			std::copy(m_playerHandPoints.begin(), m_playerHandPoints.end(), std::begin(msgEndHand.points));
			BroadcastGameMessage<MessageEndHand>(msgEndHand);

			ResetHand();

			if (std::any_of(m_playerTotalPoints.begin(), m_playerTotalPoints.end(),
				[](int16 p) { return p >= HeartsNumPointsInGame; }))
			{
				MsgEndGame msgEndGame;
				BroadcastGameMessage<MessageEndGame>(msgEndGame);

				m_matchState = MatchState::ENDED;
				m_state = STATE_GAMEOVER;

				// Request new game on behalf of computer players
				if (m_players.size() < HeartsNumPlayers)
				{
					MsgNewGameVote msgNewGameVote;
					for (int16 seat = 0; seat < HeartsNumPlayers; ++seat)
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
				MsgStartHand msgStartHand;
				msgStartHand.passDirection = m_passDirection;
				for (PlayerSocket* player : m_players)
				{
					const std::vector<Card>& cards = m_playerCards.at(player->m_seat);
					for (BYTE y = 0; y < HeartsNumCardsInHand; ++y)
						msgStartHand.hand[y] = cards[y];

					player->OnMatchGameMessage<MessageStartHand>(msgStartHand);
				}

				if (m_passDirection == MsgStartHand::PASS_NONE)
				{
					m_playerTurn = -1;
					for (int16 seat = 0; seat < HeartsNumPlayers; ++seat)
					{
						const CardArray& playerCards = m_playerCards[seat];
						if (std::find(playerCards.begin(), playerCards.end(), HeartsCard2C) != playerCards.end())
						{
							m_playerTurn = seat;
							break;
						}
					}
					if (m_playerTurn < 0)
						throw std::runtime_error("Hearts::MessagePass: Could not find player with card 2C!");

					MsgStartPlay msgStartPlay;
					msgStartPlay.seat = m_playerTurn;
					BroadcastGameMessage<MessageStartPlay>(msgStartPlay);

					m_matchState = MatchState::PLAYING;

					// Play card on behalf of computer player
					if (m_playerSeatsComputer[m_playerTurn])
						ProcessPlayCard(m_playerTurn, HeartsCard2C);
				}
				else
				{
					m_matchState = MatchState::PASSING;
				}
			}
			return;
		}
		else
		{
			m_currentTrick.Reset();
		}
	}

	// Play card on behalf of computer player
	if (m_playerSeatsComputer[m_playerTurn])
		ProcessPlayCard(m_playerTurn, m_currentTrick.GetAutoCard(m_playerCards[m_playerTurn], m_pointsBroken));
}


void
HeartsMatch::ProcessIncomingGameMessageImpl(PlayerSocket& player, uint32 type)
{
	using namespace Hearts;

	switch (m_matchState)
	{
		case MatchState::INITIALIZING:
		{
			if (m_playersCheckedIn[player.m_seat])
				break;

			const MsgCheckIn msgCheckIn = player.OnMatchAwaitGameMessage<MsgCheckIn, MessageCheckIn>();
			if (msgCheckIn.protocolSignature != XPHeartsProtocolSignature)
				throw std::runtime_error("Hearts::MsgCheckIn: Invalid protocol signature!");
			if (msgCheckIn.protocolVersion != XPHeartsProtocolVersion)
				throw std::runtime_error("Hearts::MsgCheckIn: Incorrect protocol version!");
			if (msgCheckIn.clientVersion != XPHeartsClientVersion)
				throw std::runtime_error("Hearts::MsgCheckIn: Incorrect client version!");
			if (msgCheckIn.seat != player.m_seat)
				throw std::runtime_error("Hearts::MsgCheckIn: Incorrect player seat!");

			RegisterCheckIn(player.m_seat);
			return;
		}
		case MatchState::PASSING:
		{
			if (type == MessagePass)
			{
				const MsgPass msgPass = player.OnMatchAwaitGameMessage<MsgPass, MessagePass>();
				if (msgPass.seat != player.m_seat)
					throw std::runtime_error("Hearts::MsgPass: Incorrect player seat!");

				if (m_playersPassedCards[player.m_seat])
					throw std::runtime_error("Hearts::MessagePass: Player has already passed cards!");

				std::array<Card, HeartsNumCardsInPass> passCards;
				std::copy(std::begin(msgPass.cards), std::end(msgPass.cards), passCards.begin());
				ProcessPass(player.m_seat, std::move(passCards));
				return;
			}
			break;
		}
		case MatchState::PLAYING:
		{
			if (type == MessagePlay)
			{
				const MsgPlay msgPlay = player.OnMatchAwaitGameMessage<MsgPlay, MessagePlay>();
				if (msgPlay.seat != player.m_seat)
					throw std::runtime_error("Hearts::MsgPlay: Incorrect player seat!");
				if (!IsValidXPCardValue(msgPlay.card))
					throw std::runtime_error("Hearts::MsgPlay: Invalid card!");

				if (msgPlay.seat != m_playerTurn)
					throw std::runtime_error("Hearts::MessagePlay: Not this player's turn!");

				const CardArray& cards = m_playerCards[player.m_seat];
				if (std::find(cards.begin(), cards.end(), msgPlay.card) == cards.end())
					throw std::runtime_error("Hearts::MessagePlay: Player does not possess provided card!");
				if (!m_currentTrick.FollowsSuit(msgPlay.card, cards))
					throw std::runtime_error("Hearts::MessagePlay: Card does not follow suit!");

				if (m_currentTrick.IsEmpty()) // Playing lead card?
				{
					if (cards.size() >= HeartsNumCardsInHand) // First trick?
					{
						if (msgPlay.card != HeartsCard2C)
							throw std::runtime_error("Hearts::MessagePlay: Tried to lead first trick of hand with a card that is not 2C!");
					}
					else if (!m_pointsBroken && GetXPCardValueSuit(msgPlay.card) == CardSuit::HEARTS)
					{
						if (std::any_of(cards.begin(), cards.end(), [](Card c) { return GetXPCardValueSuit(c) != CardSuit::HEARTS; }))
							throw std::runtime_error("Hearts::MessagePlay: Tried to lead trick with a Heart despite points not having been broken!");
					}
				}
				else if (cards.size() >= HeartsNumCardsInHand) // Not playing lead card. First trick?
				{
					if (msgPlay.card == HeartsCardQS)
					{
						throw std::runtime_error("Hearts::MessagePlay: Tried to play QS in first trick!");
					}
					else if (GetXPCardValueSuit(msgPlay.card) == CardSuit::HEARTS)
					{
						if (std::any_of(cards.begin(), cards.end(), [](Card c) { return GetXPCardValueSuit(c) != CardSuit::HEARTS; }))
							throw std::runtime_error("Hearts::MessagePlay: Tried to play a Heart in first trick!");
					}
				}

				ProcessPlayCard(player.m_seat, msgPlay.card);
				return;
			}
			break;
		}
		case MatchState::ENDED:
		{
			if (type == MessageNewGameVote)
			{
				MsgNewGameVote msgNewGameVote = player.OnMatchAwaitGameMessage<MsgNewGameVote, MessageNewGameVote>();
				if (msgNewGameVote.seat != player.m_seat)
					throw std::runtime_error("Hearts::MsgNewGameVote: Incorrect player seat!");

				BroadcastGameMessage<MessageNewGameVote>(msgNewGameVote);

				RegisterCheckIn(player.m_seat);
				return;
			}
			break;
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
				throw std::runtime_error("Hearts::MsgChatMessage: Incorrect user ID!");
			// msgChat.first.seat is always 0, thus invalid and will not be validated

			msgChat.first.seat = player.m_seat;

			const WCHAR* chatMsgRaw = reinterpret_cast<const WCHAR*>(msgChat.second.raw);
			const size_t chatMsgLen = msgChat.second.GetLength() / sizeof(WCHAR) - 1;
			if (chatMsgLen <= 1)
				throw std::runtime_error("Hearts::MsgChatMessage: Empty chat message!");
			if (!player.IsWinME() && chatMsgRaw[chatMsgLen - 1] != L'\0')
				throw std::runtime_error("Hearts::MsgChatMessage: Non-null-terminated chat message!");

			const std::wstring chatMsg(chatMsgRaw, chatMsgLen);
			const uint8_t msgID = ValidateChatMessage(chatMsg, 60, 62);
			if (!msgID)
				throw std::runtime_error("Hearts::MsgChatMessage: Invalid chat message!");


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
			throw std::runtime_error("HeartsMatch::ProcessIncomingGameMessageImpl(): Game message of unknown type received: " + std::to_string(type));
	}
}


void
HeartsMatch::OnReplacePlayer(const PlayerSocket& player, uint32 userIDNew)
{
	using namespace Hearts;

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
		case MatchState::PASSING:
		{
			if (!m_playersPassedCards[player.m_seat])
				ProcessPass(player.m_seat, HeartsGetAutoPass(m_playerCards[player.m_seat]));
			break;
		}
		case MatchState::PLAYING:
		{
			if (m_playerTurn == player.m_seat)
				ProcessPlayCard(player.m_seat, m_currentTrick.GetAutoCard(m_playerCards[player.m_seat], m_pointsBroken));
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
