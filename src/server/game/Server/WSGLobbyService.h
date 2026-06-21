/*
 * This file is part of the AzerothCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Affero General Public License as published by the
 * Free Software Foundation; either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef WSG_LOBBY_SERVICE_H
#define WSG_LOBBY_SERVICE_H

#include "Common.h"
#include "SharedDefines.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <chrono>
#include <mutex>

enum class LobbyStatus
{
    PENDING,
    STARTED,
    COMPLETED,
    EXPIRED
};

struct LobbyParticipant
{
    std::string characterName;
    TeamId faction;
    std::string characterData; // JSON data for character import
    uint32 accountId;
    std::chrono::steady_clock::time_point joinedAt;
};

struct WSGLobbySession
{
    std::string id;
    std::string leaderName;
    std::chrono::steady_clock::time_point createdAt;
    std::chrono::steady_clock::time_point startedAt;
    uint32 wsgInstanceId;
    LobbyStatus status;
    std::vector<LobbyParticipant> participants;
    std::mutex participantsMutex;
    
    uint32 GetAllianceCount() const;
    uint32 GetHordeCount() const;
    bool CanStart() const;
    bool IsExpired() const;
};

class WSGLobbyService
{
public:
    static WSGLobbyService* instance();

    // Lobby management
    std::string CreateLobby(const std::string& leaderName, TeamId faction, const std::string& characterData);
    bool JoinLobby(const std::string& lobbyId, const std::string& characterName, TeamId faction, const std::string& characterData);
    bool StartLobby(const std::string& lobbyId, const std::string& requestingPlayer);
    WSGLobbySession* GetLobby(const std::string& lobbyId);
    std::vector<std::string> GetActiveLobbyIds() const;
    
    // Lobby status
    std::string GetLobbyStatusJson(const std::string& lobbyId);
    void CleanupExpiredLobbies();
    
    // WSG instance management
    uint32 CreateWSGInstanceForLobby(const std::string& lobbyId);
    bool AssignPlayerToWSGInstance(uint32 accountId, uint32 characterGuid, uint32 instanceId);
    
    // Account creation
    struct AccountCredentials
    {
        std::string username;
        std::string password;
        uint32 accountId;
    };
    std::vector<AccountCredentials> CreateAccountsForLobby(const std::string& lobbyId);
    
    // Configuration
    void LoadConfig();
    bool IsEnabled() const { return _enabled; }
    uint32 GetMaxLobbies() const { return _maxLobbies; }
    uint32 GetLobbyTimeout() const { return _lobbyTimeout; }
    uint32 GetMinPlayers() const { return _minPlayers; }
    uint32 GetMaxPlayers() const { return _maxPlayers; }
    
private:
    WSGLobbyService();
    ~WSGLobbyService();
    
    std::string GenerateLobbyId();
    
    std::unordered_map<std::string, std::unique_ptr<WSGLobbySession>> _lobbies;
    mutable std::mutex _lobbiesMutex;
    
    // Configuration
    bool _enabled;
    uint32 _maxLobbies;
    uint32 _lobbyTimeout; // in seconds
    uint32 _minPlayers;
    uint32 _maxPlayers;
    bool _autoBalance;
    
    // Instance ID tracking
    std::atomic<uint32> _nextInstanceId;
};

#define sWSGLobbyService WSGLobbyService::instance()

#endif // WSG_LOBBY_SERVICE_H