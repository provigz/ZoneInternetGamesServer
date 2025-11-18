#pragma once

#include "../Defines.hpp"
#include "../Endian.hpp"
#include "..\WinCommon\SpadesUtil.hpp"

#include <ostream>

namespace WinXP {

namespace Spades {

#define XPSpadesProtocolSignature 0x7368766C
#define XPSpadesProtocolVersion 4
#define XPSpadesClientVersion 65536

enum
{
	MessageCheckIn = 256,
	MessageStartGame,
	MessageReplacePlayer,
	MessageStartBid,
	MessageStartPlay = 261,
	MessageEndHand,
	MessageEndGame,
	MessageBid,
	MessagePlay = 266,
	MessageNewGameVote,
	MessageChatMessage = 268,
	MessageShowCards = 279
};


struct MsgCheckIn final
{
	uint32 protocolSignature = 0;
	uint32 protocolVersion = 0;
	uint32 clientVersion = 0;
	uint32 playerID = 0;
	int16 seat = 0;

	void ConvertToHostEndian()
	{
		HOST_ENDIAN_LONG(protocolSignature)
		HOST_ENDIAN_LONG(protocolVersion)
		HOST_ENDIAN_LONG(clientVersion)
		HOST_ENDIAN_LONG(playerID)
		HOST_ENDIAN_SHORT(seat)
	}
	void ConvertToNetworkEndian()
	{
		NETWORK_ENDIAN_LONG(protocolSignature)
		NETWORK_ENDIAN_LONG(protocolVersion)
		NETWORK_ENDIAN_LONG(clientVersion)
		NETWORK_ENDIAN_LONG(playerID)
		NETWORK_ENDIAN_SHORT(seat)
	}

private:
	int16 _padding = 0;
};

struct MsgStartGame final
{
	uint32 playerIDs[SpadesNumPlayers] = {};
	uint32 gameOptions = 0;
	int16 numPointsInGame = 500;

	void ConvertToHostEndian()
	{
		for (BYTE i = 0; i < SpadesNumPlayers; ++i)
			HOST_ENDIAN_LONG(playerIDs[i])
		HOST_ENDIAN_LONG(gameOptions)
		HOST_ENDIAN_SHORT(numPointsInGame)
	}
	void ConvertToNetworkEndian()
	{
		for (BYTE i = 0; i < SpadesNumPlayers; ++i)
			NETWORK_ENDIAN_LONG(playerIDs[i])
		NETWORK_ENDIAN_LONG(gameOptions)
		NETWORK_ENDIAN_SHORT(numPointsInGame)
	}

private:
	int16 _unused = 0;
};

struct MsgChatMessage final
{
	uint32 userID = 0;
	uint16 messageLength = 0;

private:
	int16 _padding = 0;

public:
	// char[]

	void ConvertToHostEndian()
	{
		HOST_ENDIAN_LONG(userID)
		HOST_ENDIAN_SHORT(messageLength)
	}
	void ConvertToNetworkEndian()
	{
		NETWORK_ENDIAN_LONG(userID)
		NETWORK_ENDIAN_SHORT(messageLength)
	}
};

struct MsgReplacePlayer final
{
	uint32 userIDNew = 0;
	int16 seat = 0;

private:
	int16 _unused = 0;

public:
	void ConvertToHostEndian()
	{
		HOST_ENDIAN_LONG(userIDNew)
		HOST_ENDIAN_SHORT(seat)
	}
	void ConvertToNetworkEndian()
	{
		NETWORK_ENDIAN_LONG(userIDNew)
		NETWORK_ENDIAN_SHORT(seat)
	}
};


struct MsgStartBid final
{
	int16 boardNumber = 0;
	int16 dealer = 0;
	char hand[SpadesNumCardsInHand] = {};

	void ConvertToHostEndian()
	{
		HOST_ENDIAN_SHORT(boardNumber)
		HOST_ENDIAN_SHORT(dealer)
	}
	void ConvertToNetworkEndian()
	{
		NETWORK_ENDIAN_SHORT(boardNumber)
		NETWORK_ENDIAN_SHORT(dealer)
	}

	STRUCT_PADDING(2)
};

struct MsgShowCards final
{
	int16 seat = 0;

	void ConvertToHostEndian()
	{
		HOST_ENDIAN_SHORT(seat)
	}
	void ConvertToNetworkEndian()
	{
		NETWORK_ENDIAN_SHORT(seat)
	}

	STRUCT_PADDING(2)
};

struct MsgBid final
{
	int16 seat = 0;
	int16 nextBidder = 0;
	char bid = 0;

	enum BidValues
	{
		BID_DOUBLE_NIL = -128
	};

	void ConvertToHostEndian()
	{
		HOST_ENDIAN_SHORT(seat)
		HOST_ENDIAN_SHORT(nextBidder)
	}
	void ConvertToNetworkEndian()
	{
		NETWORK_ENDIAN_SHORT(seat)
		NETWORK_ENDIAN_SHORT(nextBidder)
	}

	STRUCT_PADDING(2)
};


struct MsgStartPlay final
{
	int16 leader = 0;

	void ConvertToHostEndian()
	{
		HOST_ENDIAN_SHORT(leader)
	}
	void ConvertToNetworkEndian()
	{
		NETWORK_ENDIAN_SHORT(leader)
	}

	STRUCT_PADDING(2)
};

struct MsgPlay final
{
	int16 seat = 0;
	int16 nextPlayer = 0;
	char card = 0;

	void ConvertToHostEndian()
	{
		HOST_ENDIAN_SHORT(seat)
		HOST_ENDIAN_SHORT(nextPlayer)
	}
	void ConvertToNetworkEndian()
	{
		NETWORK_ENDIAN_SHORT(seat)
		NETWORK_ENDIAN_SHORT(nextPlayer)
	}

