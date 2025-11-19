#include "MatchManager.hpp"

#include <cassert>
#include <iostream>
#include <stdexcept>
#include <synchapi.h>

#include "Config.hpp"
#include "Util.hpp"
#include "Win7/BackgammonMatch.hpp"
#include "Win7/CheckersMatch.hpp"
#include "Win7/PlayerSocket.hpp"
#include "Win7/SpadesMatch.hpp"
#include "WinXP/BackgammonMatch.hpp"
#include "WinXP/CheckersMatch.hpp"
#include "WinXP/HeartsMatch.hpp"
#include "WinXP/PlayerSocket.hpp"
#include "WinXP/ReversiMatch.hpp"
#include "WinXP/SpadesMatch.hpp"

MatchManager MatchManager::s_instance;

DWORD WINAPI
MatchManager::UpdateHandler(void*)
{
	while (true)
	{
		s_instance.Update();
		Sleep(1000); // Update once each second
	}

	return 0;
}


MatchManager::MatchManager() :
	m_mutex(CreateMutex(nullptr, false, nullptr)),
	m_lastMatchIndex(0),
	m_matches_win7(),
	m_matches_winxp()
{
	if (!m_mutex)
		throw std::runtime_error("MatchManager: Couldn't create mutex: " + std::to_string(GetLastError()));
}

MatchManager::~MatchManager()
{
	CloseHandle(m_mutex);
}


std::pair<const std::vector<std::unique_ptr<Win7::Match>>&, const std::vector<std::unique_ptr<WinXP::Match>>&>
MatchManager::AcquireMatches()
{
	switch (WaitForSingleObject(m_mutex, SOCKET_TIMEOUT_MS + 10000))
	{
		case WAIT_OBJECT_0: // Acquired ownership of the mutex
			return { m_matches_win7, m_matches_winxp };
		case WAIT_TIMEOUT:
			throw std::runtime_error("MatchManager::AcquireMatchesWin7(): Timed out waiting for mutex: " + std::to_string(GetLastError()));
		case WAIT_ABANDONED: // Acquired ownership of an abandoned mutex
			throw std::runtime_error("MatchManager::AcquireMatchesWin7(): Got ownership of an abandoned mutex: " + std::to_string(GetLastError()));
		default:
			throw std::runtime_error("MatchManager::AcquireMatchesWin7(): An error occured waiting for mutex: " + std::to_string(GetLastError()));
	}
}

void
MatchManager::FreeAcquiredMatches()
{
	if (!ReleaseMutex(m_mutex))
		throw std::runtime_error("MatchManager::FreeAcquiredMatches(): Couldn't release mutex: " + std::to_string(GetLastError()));
}


