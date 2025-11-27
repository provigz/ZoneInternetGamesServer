#pragma once

#include <string>
#include <set>

typedef unsigned short USHORT;

class Config final
{
public:
	Config();

	void Load(const std::string& file);
	void Save();

	static const std::initializer_list<std::pair<std::string, std::string>> s_optionKeys;

	std::string GetValue(const std::string& key) const;
	void SetValue(const std::string& key, const std::string& value);

private:
	std::string m_file;

public:
	USHORT port;

	std::string logsDirectory;

	USHORT numConnectionsPerIP;
	bool enableHTTP;

	bool skipLevelMatching; // When searching for a lobby, don't take the match level into account.
	bool allowSinglePlayer; // Allow matches, which support computer players, to exist with only one real player.
	bool disableXPAdBanner;

	std::set<std::string> bannedIPs;
};

extern Config g_config;
