#pragma once

#include "../Defines.hpp"
#include "../Endian.hpp"

#include <ostream>

namespace WinXP {

namespace Reversi {

#define XPReversiProtocolSignature 0x72767365
#define XPReversiProtocolVersion 3
#define XPReversiClientVersion 0x00010204

#define MEReversiProtocolVersion 0x00010204

enum
{
	MessageCheckIn = 256,
	MessageMovePiece,
	MessageChatMessage,
	MessageEndGame,
	MessageEndMatch,
	MessageNewGameVote = 266
};


struct MsgCheckIn final
{
	int32 protocolSignature = 0;
	int32 protocolVersion = 0;
	int32 clientVersion = 0;
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

struct MsgChatMessage final
{
	uint32 userID = 0;
	int16 seat = 0;
	uint16 messageLength = 0;
	// char[]

	void ConvertToHostEndian()
	{
		HOST_ENDIAN_LONG(userID)
		HOST_ENDIAN_SHORT(seat)
		HOST_ENDIAN_SHORT(messageLength)
	}
	void ConvertToNetworkEndian()
	{
		NETWORK_ENDIAN_LONG(userID)
		NETWORK_ENDIAN_SHORT(seat)
		NETWORK_ENDIAN_SHORT(messageLength)
	}
};


struct MsgMovePiece final
{
	int16 seat = 0;

private:
	int16 _padding = 0;

public:
	uint8_t col = 0;
	uint8_t row = 0;

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

struct MsgEndGame final
{
	int16 seat = 0;

private:
	int16 _padding = 0;

public:
	uint32 flags = 0;

	enum Flags
	{
		FLAG_RESIGN = 16
	};

	void ConvertToHostEndian()
	{
		HOST_ENDIAN_SHORT(seat)
		HOST_ENDIAN_LONG(flags)
	}
	void ConvertToNetworkEndian()
	{
		NETWORK_ENDIAN_SHORT(seat)
		NETWORK_ENDIAN_LONG(flags)
	}
};

struct MsgEndMatch final
{
	int32 numPoints = 0;
	int16 reason = 0;
	int16 seatLost = 0;
	int16 seatQuit = 0;

private:
	int16 _padding = 0;

public:
	int16 pieceCount[2];

	enum Reason
	{
		REASON_GAMEOVER = 4
	};

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
	out << "Reversi::MsgCheckIn:";
	return out
		<< "  protocolSignature = 0x" << std::hex << m.protocolSignature << std::dec
		<< "  protocolVersion = " << m.protocolVersion
		<< "  clientVersion = " << m.clientVersion
		<< "  playerID = " << m.playerID
		<< "  seat = " << m.seat;
}

static std::ostream& operator<<(std::ostream& out, const MsgChatMessage& m)
{
	out << "Reversi::MsgChatMessage:";
	return out
		<< "  userID = " << m.userID
		<< "  seat = " << m.seat
		<< "  messageLength = " << m.messageLength;
}

static std::ostream& operator<<(std::ostream& out, const MsgMovePiece& m)
{
	out << "Reversi::MsgMovePiece:";
	return out
		<< "  seat = " << m.seat
		<< "  col = " << static_cast<int>(m.col)
		<< "  row = " << static_cast<int>(m.row);
}

static std::ostream& operator<<(std::ostream& out, const MsgEndGame& m)
{
	out << "Reversi::MsgEndGame:";
	return out
		<< "  seat = " << m.seat
		<< "  flags = " << m.flags;
}

static std::ostream& operator<<(std::ostream& out, const MsgEndMatch& m)
{
	out << "Reversi::MsgEndMatch:";
	return out
		<< "  numPoints = " << m.numPoints
		<< "  reason = " << m.reason
		<< "  seatLost = " << m.seatLost
		<< "  seatQuit = " << m.seatQuit
		<< "  pieceCount[0] = " << m.pieceCount[0]
		<< "  pieceCount[1] = " << m.pieceCount[1];
}

static std::ostream& operator<<(std::ostream& out, const MsgNewGameVote& m)
{
	out << "Reversi::MsgNewGameVote:";
	return out
		<< "  seat = " << m.seat;
}

}

}
