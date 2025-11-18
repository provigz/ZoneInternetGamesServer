#pragma once

#include "../Defines.hpp"
#include "../Endian.hpp"

#include <ostream>

namespace WinXP {

namespace Hearts {

#define XPHeartsProtocolSignature 0x6872747A
#define XPHeartsProtocolVersion 5
#define XPHeartsClientVersion 66816

#define HeartsNumPlayers 4
#define HeartsNumPointsInGame 100
#define HeartsNumPointsInHand 26
#define HeartsNumCardsInHand 13
#define HeartsNumCardsInPass 3
#define HeartsPassDirections 4

#define HeartsUnsetCard 0x7F
#define HeartsCard2C 0
#define HeartsCardQS 36

enum
{
	MessageStartGame = 256,
	MessageReplacePlayer,
	MessageStartHand,
	MessageStartPlay,
	MessageEndHand,
	MessageEndGame,
	MessageCheckIn,
	MessagePass,
	MessagePlay,
	MessageNewGameVote,
	MessageChatMessage
};


struct MsgCheckIn final
{
	uint32 protocolSignature = 0;
	uint32 protocolVersion = 0;
	uint32 clientVersion = 0;
	int16 seat = 0;

	void ConvertToHostEndian()
	{
		HOST_ENDIAN_LONG(protocolSignature)
		HOST_ENDIAN_LONG(protocolVersion)
		HOST_ENDIAN_LONG(clientVersion)
		HOST_ENDIAN_SHORT(seat)
	}
	void ConvertToNetworkEndian()
	{
		NETWORK_ENDIAN_LONG(protocolSignature)
		NETWORK_ENDIAN_LONG(protocolVersion)
		NETWORK_ENDIAN_LONG(clientVersion)
		NETWORK_ENDIAN_SHORT(seat)
	}

private:
	int16 _padding = 0;
};

struct MsgStartGame final
{
	uint16 numCardsInHand = HeartsNumCardsInHand;
	uint16 numCardsInPass = HeartsNumCardsInPass;
	uint16 numPointsInGame = HeartsNumPointsInGame;

private:
	int16 _padding1 = 0;

public:
	uint32 gameOptions = 0;

private:
	uint32 _padding2 = 0;

public:
	uint32 playerIDs[HeartsNumPlayers] = {};

private:
	uint32 _unused[2] = {};

public:
	void ConvertToHostEndian()
	{
		HOST_ENDIAN_SHORT(numCardsInHand)
		HOST_ENDIAN_SHORT(numCardsInPass)
		HOST_ENDIAN_SHORT(numPointsInGame)
		HOST_ENDIAN_LONG(gameOptions)
		for (BYTE i = 0; i < HeartsNumPlayers; ++i)
			HOST_ENDIAN_LONG(playerIDs[i])
	}
	void ConvertToNetworkEndian()
	{
		NETWORK_ENDIAN_SHORT(numCardsInHand)
		NETWORK_ENDIAN_SHORT(numCardsInPass)
		NETWORK_ENDIAN_SHORT(numPointsInGame)
		NETWORK_ENDIAN_LONG(gameOptions)
		for (BYTE i = 0; i < HeartsNumPlayers; ++i)
			NETWORK_ENDIAN_LONG(playerIDs[i])
	}
};

struct MsgChatMessage final
{
	uint32 userID = 0;
	int16 seat = 0;
	uint16 messageLength = 0;
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


struct MsgStartHand final
{
	int16 passDirection = 0;
	char hand[HeartsNumCardsInHand] = {};

private:
	char _unused[5] = {};

public:
	enum PassDirection
	{
		PASS_NONE = 0,
		PASS_LEFT,
		PASS_RIGHT,
		PASS_ACROSS,
	};

	void ConvertToHostEndian()
	{
		HOST_ENDIAN_SHORT(passDirection)
	}
	void ConvertToNetworkEndian()
	{
		NETWORK_ENDIAN_SHORT(passDirection)
	}
};

struct MsgPass final
{
	int16 seat = 0;
	char cards[HeartsNumCardsInPass] = {};

private:
	char _unused[2] = {};

public:
	void ConvertToHostEndian()
	{
		HOST_ENDIAN_SHORT(seat)
	}
	void ConvertToNetworkEndian()
	{
		NETWORK_ENDIAN_SHORT(seat)
	}
};


struct MsgStartPlay final
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

private:
	int16 _padding = 0;
};

struct MsgPlay final
{
	int16 seat = 0;
	char card = 0;