	STRUCT_PADDING(2)
};

struct MsgEndHand final
{
	int16 bags[2] = {};
	int16 boardNumber = 0;

private:
	int16 padding = 0;
	char _unused1[SpadesNumPlayers] = {};

public:
	int16 points[2] = {};

private:
	int16 _unused2[2] = {};

public:
	int16 pointsBase[2] = {};
	int16 pointsBagBonus[2] = {};
	int16 pointsNil[2] = {};
	int16 pointsBagPenalty[2] = {};

	void ConvertToHostEndian()
	{
		HOST_ENDIAN_SHORT(bags[0])
		HOST_ENDIAN_SHORT(bags[1])
	}
	void ConvertToNetworkEndian()
	{
		NETWORK_ENDIAN_SHORT(bags[0])
		NETWORK_ENDIAN_SHORT(bags[1])
	}
};

struct MsgEndGame final
{
	char winners[SpadesNumPlayers] = {};

	void ConvertToHostEndian() {}
	void ConvertToNetworkEndian() {}
};


struct MsgNewGameVote final
{
	int16 seat = 0;

	void ConvertToHostEndian()
	{
		HOST_ENDIAN_SHORT(seat)
	}
	void ConvertToNetworkEndian()
	{
		NETWORK_ENDIAN_SHORT(seat)
	}

	STRUCT_PADDING(2)
};


static std::ostream& operator<<(std::ostream& out, const MsgCheckIn& m)
{
	out << "Spades::MsgCheckIn:";
	return out
		<< "  protocolSignature = 0x" << std::hex << m.protocolSignature << std::dec
		<< "  protocolVersion = " << m.protocolVersion
		<< "  clientVersion = " << m.clientVersion
		<< "  playerID = " << m.playerID
		<< "  seat = " << m.seat;
}

static std::ostream& operator<<(std::ostream& out, const MsgStartGame& m)
{
	out << "Spades::MsgStartGame:"
		<< "  playerIDs = {";
	for (uint8_t i = 0; i < SpadesNumPlayers; ++i)
		out << (i == 0 ? " " : ", ") << static_cast<int>(m.playerIDs[i]);
	return out << " }"
		<< "  gameOptions = " << m.gameOptions
		<< "  numPointsInGame = " << m.numPointsInGame;
}

static std::ostream& operator<<(std::ostream& out, const MsgChatMessage& m)
{
	out << "Spades::MsgChatMessage:";
	return out
		<< "  userID = " << m.userID
		<< "  messageLength = " << m.messageLength;
}

static std::ostream& operator<<(std::ostream& out, const MsgReplacePlayer& m)
{
	out << "Spades::MsgReplacePlayer:";
	return out
		<< "  userIDNew = " << m.userIDNew
		<< "  seat = " << m.seat;
}

static std::ostream& operator<<(std::ostream& out, const MsgStartBid& m)
{
	out << "Spades::MsgStartBid:"
		<< "  boardNumber = " << m.boardNumber
		<< "  dealer = " << m.dealer
		<< "  hand = {";
	for (uint8_t i = 0; i < SpadesNumCardsInHand; ++i)
		out << (i == 0 ? " " : ", ") << static_cast<int>(m.hand[i]);
	return out << " }";
}

static std::ostream& operator<<(std::ostream& out, const MsgShowCards& m)
{
	out << "Spades::MsgShowCards:";
	return out
		<< "  seat = " << m.seat;
}

static std::ostream& operator<<(std::ostream& out, const MsgBid& m)
{
	out << "Spades::MsgBid:";
	return out
		<< "  seat = " << m.seat
		<< "  nextBidder = " << m.nextBidder
		<< "  bid = " << static_cast<int>(m.bid);
}

static std::ostream& operator<<(std::ostream& out, const MsgStartPlay& m)
{
	out << "Spades::MsgStartPlay:";
	return out
		<< "  leader = " << m.leader;
}

static std::ostream& operator<<(std::ostream& out, const MsgPlay& m)
{
	out << "Spades::MsgPlay:";
	return out
		<< "  seat = " << m.seat
		<< "  nextPlayer = " << m.nextPlayer
		<< "  card = " << static_cast<int>(m.card);
}

static std::ostream& operator<<(std::ostream& out, const MsgEndHand& m)
{
	out << "Spades::MsgEndHand:";
	return out
		<< "  bags[0] = " << m.bags[0]
		<< "  bags[1] = " << m.bags[1]
		<< "  boardNumber = " << m.boardNumber
		<< "  points[0] = " << m.points[0]
		<< "  points[1] = " << m.points[1]
		<< "  pointsBase[0] = " << m.pointsBase[0]
		<< "  pointsBase[1] = " << m.pointsBase[1]
		<< "  pointsBagBonus[0] = " << m.pointsBagBonus[0]
		<< "  pointsBagBonus[1] = " << m.pointsBagBonus[1]
		<< "  pointsNil[0] = " << m.pointsNil[0]
		<< "  pointsNil[1] = " << m.pointsNil[1]
		<< "  pointsBagPenalty[0] = " << m.pointsBagPenalty[0]
		<< "  pointsBagPenalty[1] = " << m.pointsBagPenalty[1];
}

static std::ostream& operator<<(std::ostream& out, const MsgEndGame& m)
{
	out << "Spades::MsgEndGame:"
		<< "  winners = {";
	for (uint8_t i = 0; i < SpadesNumPlayers; ++i)
		out << (i == 0 ? " " : ", ") << static_cast<int>(m.winners[i]);
	return out << " }";
}

static std::ostream& operator<<(std::ostream& out, const MsgNewGameVote& m)
{
	out << "Spades::MsgNewGameVote:";
	return out
		<< "  seat = " << m.seat;
}

}

}
