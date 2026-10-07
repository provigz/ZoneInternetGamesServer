#pragma once

#include "../Defines.hpp"
#include "../Endian.hpp"

#include <ostream>

namespace WinXP {

namespace Checkers {

#define XPCheckersProtocolSignature 0x43484B52
#define XPCheckersProtocolVersion 2
#define XPCheckersClientVersion 0x00010202

#define MECheckersProtocolVersion 0x00010202

enum
{
	MessageCheckIn = 256,
	MessageMovePiece,
	MessageChatMessage,
	MessageEndGame,
	MessageEndMatch,
	MessageFinishMove,
	MessageDrawResponse,
	MessageNewGameVote = 267
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
	uint8_t startCol = 0;
	uint8_t startRow = 0;
	uint8_t finishCol = 0;
	uint8_t finishRow = 0;

	void ConvertToHostEndian()
	{
		HOST_ENDIAN_SHORT(seat)
	}
	void ConvertToNetworkEndian()
	{
		NETWORK_ENDIAN_SHORT(seat)
	}
};

struct MsgFinishMove final
{
	int16 seat = 0;
	int16 drawSeat = 0;

	void ConvertToHostEndian()
	{
		HOST_ENDIAN_SHORT(seat)
		HOST_ENDIAN_SHORT(drawSeat)
	}
	void ConvertToNetworkEndian()
	{
		NETWORK_ENDIAN_SHORT(seat)
		NETWORK_ENDIAN_SHORT(drawSeat)
	}

private:
	uint32 _unused_time = 0;
	uint8_t _unused_piece = 0;
};

struct MsgDrawResponse final
{
	int16 seat = 0;
	int16 response = 0;

	enum Response
	{
		RESPONSE_ACCEPT = 1,
		RESPONSE_REJECT
	};

	void ConvertToHostEndian()
	{
		HOST_ENDIAN_SHORT(seat)
		HOST_ENDIAN_SHORT(response)
	}
	void ConvertToNetworkEndian()
	{
		NETWORK_ENDIAN_SHORT(seat)
		NETWORK_ENDIAN_SHORT(response)
	}
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
		FLAG_RESIGN = 16,
		FLAG_DRAW = 512
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
	int16 reason = 0;
	int16 seatLost = 0;
	int16 seatQuit = 0;

	enum Reason
	{
		REASON_GAMEOVER = 4
	};

	void ConvertToHostEndian() {}
	void ConvertToNetworkEndian() {}

private:
	int16 _padding = 0;
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
	out << "Checkers::MsgCheckIn:";
	return out
		<< "  protocolSignature = 0x" << std::hex << m.protocolSignature << std::dec
		<< "  protocolVersion = " << m.protocolVersion
		<< "  clientVersion = " << m.clientVersion
		<< "  playerID = " << m.playerID
		<< "  seat = " << m.seat;
}

static std::ostream& operator<<(std::ostream& out, const MsgChatMessage& m)
{
	out << "Checkers::MsgChatMessage:";
	return out
		<< "  userID = " << m.userID
		<< "  seat = " << m.seat
		<< "  messageLength = " << m.messageLength;
}

static std::ostream& operator<<(std::ostream& out, const MsgMovePiece& m)
{
	out << "Checkers::MsgMovePiece:";
	return out
		<< "  seat = " << m.seat
		<< "  startCol = " << static_cast<int>(m.startCol)
		<< "  startRow = " << static_cast<int>(m.startRow)
		<< "  finishCol = " << static_cast<int>(m.finishCol)
		<< "  finishRow = " << static_cast<int>(m.finishRow);
}

static std::ostream& operator<<(std::ostream& out, const MsgFinishMove& m)
{
	out << "Checkers::MsgFinishMove:";
	return out
		<< "  seat = " << m.seat
		<< "  drawSeat = " << m.drawSeat;
}

static std::ostream& operator<<(std::ostream& out, const MsgDrawResponse& m)
{
	out << "Checkers::MsgDrawResponse:";
	return out
		<< "  seat = " << m.seat
		<< "  response = " << m.response;
}

static std::ostream& operator<<(std::ostream& out, const MsgEndGame& m)
{
	out << "Checkers::MsgEndGame:";
	return out
		<< "  seat = " << m.seat
		<< "  flags = " << m.flags;
}

static std::ostream& operator<<(std::ostream& out, const MsgEndMatch& m)
{
	out << "Checkers::MsgEndMatch:";
	return out
		<< "  reason = " << m.reason
		<< "  seatLost = " << m.seatLost
		<< "  seatQuit = " << m.seatQuit;
}

static std::ostream& operator<<(std::ostream& out, const MsgNewGameVote& m)
{
	out << "Checkers::MsgNewGameVote:";
	return out
		<< "  seat = " << m.seat;
}

}

}