void
MatchManager::Update()
{
	switch (WaitForSingleObject(m_mutex, SOCKET_TIMEOUT_MS + 10000))
	{
		case WAIT_OBJECT_0: // Acquired ownership of the mutex
			break;
		case WAIT_TIMEOUT:
			throw std::runtime_error("MatchManager::Update(): Timed out waiting for mutex: " + std::to_string(GetLastError()));
		case WAIT_ABANDONED: // Acquired ownership of an abandoned mutex
			throw std::runtime_error("MatchManager::Update(): Got ownership of an abandoned mutex: " + std::to_string(GetLastError()));
		default:
			throw std::runtime_error("MatchManager::Update(): An error occured waiting for mutex: " + std::to_string(GetLastError()));
	}

	for (auto it = m_matches_win7.begin(); it != m_matches_win7.end();)
	{
		const auto& match = *it;

		if (match->GetState() != Win7::Match::STATE_ENDED)
			match->Update();

		// Close ended matches
		if (match->GetState() == Win7::Match::STATE_ENDED)
		{
			SessionLog() << "[MATCH MANAGER] Closing ended Windows 7 " << Win7::Match::GameToNameString(match->GetGame())
				<< " match " << match->GetGUID() << "!" << std::endl;

			switch (WaitForSingleObject(match->m_mutex, SOCKET_TIMEOUT_MS + 10000))
			{
				case WAIT_OBJECT_0: // Acquired ownership of the match mutex
					break;
				case WAIT_TIMEOUT:
					throw std::runtime_error("MatchManager::Update(): Timed out waiting for Win7::Match mutex: " + std::to_string(GetLastError()));
				case WAIT_ABANDONED: // Acquired ownership of an abandoned match mutex
					throw std::runtime_error("MatchManager::Update(): Got ownership of an abandoned Win7::Match mutex: " + std::to_string(GetLastError()));
				default:
					throw std::runtime_error("MatchManager::Update(): An error occured waiting for Win7::Match mutex: " + std::to_string(GetLastError()));
			}
			it = m_matches_win7.erase(it);
			continue;
		}
		++it;
	}
	for (auto it = m_matches_winxp.begin(); it != m_matches_winxp.end();)
	{
		const auto& match = *it;

		if (match->GetState() != WinXP::Match::STATE_ENDED)
			match->Update();

		// Close ended matches
		if (match->GetState() == WinXP::Match::STATE_ENDED)
		{
			SessionLog() << "[MATCH MANAGER] Closing ended Windows XP " << WinXP::Match::GameToNameString(match->GetGame())
				<< " match " << match->GetGUID() << "!" << std::endl;

			switch (WaitForSingleObject(match->m_mutex, SOCKET_TIMEOUT_MS + 10000))
			{
				case WAIT_OBJECT_0: // Acquired ownership of the match mutex
					break;
				case WAIT_TIMEOUT:
					throw std::runtime_error("MatchManager::DestroyMatch(): Timed out waiting for WinXP::Match mutex: " + std::to_string(GetLastError()));
				case WAIT_ABANDONED: // Acquired ownership of an abandoned match mutex
					throw std::runtime_error("MatchManager::Update(): Got ownership of an abandoned WinXP::Match mutex: " + std::to_string(GetLastError()));
				default:
					throw std::runtime_error("MatchManager::Update(): An error occured waiting for WinXP::Match mutex: " + std::to_string(GetLastError()));
			}
			it = m_matches_winxp.erase(it);
			continue;
		}
		++it;
	}

	if (!ReleaseMutex(m_mutex))
		throw std::runtime_error("MatchManager::Update(): Couldn't release mutex: " + std::to_string(GetLastError()));
}

GUID
MatchManager::DestroyMatch(unsigned int index)
{
	switch (WaitForSingleObject(m_mutex, SOCKET_TIMEOUT_MS + 10000))
	{
		case WAIT_OBJECT_0: // Acquired ownership of the mutex
			break;
		case WAIT_TIMEOUT:
			throw std::runtime_error("MatchManager::DestroyMatch(): Timed out waiting for mutex: " + std::to_string(GetLastError()));
		case WAIT_ABANDONED: // Acquired ownership of an abandoned mutex
			throw std::runtime_error("MatchManager::DestroyMatch(): Got ownership of an abandoned mutex: " + std::to_string(GetLastError()));
		default:
			throw std::runtime_error("MatchManager::DestroyMatch(): An error occured waiting for mutex: " + std::to_string(GetLastError()));
	}

	GUID matchGUID;

	auto it7 = std::find_if(m_matches_win7.begin(), m_matches_win7.end(),
		[index](const auto& m) { return m->GetIndex() == index; });
	if (it7 == m_matches_win7.end())
	{
		auto itXP = std::find_if(m_matches_winxp.begin(), m_matches_winxp.end(),
			[index](const auto& m) { return m->GetIndex() == index; });
		if (itXP != m_matches_winxp.end())
		{
			const auto& match = *itXP;
			matchGUID = match->GetGUID();

			SessionLog() << "[MATCH MANAGER] Closing Windows XP " << WinXP::Match::GameToNameString(match->GetGame())
				<< " match " << matchGUID << " per request!" << std::endl;

			switch (WaitForSingleObject(match->m_mutex, SOCKET_TIMEOUT_MS + 10000))
			{
				case WAIT_OBJECT_0: // Acquired ownership of the match mutex
					break;
				case WAIT_TIMEOUT:
					throw std::runtime_error("MatchManager::DestroyMatch(): Timed out waiting for WinXP::Match mutex: " + std::to_string(GetLastError()));
				case WAIT_ABANDONED: // Acquired ownership of an abandoned match mutex
					throw std::runtime_error("MatchManager::DestroyMatch(): Got ownership of an abandoned WinXP::Match mutex: " + std::to_string(GetLastError()));
				default:
					throw std::runtime_error("MatchManager::DestroyMatch(): An error occured waiting for WinXP::Match mutex: " + std::to_string(GetLastError()));
			}
			m_matches_winxp.erase(itXP);
		}
		else
		{
			if (!ReleaseMutex(m_mutex))
				throw std::runtime_error("MatchManager::DestroyMatch(): Couldn't release mutex: " + std::to_string(GetLastError()));
			throw std::runtime_error("No match with index " + std::to_string(index) + '!');
		}
	}
	else
	{
		const auto& match = *it7;
		matchGUID = match->GetGUID();

		SessionLog() << "[MATCH MANAGER] Closing Windows 7 " << Win7::Match::GameToNameString(match->GetGame())
			<< " match " << matchGUID << " per request!" << std::endl;

		switch (WaitForSingleObject(match->m_mutex, SOCKET_TIMEOUT_MS + 10000))
		{
			case WAIT_OBJECT_0: // Acquired ownership of the match mutex
				break;
			case WAIT_TIMEOUT:
				throw std::runtime_error("MatchManager::DestroyMatch(): Timed out waiting for Win7::Match mutex: " + std::to_string(GetLastError()));
			case WAIT_ABANDONED: // Acquired ownership of an abandoned match mutex
				throw std::runtime_error("MatchManager::DestroyMatch(): Got ownership of an abandoned Win7::Match mutex: " + std::to_string(GetLastError()));
			default:
				throw std::runtime_error("MatchManager::DestroyMatch(): An error occured waiting for Win7::Match mutex: " + std::to_string(GetLastError()));
		}
		m_matches_win7.erase(it7);
	}

	if (!ReleaseMutex(m_mutex))
		throw std::runtime_error("MatchManager::DestroyMatch(): Couldn't release mutex: " + std::to_string(GetLastError()));
	return matchGUID;
}