	void ConvertToHostEndian()
	{
		HOST_ENDIAN_SHORT(seat)
	}
	void ConvertToNetworkEndian()
	{
		NETWORK_ENDIAN_SHORT(seat)
	}

private:
	char _padding = 0;
};

struct MsgEndHand final
{
	int16 points[HeartsNumPlayers] = {};

private:
	int16 _unused1[2] = {};
	int16 _unused2 = 0;

public:
	void ConvertToHostEndian()
	{
		for (BYTE i = 0; i < HeartsNumPlayers; ++i)
			HOST_ENDIAN_SHORT(points[i])
	}
	void ConvertToNetworkEndian()
	{
		for (BYTE i = 0; i < HeartsNumPlayers; ++i)
			NETWORK_ENDIAN_SHORT(points[i])
	}

	STRUCT_PADDING(2)
};

struct MsgEndGame final
{
private:
	int16 _unused1 = 0;
	uint16 _unused2 = 0;

public:
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
	out << "Hearts::MsgCheckIn:";
	return out
		<< "  protocolSignature = 0x" << std::hex << m.protocolSignature << std::dec
		<< "  protocolVersion = " << m.protocolVersion
		<< "  clientVersion = " << m.clientVersion
		<< "  seat = " << m.seat;
}

static std::ostream& operator<<(std::ostream& out, const MsgStartGame& m)
{
	out << "Hearts::MsgStartGame:"
		<< "  numCardsInHand = " << m.numCardsInHand
		<< "  numCardsInPass = " << m.numCardsInPass
		<< "  numPointsInGame = " << m.numPointsInGame
		<< "  gameOptions = " << m.gameOptions
		<< "  playerIDs = {";
	for (uint8_t i = 0; i < HeartsNumPlayers; ++i)
		out << (i == 0 ? " " : ", ") << static_cast<int>(m.playerIDs[i]);
	return out << " }";
}

static std::ostream& operator<<(std::ostream& out, const MsgChatMessage& m)
{
	out << "Hearts::MsgChatMessage:";
	return out
		<< "  userID = " << m.userID
		<< "  seat = " << m.seat
		<< "  messageLength = " << m.messageLength;
}

static std::ostream& operator<<(std::ostream& out, const MsgReplacePlayer& m)
{
	out << "Hearts::MsgReplacePlayer:";
	return out
		<< "  userIDNew = " << m.userIDNew
		<< "  seat = " << m.seat;
}

static std::ostream& operator<<(std::ostream& out, const MsgStartHand& m)
{
	out << "Hearts::MsgStartHand:"
		<< "  passDirection = " << m.passDirection
		<< "  hand = {";
	for (uint8_t i = 0; i < HeartsNumCardsInHand; ++i)
		out << (i == 0 ? " " : ", ") << static_cast<int>(m.hand[i]);
	return out << " }";
}

static std::ostream& operator<<(std::ostream& out, const MsgPass& m)
{
	out << "Hearts::MsgPass:"
		<< "  seat = " << m.seat
		<< "  cards = {";
	for (uint8_t i = 0; i < HeartsNumCardsInPass; ++i)
		out << (i == 0 ? " " : ", ") << static_cast<int>(m.cards[i]);
	return out << " }";
}

static std::ostream& operator<<(std::ostream& out, const MsgStartPlay& m)
{
	out << "Hearts::MsgStartPlay:";
	return out
		<< "  seat = " << m.seat;
}

static std::ostream& operator<<(std::ostream& out, const MsgPlay& m)
{
	out << "Hearts::MsgPlay:";
	return out
		<< "  seat = " << m.seat
		<< "  card = " << static_cast<int>(m.card);
}

static std::ostream& operator<<(std::ostream& out, const MsgEndHand& m)
{
	out << "Hearts::MsgEndHand:"
		<< "  points = {";
	for (uint8_t i = 0; i < HeartsNumPlayers; ++i)
		out << (i == 0 ? " " : ", ") << static_cast<int>(m.points[i]);
	return out << " }";
}

static std::ostream& operator<<(std::ostream& out, const MsgEndGame& m)
{
	out << "Hearts::MsgEndGame:";
	return out;
}

static std::ostream& operator<<(std::ostream& out, const MsgNewGameVote& m)
{
	out << "Hearts::MsgNewGameVote:";
	return out
		<< "  seat = " << m.seat;
}

}

}
