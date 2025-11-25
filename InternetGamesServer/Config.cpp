#include "Config.hpp"

#include <iostream>
#include <stdexcept>

#include <tinyxml2.h>

#include "Util.hpp"

#define DEFAULT_PORT 28805
#define DEFAULT_LOGS_DIRECTORY "InternetGamesServer_logs"

Config::Config() :
	m_file(),
	port(DEFAULT_PORT),
	logsDirectory(DEFAULT_LOGS_DIRECTORY),
	numConnectionsPerIP(0),
	skipLevelMatching(false),
	allowSinglePlayer(true),
	disableXPAdBanner(false),
	bannedIPs()
{
}

void
Config::Load(const std::string& file)
{
	m_file = file;

	tinyxml2::XMLDocument doc;
	const tinyxml2::XMLError err = doc.LoadFile(file.c_str());
	if (err != tinyxml2::XML_SUCCESS)
	{
		std::cout << "[CONFIG] WARNING: Couldn't load config file \"" << m_file << "\": "
			<< std::string(tinyxml2::XMLDocument::ErrorIDToName(err))
			<< ": " + std::string(doc.ErrorStr()) << std::endl;
		Save();
		return;
	}

	tinyxml2::XMLElement* elRoot = doc.RootElement();
	if (!elRoot)
		throw std::runtime_error("No root XML element!");

	{
		tinyxml2::XMLElement* elPort = elRoot->FirstChildElement("Port");
		if (elPort && elPort->GetText())
		{
			try
			{
				port = static_cast<USHORT>(std::stoi(elPort->GetText()));
			}
			catch (const std::exception& err)
			{
				std::cout << "[CONFIG] Invalid \"Port\" number: " << err.what() << std::endl;
			}
		}
	}
	{
		tinyxml2::XMLElement* elLogsDirectory = elRoot->FirstChildElement("LogsDirectory");
		if (elLogsDirectory && elLogsDirectory->GetText())
			logsDirectory = elLogsDirectory->GetText();
	}
	{
		tinyxml2::XMLElement* elNumConnectionsPerIP = elRoot->FirstChildElement("NumConnectionsPerIP");
		if (elNumConnectionsPerIP && elNumConnectionsPerIP->GetText())
		{
			try
			{
				numConnectionsPerIP = static_cast<USHORT>(std::stoi(elNumConnectionsPerIP->GetText()));
			}
			catch (const std::exception& err)
			{
				std::cout << "[CONFIG] Invalid \"NumConnectionsPerIP\" number: " << err.what() << std::endl;
			}
		}
	}
	{
		tinyxml2::XMLElement* elSkipLevelMatching = elRoot->FirstChildElement("SkipLevelMatching");
		if (elSkipLevelMatching && elSkipLevelMatching->GetText())
			skipLevelMatching = *elSkipLevelMatching->GetText() == '1';
	}
	{
		tinyxml2::XMLElement* elAllowSinglePlayer = elRoot->FirstChildElement("AllowSinglePlayer");
		if (elAllowSinglePlayer && elAllowSinglePlayer->GetText())
			allowSinglePlayer = *elAllowSinglePlayer->GetText() == '1';
	}
	{
		tinyxml2::XMLElement* elDisableXPAdBanner = elRoot->FirstChildElement("DisableXPAdBanner");
		if (elDisableXPAdBanner && elDisableXPAdBanner->GetText())
			disableXPAdBanner = *elDisableXPAdBanner->GetText() == '1';
	}
	{
		tinyxml2::XMLElement* elBannedIPs = elRoot->FirstChildElement("BannedIPs");
		if (elBannedIPs)
		{
			for (tinyxml2::XMLElement* elIP = elBannedIPs->FirstChildElement("IP");
				elIP; elIP = elIP->NextSiblingElement("IP"))
			{
				if (elIP->GetText())
					bannedIPs.insert(elIP->GetText());
			}
		}
	}
}