Win7::Match*
MatchManager::FindLobby(Win7::PlayerSocket& player)
{
	if (player.GetLevel() == Win7::Match::Level::INVALID)
		throw std::runtime_error("Cannot find lobby for Windows 7 player: Invalid level!");

	Win7::Match* targetMatch = nullptr;
	for (const auto& match : m_matches_win7)
	{
		if (match->GetState() == Win7::Match::STATE_WAITINGFORPLAYERS &&
			match->GetGame() == player.GetGame() &&
			(g_config.skipLevelMatching || match->GetLevel() == player.GetLevel()))
		{
			targetMatch = match.get();
			break;
		}
	}
	if (targetMatch)
	{
		targetMatch->JoinPlayer(player);
		SessionLog() << "[MATCH MANAGER] Added " << player.GetAddress()
			<< " to existing Windows 7 " << Win7::Match::GameToNameString(targetMatch->GetGame())
			<< " match " << targetMatch->GetGUID() << '.' << std::endl;
		return targetMatch;
	}

	// No free lobby found - create a new one
	Win7::Match* match = CreateLobby(player);
	SessionLog() << "[MATCH MANAGER] Added " << player.GetAddress()
		<< " to new Windows 7 " << Win7::Match::GameToNameString(match->GetGame())
		<< " match " << match->GetGUID() << '.' << std::endl;
	return match;
}

WinXP::Match*
MatchManager::FindLobby(WinXP::PlayerSocket& player)
{
	if (player.GetSkillLevel() == WinXP::Match::SkillLevel::INVALID)
		throw std::runtime_error("Cannot find lobby for Windows XP player: Invalid skill level!");

	WinXP::Match* targetMatch = nullptr;
	for (const auto& match : m_matches_winxp)
	{
		if (match->GetState() == WinXP::Match::STATE_WAITINGFORPLAYERS &&
			match->GetGame() == player.GetGame() &&
			(g_config.skipLevelMatching || match->GetSkillLevel() == player.GetSkillLevel()))
		{
			targetMatch = match.get();
			break;
		}
	}
	if (targetMatch)
	{
		targetMatch->JoinPlayer(player);
		SessionLog() << "[MATCH MANAGER] Added " << player.GetAddress()
			<< " to existing Windows XP " << WinXP::Match::GameToNameString(targetMatch->GetGame())
			<< " match " << targetMatch->GetGUID() << '.' << std::endl;
		return targetMatch;
	}

	// No free lobby found - create a new one
	WinXP::Match* match = CreateLobby(player);
	SessionLog() << "[MATCH MANAGER] Added " << player.GetAddress()
		<< " to new Windows XP " << WinXP::Match::GameToNameString(match->GetGame())
		<< " match " << match->GetGUID() << '.' << std::endl;
	return match;
}


