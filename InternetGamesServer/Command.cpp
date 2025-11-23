#include "Command.hpp"

#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

#include "Config.hpp"
#include "MatchManager.hpp"
#include "Socket.hpp"
#include "Util.hpp"
#include "Win7/Match.hpp"
#include "Win7/PlayerSocket.hpp"
#include "WinXP/Match.hpp"
#include "WinXP/PlayerSocket.hpp"

DWORD WINAPI CommandHandler(void*)
{
	std::string input;
	while (true)
	{
		std::cout << "> ";
		std::getline(std::cin, input);

		std::stringstream inputStr(input);

		std::string cmd;
		inputStr >> cmd;
		if (cmd.empty())
			continue;

		/* Process command */
		try
		{
			if (cmd[0] == '?' || cmd[0] == 'h' || cmd[0] == 'H')
			{
				std::cout << "List of commands:" << std::endl
					<< "  - 'c {key} {value [optional]}': Prints or configures the option with the specified key. For a list of valid options, type 'c'." << std::endl
					<< "  - 'lc': Lists all connected client sockets." << std::endl
					<< "  - 'lm': Lists all active matches." << std::endl
					<< "  - 'k {ip}:{port [optional]}': Kicks any connected client sockets, originating from the provided IP and port." << std::endl
					<< "                                If no port is specified, any clients that originate from the IP will be kicked." << std::endl
					<< "                                NOTE: This command, as well as 'b' which also kicks players, does not guarantee that other players will stay connected to the server!" << std::endl
					<< "  - 'b {ip}': Bans any client sockets, originating from the provided IP, from connecting to this server." << std::endl
					<< "  - 'u {ip}': Removes an IP from the ban list." << std::endl
					<< "  - 'lb': Lists all banned IPs." << std::endl
					<< "  - 'd {index}': Destroys (disbands) the match with the specified index." << std::endl;
			}
			else if (cmd[0] == 'c' || cmd[0] == 'C')
			{
				std::string key, value;
				inputStr >> key >> value;
				if (key.empty())
				{
					std::cout << "List of options:" << std::endl;
					for (const auto& optionKey : Config::s_optionKeys)
						std::cout << "  - \"" << optionKey.first << "\": " << optionKey.second << std::endl;
					continue;
				}
				if (value.empty())
				{
					std::cout << '"' << g_config.GetValue(key) << '"' << std::endl;
					continue;
				}

				g_config.SetValue(key, value);
			}
			else if (cmd[0] == 'k' || cmd[0] == 'K')
			{
				std::string address;
				inputStr >> address;
				if (address.empty())
				{
					std::cout << "No client address provided!" << std::endl;
					continue;
				}
				std::stringstream addrStr(address);
				std::string ip, portStr;
				std::getline(addrStr, ip, ':');

				switch (WaitForSingleObject(Socket::s_socketListMutex, SOCKET_LIST_MUTEX_TIMEOUT_MS))
				{
					case WAIT_OBJECT_0: // Acquired ownership of the mutex
						break;
					case WAIT_TIMEOUT:
						throw MutexError("CommandHandler(): Kick: Timed out waiting for socket list mutex: " + std::to_string(GetLastError()));
					case WAIT_ABANDONED: // Acquired ownership of an abandoned mutex
						throw MutexError("CommandHandler(): Kick: Got ownership of an abandoned socket list mutex: " + std::to_string(GetLastError()));
					default:
						throw MutexError("CommandHandler(): Kick: An error occured waiting for socket list mutex: " + std::to_string(GetLastError()));
				}
				if (std::getline(addrStr, portStr, ':'))
				{
					USHORT port;
					try
					{
						port = static_cast<USHORT>(std::stoi(portStr));
					}
					catch (const std::exception& err)
					{
						throw std::runtime_error("Invalid port number: " + std::string(err.what()));
					}
					Socket* socket = Socket::GetSocketByIP(ip, port);
					if (!socket)
						throw std::runtime_error("No socket with IP \"" + ip + "\" and port " + std::to_string(port) + " found!");

					const Socket::Address sockAddr = socket->GetAddress();
					socket->Disconnect();
					std::cout << "Disconnected socket \"" << sockAddr << "\"!" << std::endl;
				}
				else
				{
					const std::vector<Socket*> sockets = Socket::GetSocketsByIP(ip);
					if (sockets.empty())
						throw std::runtime_error("No sockets with IP \"" + ip + "\" found!");
					for (Socket* socket : sockets)
					{
						const Socket::Address sockAddr = socket->GetAddress();
						socket->Disconnect();
						std::cout << "Disconnected socket \"" << sockAddr << "\"!" << std::endl;
					}
				}
				if (!ReleaseMutex(Socket::s_socketListMutex))
					throw MutexError("CommandHandler(): Kick: Couldn't release socket list mutex: " + std::to_string(GetLastError()));
			}
			else if (cmd[0] == 'b' || cmd[0] == 'B')
			{
				std::string address;
				inputStr >> address;
				if (address.empty())
				{
					std::cout << "No client IP provided!" << std::endl;
					continue;
				}
				// Split string by ':' just in case a port was specified. In such case, it will be ignored
				std::stringstream addrStr(address);
				std::string ip;
				std::getline(addrStr, ip, ':');

				// 1. Disconnect any sockets, originating from the given IP
				switch (WaitForSingleObject(Socket::s_socketListMutex, SOCKET_LIST_MUTEX_TIMEOUT_MS))
				{
					case WAIT_OBJECT_0: // Acquired ownership of the mutex
						break;
					case WAIT_TIMEOUT:
						throw MutexError("CommandHandler(): Ban: Timed out waiting for socket list mutex: " + std::to_string(GetLastError()));
					case WAIT_ABANDONED: // Acquired ownership of an abandoned mutex
						throw MutexError("CommandHandler(): Ban: Got ownership of an abandoned socket list mutex: " + std::to_string(GetLastError()));
					default:
						throw MutexError("CommandHandler(): Ban: An error occured waiting for socket list mutex: " + std::to_string(GetLastError()));
				}
				const std::vector<Socket*> sockets = Socket::GetSocketsByIP(ip);
				for (Socket* socket : sockets)
				{
					const Socket::Address sockAddr = socket->GetAddress();
					socket->Disconnect();
					std::cout << "Disconnected socket \"" << sockAddr << "\"!" << std::endl;
				}
				if (!ReleaseMutex(Socket::s_socketListMutex))
					throw MutexError("CommandHandler(): Ban: Couldn't release socket list mutex: " + std::to_string(GetLastError()));

				// 2. Add IP to ban list
				if (g_config.bannedIPs.insert(ip).second)
				{
					g_config.Save();
					std::cout << "Added IP " << ip << " to ban list!" << std::endl;
				}
				else
				{
					std::cout << "IP " << ip << " is already in ban list." << std::endl;
				}
			}
			else if (cmd[0] == 'u' || cmd[0] == 'U')
			{
				std::string address;
				inputStr >> address;
				if (address.empty())
				{
					std::cout << "No client IP provided!" << std::endl;
					continue;
				}
				// Split string by ':' just in case a port was specified. In such case, it will be ignored
				std::stringstream addrStr(address);
				std::string ip;
				std::getline(addrStr, ip, ':');

				// Remove IP from ban list
				if (g_config.bannedIPs.erase(ip) > 0)
				{
					g_config.Save();
					std::cout << "Removed IP " << ip << " from ban list!" << std::endl;
				}
				else
				{
					std::cout << "IP " << ip << " is not in ban list." << std::endl;
				}
			}
			else if (cmd[0] == 'd' || cmd[0] == 'D')
			{
				unsigned int index = 0;
				inputStr >> index;
				if (!index)
				{
					std::cout << "No match ID provided!" << std::endl;
					continue;
				}

				const GUID guid = MatchManager::Get().DestroyMatch(index);
				std::cout << "Destroyed match with GUID " << guid << '!' << std::endl;
			}
			else if (StartsWith(cmd, "lc") || StartsWith(cmd, "LC") || StartsWith(cmd, "Lc") || StartsWith(cmd, "lC"))
			{
				std::cout
					<< std::endl
					<< std::left << std::setw(23) << "IP"
					<< std::setw(21) << "Connected since"
					<< std::setw(31) << "Type"
					<< std::setw(27) << "State"
					<< "Match Joined (GUID)" << std::endl
					<< std::string(140, '-') << std::endl;

				switch (WaitForSingleObject(Socket::s_socketListMutex, SOCKET_LIST_MUTEX_TIMEOUT_MS))
				{
					case WAIT_OBJECT_0: // Acquired ownership of the mutex
						break;
					case WAIT_TIMEOUT:
						throw MutexError("CommandHandler(): List clients: Timed out waiting for socket list mutex: " + std::to_string(GetLastError()));
					case WAIT_ABANDONED: // Acquired ownership of an abandoned mutex
						throw MutexError("CommandHandler(): List clients: Got ownership of an abandoned socket list mutex: " + std::to_string(GetLastError()));
					default:
						throw MutexError("CommandHandler(): List clients: An error occured waiting for socket list mutex: " + std::to_string(GetLastError()));
				}
				for (const Socket* socket : Socket::GetList())
				{
					const std::time_t connectionTime = socket->GetConnectionTime();
					std::tm localConnectionTime;
					localtime_s(&localConnectionTime, &connectionTime);

					std::cout
						<< std::setw(21) << socket->GetAddressString() << "  "
						<< std::put_time(&localConnectionTime, "%d/%m/%Y %H:%M:%S") << "  "
						<< std::setw(29) << Socket::TypeToString(socket->GetType()) << "  ";
					if (socket->GetPlayerSocket())
					{
						switch (socket->GetType())
						{
							case Socket::WIN7:
							{
								const Win7::PlayerSocket* player = static_cast<Win7::PlayerSocket*>(socket->GetPlayerSocket());
								std::cout << std::setw(25) << Win7::PlayerSocket::StateToString(player->GetState()) << "  ";
								if (player->GetMatch())
									std::cout << player->GetMatch()->GetGUID();
								else
									std::cout << "No";
								break;
							}
							case Socket::WINXP:
							{
								const WinXP::PlayerSocket* player = static_cast<WinXP::PlayerSocket*>(socket->GetPlayerSocket());
								std::cout << std::setw(25) << WinXP::PlayerSocket::StateToString(player->GetState()) << "  ";
								if (player->GetMatch())
									std::cout << player->GetMatch()->GetGUID();
								else
									std::cout << "No";
								break;
							}
						}
					}
					std::cout << std::endl;
				}
				if (!ReleaseMutex(Socket::s_socketListMutex))
					throw MutexError("CommandHandler(): List clients: Couldn't release socket list mutex: " + std::to_string(GetLastError()));

				std::cout << std::endl;
			}
			else if (StartsWith(cmd, "lm") || StartsWith(cmd, "LM") || StartsWith(cmd, "Lm") || StartsWith(cmd, "lM"))
			{
				std::cout
					<< std::endl
					<< std::left << std::setw(12) << "Index"
					<< std::setw(40) << "GUID"
					<< std::setw(21) << "Created on"
					<< std::setw(7) << "Type"
					<< std::setw(25) << "State"
					<< "Game" << std::endl
					<< std::string(130, '-') << std::endl;

				const auto matches = MatchManager::Get().AcquireMatches();
				for (const std::unique_ptr<Win7::Match>& match : matches.first)
				{
					const std::time_t creationTime = match->GetCreationTime();
					std::tm localCreationTime;
					localtime_s(&localCreationTime, &creationTime);

					std::cout
						<< std::right << std::setw(10) << match->GetIndex() << "  "
						<< std::left << match->GetGUID() << "  "
						<< std::put_time(&localCreationTime, "%d/%m/%Y %H:%M:%S") << "  "
						<< std::setw(7) << "Win7"
						<< std::setw(25) << Win7::Match::StateToString(match->GetState())
						<< Win7::Match::GameToNameString(match->GetGame())
						<< " (" << Win7::Match::LevelToString(match->GetLevel()) << ')'
						<< std::endl;
				}
				for (const std::unique_ptr<WinXP::Match>& match : matches.second)
				{
					const std::time_t creationTime = match->GetCreationTime();
					std::tm localCreationTime;
					localtime_s(&localCreationTime, &creationTime);

					std::cout
						<< std::right << std::setw(10) << match->GetIndex() << "  "
						<< std::left << match->GetGUID() << "  "
						<< std::put_time(&localCreationTime, "%d/%m/%Y %H:%M:%S") << "  "
						<< std::setw(7) << "WinXP"
						<< std::setw(25) << WinXP::Match::StateToString(match->GetState())
						<< WinXP::Match::GameToNameString(match->GetGame())
						<< " (" << WinXP::Match::SkillLevelToString(match->GetSkillLevel()) << ')'
						<< std::endl;
				}
				MatchManager::Get().FreeAcquiredMatches();

				std::cout << std::endl;
			}
			else if (StartsWith(cmd, "lb") || StartsWith(cmd, "LB") || StartsWith(cmd, "Lb") || StartsWith(cmd, "lB"))
			{
				std::cout << "Ban list:" << std::endl;
				if (g_config.bannedIPs.empty())
				{
					std::cout << "  - <empty>" << std::endl;
					continue;
				}
				for (const std::string& bannedIP : g_config.bannedIPs)
					std::cout << "  - " << bannedIP << std::endl;
			}
			else
			{
				std::cout << "Unknown command! Type '?' or 'h' for a list of commands." << std::endl;
			}
		}
		catch (const MutexError& fatalErr)
		{
			std::cout << "[FATAL!] " << fatalErr.what();
			SessionLog() << "[FATAL!] " << fatalErr.what();
			throw;
		}
		catch (const std::exception& err)
		{
			std::cout << err.what() << std::endl;
		}
	}

	return 0;
}