void
Config::Save()
{
	tinyxml2::XMLDocument doc;

	tinyxml2::XMLElement* elRoot = doc.NewElement("Config");
	doc.InsertFirstChild(elRoot);

	NewElementWithText(elRoot, "Port", port);
	NewElementWithText(elRoot, "LogsDirectory", logsDirectory.c_str());
	NewElementWithText(elRoot, "NumConnectionsPerIP", numConnectionsPerIP);
	NewElementWithText(elRoot, "SkipLevelMatching", skipLevelMatching ? "1" : "0");
	NewElementWithText(elRoot, "AllowSinglePlayer", allowSinglePlayer ? "1" : "0");
	NewElementWithText(elRoot, "DisableXPAdBanner", disableXPAdBanner ? "1" : "0");

	tinyxml2::XMLElement* elBannedIPs = doc.NewElement("BannedIPs");
	for (const std::string& ip : bannedIPs)
		NewElementWithText(elBannedIPs, "IP", ip.c_str());
	elRoot->InsertEndChild(elBannedIPs);

	const tinyxml2::XMLError err = doc.SaveFile(m_file.c_str());
	if (err != tinyxml2::XML_SUCCESS)
		SessionLog() << "[CONFIG] WARNING: Couldn't save config file \"" << m_file << "\": "
			<< std::string(tinyxml2::XMLDocument::ErrorIDToName(err))
			<< ": " + std::string(doc.ErrorStr()) << std::endl;
}


const std::initializer_list<std::pair<std::string, std::string>> Config::s_optionKeys = {
	{ "port", "The port the server should be hosted on. Requires restart to apply. (Default: 28805)" },
	{ "logdir", "The directory where log files are written to. Set to 0 to disable logging. Requires restart to fully apply. (Default: \"InternetGamesServer_logs\")" },
	{ "numconnsip", "Limits the number of connections allowed from a given IP address. Maximum is 65535. 0 signifies no limit. NOTE: Make this 0 or higher than 1 to keep XP ad banner functionality! (Default: 0)" },
	{ "skiplevel", "Do not match players in matches based on skill level. Value can only be 0 or 1. (Default: 0)" },
	{ "singleplayer", "Allow matches, which support computer players, to exist with only one real player. (Default: 1)" },
	{ "disablead", "Prevent the server from responding to ad banner requests from Windows XP games with a custom \"Powered by ZoneInternetGamesServer\" banner. Value can only be 0 or 1. (Default: 0)" }
};

std::string
Config::GetValue(const std::string& key) const
{
	if (key == "port")
		return std::to_string(port);
	if (key == "logdir")
		return logsDirectory;
	if (key == "numconnsip")
		return std::to_string(numConnectionsPerIP);
	if (key == "skiplevel")
		return skipLevelMatching ? "1" : "0";
	if (key == "singleplayer")
		return allowSinglePlayer ? "1" : "0";
	if (key == "disablead")
		return disableXPAdBanner ? "1" : "0";
	throw std::runtime_error("Invalid option key!");
}

void
Config::SetValue(const std::string& key, const std::string& value)
{
#define CONFIG_SET_BOOL_VALUE(var) var = (value == "1" ? true : (value == "0" ? false : var))

	if (key == "port")
	{
		try
		{
			port = static_cast<USHORT>(std::stoi(value));
		}
		catch (const std::exception& err)
		{
			throw std::runtime_error("Invalid \"port\" number: " + std::string(err.what()));
		}
	}
	else if (key == "logdir")
	{
		if (value == "0")
		{
			logsDirectory.clear();
			return;
		}
		CreateNestedDirectories(value);
		logsDirectory = value;
	}
	else if (key == "numconnsip")
	{
		try
		{
			numConnectionsPerIP = static_cast<USHORT>(std::stoi(value));
		}
		catch (const std::exception& err)
		{
			throw std::runtime_error("Invalid \"numconnsip\" number: " + std::string(err.what()));
		}
	}
	else if (key == "skiplevel")
		CONFIG_SET_BOOL_VALUE(skipLevelMatching);
	else if (key == "singleplayer")
		CONFIG_SET_BOOL_VALUE(allowSinglePlayer);
	else if (key == "disablead")
		CONFIG_SET_BOOL_VALUE(disableXPAdBanner);
	else
		throw std::runtime_error("Invalid option key!");

#undef CONFIG_SET_BOOL_VALUE

	Save();
}


Config g_config;