Win7::Match*
MatchManager::CreateLobby(Win7::PlayerSocket& player)
{
	switch (WaitForSingleObject(m_mutex, SOCKET_TIMEOUT_MS + 10000))
	{
		case WAIT_OBJECT_0: // Acquired ownership of the mutex
			break;
		case WAIT_TIMEOUT:
			throw std::runtime_error("MatchManager::CreateLobby(): Timed out waiting for mutex: " + std::to_string(GetLastError()));
		case WAIT_ABANDONED: // Acquired ownership of an abandoned mutex
			throw std::runtime_error("MatchManager::CreateLobby(): Got ownership of an abandoned mutex: " + std::to_string(GetLastError()));
		default:
			throw std::runtime_error("MatchManager::CreateLobby(): An error occured waiting for mutex: " + std::to_string(GetLastError()));
	}

	switch (player.GetGame())
	{
		case Win7::Match::Game::BACKGAMMON:
			m_matches_win7.push_back(std::make_unique<Win7::BackgammonMatch>(++m_lastMatchIndex, player));
			break;

		case Win7::Match::Game::CHECKERS:
			m_matches_win7.push_back(std::make_unique<Win7::CheckersMatch>(++m_lastMatchIndex, player));
			break;

		case Win7::Match::Game::SPADES:
			m_matches_win7.push_back(std::make_unique<Win7::SpadesMatch>(++m_lastMatchIndex, player));
			break;

		default:
			throw std::runtime_error("Cannot create lobby for Windows 7 player: Invalid game type!");
	}

	if (!ReleaseMutex(m_mutex))
		throw std::runtime_error("MatchManager::CreateLobby(): Couldn't release mutex: " + std::to_string(GetLastError()));
	return m_matches_win7.back().get();
}

WinXP::Match*
MatchManager::CreateLobby(WinXP::PlayerSocket& player)
{
	switch (WaitForSingleObject(m_mutex, SOCKET_TIMEOUT_MS + 10000))
	{
		case WAIT_OBJECT_0: // Acquired ownership of the mutex
			break;
		case WAIT_TIMEOUT:
			throw std::runtime_error("MatchManager::CreateLobby(): Timed out waiting for mutex: " + std::to_string(GetLastError()));
		case WAIT_ABANDONED: // Acquired ownership of an abandoned mutex
			throw std::runtime_error("MatchManager::CreateLobby(): Got ownership of an abandoned mutex: " + std::to_string(GetLastError()));
		default:
			throw std::runtime_error("MatchManager::CreateLobby(): An error occured waiting for mutex: " + std::to_string(GetLastError()));
	}

	switch (player.GetGame())
	{
		case WinXP::Match::Game::BACKGAMMON:
			m_matches_winxp.push_back(std::make_unique<WinXP::BackgammonMatch>(++m_lastMatchIndex, player));
			break;

		case WinXP::Match::Game::CHECKERS:
			m_matches_winxp.push_back(std::make_unique<WinXP::CheckersMatch>(++m_lastMatchIndex, player));
			break;

		case WinXP::Match::Game::SPADES:
			m_matches_winxp.push_back(std::make_unique<WinXP::SpadesMatch>(++m_lastMatchIndex, player));
			break;

		case WinXP::Match::Game::HEARTS:
			m_matches_winxp.push_back(std::make_unique<WinXP::HeartsMatch>(++m_lastMatchIndex, player));
			break;

		case WinXP::Match::Game::REVERSI:
			m_matches_winxp.push_back(std::make_unique<WinXP::ReversiMatch>(++m_lastMatchIndex, player));
			break;

		default:
			throw std::runtime_error("Cannot create lobby for Windows XP player: Invalid game type!");
	}

	if (!ReleaseMutex(m_mutex))
		throw std::runtime_error("MatchManager::CreateLobby(): Couldn't release mutex: " + std::to_string(GetLastError()));
	return m_matches_winxp.back().get();
}
