#include "CheckersMatch.hpp"

#include "PlayerSocket.hpp"
#include "..\Resource.h"

namespace WinXP {

#define XPCheckersInvertSeat(seat) (seat == 0 ? 1 : 0)
#define XPCheckersIsSeatHost(seat) (seat == 0)

CheckersMatch::CheckersMatch(unsigned int index, PlayerSocket& player) :
	Match(index, player),
	m_matchState(MatchState::AWAITING_START),
	m_playersCheckedIn({ false, false }),
	m_playerCheckInMsgs(),
	m_playerTurn(0),
	m_drawOfferedBy(-1),
	m_drawAccepted(false),
	m_playerResigned(-1)
{}

void
CheckersMatch::Reset()
{
	m_playersCheckedIn = { false, false };
	m_playerCheckInMsgs = {};
	m_playerTurn = 0;
	m_drawOfferedBy = -1;
	m_drawAccepted = false;
	m_playerResigned = -1;
}


void
CheckersMatch::ProcessIncomingGameMessageImpl(PlayerSocket& player, uint32 type)
{
	using namespace Checkers;

	switch (m_matchState)
	{
		case MatchState::AWAITING_START:
		{
			if (m_playersCheckedIn[player.m_seat])
				break;

			MsgCheckIn msgCheckIn = player.OnMatchReadGameMessage<MsgCheckIn, MessageCheckIn>();
			if (msgCheckIn.protocolSignature != XPCheckersProtocolSignature)
				throw std::runtime_error("Checkers::MsgCheckIn: Invalid protocol signature!");
			if (msgCheckIn.protocolVersion != (player.IsWinME() ? MECheckersProtocolVersion : XPCheckersProtocolVersion))
				throw std::runtime_error("Checkers::MsgCheckIn: Incorrect protocol version!");
			if (msgCheckIn.clientVersion != (player.IsWinME() ? MECheckersClientVersion : XPCheckersClientVersion))
				throw std::runtime_error("Checkers::MsgCheckIn: Incorrect client version!");
			// msgCheckIn.playerID should be undefined
			if (msgCheckIn.seat != player.m_seat)
				throw std::runtime_error("Checkers::MsgCheckIn: Incorrect player seat!");

			msgCheckIn.playerID = player.GetID();

			m_playersCheckedIn[player.m_seat] = true;
			m_playerCheckInMsgs[player.m_seat] = std::move(msgCheckIn);
			if (m_playersCheckedIn[0] && m_playersCheckedIn[1])
			{
				for (const MsgCheckIn& msg : m_playerCheckInMsgs)
					BroadcastGameMessage<MessageCheckIn>(msg);
				m_playerCheckInMsgs = {};
				m_matchState = MatchState::PLAYING;
			}
			return;
		}
		case MatchState::PLAYING:
		{
			switch (type)
			{
				case MessageMovePiece:
				{
					if (m_drawAccepted)
						throw std::runtime_error("Checkers::MsgMovePiece: Draw was accepted!");
					if (m_drawOfferedBy != -1)
						throw std::runtime_error("Checkers::MsgMovePiece: Draw was offered!");
					if (m_playerResigned != -1)
						throw std::runtime_error("Checkers::MsgMovePiece: Player has resigned!");
					if (player.m_seat != m_playerTurn)
						throw std::runtime_error("Checkers::MsgMovePiece: Not this player's turn!");

					MsgMovePiece msgMovePiece = player.OnMatchReadGameMessage<MsgMovePiece, MessageMovePiece>();
					if (msgMovePiece.seat != player.m_seat)
						throw std::runtime_error("Checkers::MsgMovePiece: Incorrect player seat!");

					BroadcastGameMessage<MessageMovePiece>(msgMovePiece, player.m_seat);
					return;
				}
				case MessageFinishMove:
				{
					if (m_drawAccepted)
						throw std::runtime_error("Checkers::MsgFinishMove: Draw was accepted!");
					if (m_playerResigned != -1)
						throw std::runtime_error("Checkers::MsgFinishMove: Player has resigned!");

					MsgFinishMove msgFinishMove = player.OnMatchReadGameMessage<MsgFinishMove, MessageFinishMove>();
					if (msgFinishMove.seat != player.m_seat)
						throw std::runtime_error("Checkers::MsgFinishMove: Incorrect player seat!");
					if (msgFinishMove.drawSeat != -1)
					{
						if (m_drawOfferedBy != -1)
							throw std::runtime_error("Checkers::MsgFinishMove: Draw already offered!");
						if (msgFinishMove.drawSeat != player.m_seat)
							throw std::runtime_error("Checkers::MsgFinishMove: Incorrect draw player seat!");
						
						m_drawOfferedBy = player.m_seat;
					}

					m_playerTurn = XPCheckersInvertSeat(m_playerTurn);

					BroadcastGameMessage<MessageFinishMove>(msgFinishMove, player.m_seat);
					return;
				}
				case MessageDrawResponse:
				{
					if (m_drawAccepted)
						throw std::runtime_error("Checkers::MsgDrawResponse: Draw was accepted!");
					if (m_drawOfferedBy == -1)
						throw std::runtime_error("Checkers::MsgDrawResponse: No draw was offered!");
					if (m_playerResigned != -1)
						throw std::runtime_error("Checkers::MsgDrawResponse: Player has resigned!");

					MsgDrawResponse msgDrawResponse = player.OnMatchReadGameMessage<MsgDrawResponse, MessageDrawResponse>();
					if (msgDrawResponse.seat != player.m_seat)
						throw std::runtime_error("Checkers::MsgDrawResponse: Incorrect player seat!");
					if (msgDrawResponse.seat != XPCheckersInvertSeat(m_drawOfferedBy))
						throw std::runtime_error("Checkers::MsgDrawResponse: Draw was not requested from this player!");

					if (msgDrawResponse.response == MsgDrawResponse::RESPONSE_ACCEPT)
						m_drawAccepted = true;
					else if (msgDrawResponse.response == MsgDrawResponse::RESPONSE_REJECT)
						m_drawOfferedBy = -1;
					else
						throw std::runtime_error("Checkers::MsgDrawResponse: Invalid response!");

					BroadcastGameMessage<MessageDrawResponse>(msgDrawResponse);
					return;
				}
				case MessageEndGame:
				{
					if (m_playerResigned != -1)
						throw std::runtime_error("Checkers::MsgEndGame: Player has resigned!");

					MsgEndGame msgEndGame = player.OnMatchReadGameMessage<MsgEndGame, MessageEndGame>();
					if (msgEndGame.seat != player.m_seat)
						throw std::runtime_error("Checkers::MsgEndGame: Incorrect player seat!");
					if (m_drawAccepted && msgEndGame.flags != MsgEndGame::FLAG_DRAW)
						throw std::runtime_error("Checkers::MsgEndGame: Match should have ended in draw!");

					if (msgEndGame.flags == MsgEndGame::FLAG_RESIGN)
						m_playerResigned = player.m_seat;

					BroadcastGameMessage<MessageEndGame>(msgEndGame, player.m_seat);
					return;
				}
				case MessageEndMatch:
				{
					if (!XPCheckersIsSeatHost(player.m_seat))
						throw std::runtime_error("Checkers::MsgEndMatch: Only the host (seat 0) can send this message!");

					MsgEndMatch msgEndMatch = player.OnMatchReadGameMessage<MsgEndMatch, MessageEndMatch>();
					if (msgEndMatch.seatLost < 0 || msgEndMatch.seatLost > 2)
						throw std::runtime_error("Checkers::MsgEndMatch: Invalid lost seat!");
					if (m_drawAccepted && (msgEndMatch.reason != MsgEndMatch::REASON_GAMEOVER || msgEndMatch.seatLost != 2))
						throw std::runtime_error("Checkers::MsgEndMatch: Match should have ended in draw!");
					if (m_playerResigned != -1 && (msgEndMatch.reason != MsgEndMatch::REASON_GAMEOVER || msgEndMatch.seatLost != m_playerResigned))
						throw std::runtime_error("Checkers::MsgEndMatch: Match should have ended in resign of player " + std::to_string(m_playerResigned) + "!");

					m_matchState = MatchState::ENDED;
					m_state = STATE_GAMEOVER;

					Reset();
					return;
				}
			}
			break;
		}
		case MatchState::ENDED:
		{
			if (type == MessageCheckIn)
			{
				MsgCheckIn msgCheckIn = player.OnMatchReadGameMessage<MsgCheckIn, MessageCheckIn>();
				if (player.IsWinME())
				{
					// msgCheckIn.protocolSignature should be undefined
					// msgCheckIn.protocolVersion should be undefined
				}
				else
				{
					if (msgCheckIn.protocolSignature != XPCheckersProtocolSignature)
						throw std::runtime_error("Checkers::MsgCheckIn: Invalid protocol signature!");
					if (msgCheckIn.protocolVersion != XPCheckersProtocolVersion)
						throw std::runtime_error("Checkers::MsgCheckIn: Incorrect protocol version!");
				}
				// msgCheckIn.playerID should be undefined
				if (msgCheckIn.seat != player.m_seat)
					throw std::runtime_error("Checkers::MsgCheckIn: Incorrect player seat!");

				MsgNewGameVote msgNewGameVote;
				msgNewGameVote.seat = player.m_seat;
				BroadcastGameMessage<MessageNewGameVote>(msgNewGameVote);

				if (player.IsWinME())
				{
					msgCheckIn.protocolSignature = XPCheckersProtocolSignature;
					msgCheckIn.protocolVersion = MECheckersProtocolVersion;
				}
				msgCheckIn.playerID = player.GetID();
				BroadcastGameMessage<MessageCheckIn>(msgCheckIn);

				m_playersCheckedIn[player.m_seat] = true;
				if (m_playersCheckedIn[0] && m_playersCheckedIn[1])
				{
					m_matchState = MatchState::PLAYING;
					m_state = STATE_PLAYING;
				}
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
				player.OnMatchReadGameMessage<MsgChatMessage, MessageChatMessage, char, 128>();
			if (msgChat.first.seat != player.m_seat)
				throw std::runtime_error("Checkers::MsgChatMessage: Incorrect player seat!");

			// userID may be 0, if chat message was sent before client check-in completed
			if (msgChat.first.userID == 0)
				msgChat.first.userID = player.GetID();
			else if (msgChat.first.userID != player.GetID())
				throw std::runtime_error("Checkers::MsgChatMessage: Incorrect user ID!");

			const WCHAR* chatMsgRaw = reinterpret_cast<const WCHAR*>(msgChat.second.raw);
			const size_t chatMsgLen = msgChat.second.GetLength() / sizeof(WCHAR) - 1;
			if (chatMsgLen <= 1)
				throw std::runtime_error("Checkers::MsgChatMessage: Empty chat message!");
			if (!player.IsWinME() && chatMsgRaw[chatMsgLen - 1] != L'\0')
				throw std::runtime_error("Checkers::MsgChatMessage: Non-null-terminated chat message!");

			const std::wstring chatMsg(chatMsgRaw, chatMsgLen);
			const uint8_t msgID = ValidateChatMessage(chatMsg, 40, 43);
			if (!msgID)
				throw std::runtime_error("Checkers::MsgChatMessage: Invalid chat message!");


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
			throw std::runtime_error("CheckersMatch::ProcessIncomingGameMessageImpl(): Game message of unknown type received: " + std::to_string(type));
	}
}

}
