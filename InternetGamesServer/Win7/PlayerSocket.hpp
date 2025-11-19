#pragma once

#include "../PlayerSocket.hpp"

#include "Match.hpp"
#include "../Util.hpp"

namespace Win7 {

class PlayerSocket final : public ::PlayerSocket
{
public:
	enum State {
		STATE_INITIALIZED,
		STATE_JOINING,
		STATE_WAITINGFOROPPONENTS,
		STATE_PLAYING
	};
	static std::string StateToString(State state);

public:
	PlayerSocket(Socket& socket);
	~PlayerSocket() override;

	void ProcessMessages() override;

	/** Event handling */
	void OnGameStart();
	void OnMatchDisconnect();
	void OnEventReceive(const std::string& xml) const;
	void OnChat(const StateChatTag* tag);

	inline State GetState() const { return m_state; }
	inline std::string GetPUID() const { return m_puid; }
	inline Match::Game GetGame() const { return m_game; }
	inline Match::Level GetLevel() const { return m_level; }
	inline Match* GetMatch() const { return m_match; }

protected:
	void OnDisconnected() override;

private:
	void ParseSasTicket(const std::string& xml);
	void ParseGasTicket(const std::string& xml);
	void ParsePasTicket(const std::string& xml);

	/** Construct protocol messages */
	std::string ConstructJoinContextMessage() const;
	std::string ConstructReadyMessage() const;
	std::string ConstructStateMessage(const std::string& xml) const;

	std::vector<std::string> GetResponse(const std::vector<std::string>& receivedData, bool& skipOtherLines);

private:
	ChangeTimeTracker<State> m_state;

	std::string m_guid;
	std::string m_puid;
	Match::Game m_game;
	Match::Level m_level;

	Match* m_match;

public:
	// Variables, set by the match
	const int8_t m_role;

private:
	PlayerSocket(const PlayerSocket&) = delete;
	PlayerSocket operator=(const PlayerSocket&) = delete;
};

}
