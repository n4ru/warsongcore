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

#include "WSGLobbyService.h"
#include "AccountMgr.h"
#include "BattlegroundMgr.h"
#include "BattlegroundQueue.h"
#include "Battleground.h"
#include "BattlegroundWS.h"
#include "World.h"
#include "WorldConfig.h"
#include "Log.h"
#include "DatabaseEnv.h"
#include "Util.h"
#include "DBCStores.h"
#include "MapMgr.h"
#include "Map.h"
#include <sstream>
#include <random>
#include <algorithm>
#include <regex>

uint32 WSGLobbySession::GetAllianceCount() const
{
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(participantsMutex));
    return std::count_if(participants.begin(), participants.end(),
        [](const LobbyParticipant& p) { return p.faction == TEAM_ALLIANCE; });
}

uint32 WSGLobbySession::GetHordeCount() const
{
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(participantsMutex));
    return std::count_if(participants.begin(), participants.end(),
        [](const LobbyParticipant& p) { return p.faction == TEAM_HORDE; });
}

bool WSGLobbySession::CanStart() const
{
    // WSC-CL
    // Fixed deadlock - don't lock here as caller should already hold the lock
    if (status != LobbyStatus::PENDING)
        return false;
        
    uint32 totalPlayers = participants.size();
    if (totalPlayers < sWSGLobbyService->GetMinPlayers())
        return false;
        
    // Count factions directly without calling GetAllianceCount/GetHordeCount
    // to avoid nested mutex locking
    uint32 allianceCount = 0;
    uint32 hordeCount = 0;
    for (const auto& p : participants)
    {
        if (p.faction == TEAM_ALLIANCE)
            allianceCount++;
        else if (p.faction == TEAM_HORDE)
            hordeCount++;
    }
    
    return allianceCount > 0 && hordeCount > 0;
}

bool WSGLobbySession::IsExpired() const
{
    if (status != LobbyStatus::PENDING)
        return false;
        
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - createdAt).count();
    return elapsed > sWSGLobbyService->GetLobbyTimeout();
}

WSGLobbyService::WSGLobbyService()
    : _enabled(false), _maxLobbies(10), _lobbyTimeout(3600), 
      _minPlayers(2), _maxPlayers(20), _autoBalance(true),
      _nextInstanceId(100000) // Start with high ID to avoid conflicts
{
    LoadConfig();
}

WSGLobbyService::~WSGLobbyService()
{
}

WSGLobbyService* WSGLobbyService::instance()
{
    static WSGLobbyService instance;
    return &instance;
}

void WSGLobbyService::LoadConfig()
{
    _enabled = sWorld->getBoolConfig(CONFIG_WSG_LOBBY_ENABLE);
    _maxLobbies = sWorld->getIntConfig(CONFIG_WSG_LOBBY_MAX_LOBBIES);
    _lobbyTimeout = sWorld->getIntConfig(CONFIG_WSG_LOBBY_TIMEOUT);
    _minPlayers = sWorld->getIntConfig(CONFIG_WSG_LOBBY_MIN_PLAYERS);
    _maxPlayers = sWorld->getIntConfig(CONFIG_WSG_LOBBY_MAX_PLAYERS);
    _autoBalance = false; // Disabled - players choose their faction based on race
    
    LOG_INFO("server.worldserver", "WSG Lobby Service: Enabled={}, MaxLobbies={}, Timeout={}s, MinPlayers={}, MaxPlayers={}",
        _enabled, _maxLobbies, _lobbyTimeout, _minPlayers, _maxPlayers);
}

std::string WSGLobbyService::GenerateLobbyId()
{
    static const char* chars = "abcdefghijklmnopqrstuvwxyz0123456789";
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, 35);
    
    std::string id;
    for (int i = 0; i < 8; ++i)
    {
        id += chars[dis(gen)];
        if (i == 3)
            id += '-';
    }
    return id;
}

std::string WSGLobbyService::CreateLobby(const std::string& leaderName, TeamId faction, const std::string& characterData)
{
    if (!_enabled)
    {
        LOG_ERROR("server.worldserver", "WSG Lobby Service is disabled");
        return "";
    }
    
    std::lock_guard<std::mutex> lock(_lobbiesMutex);
    
    if (_lobbies.size() >= _maxLobbies)
    {
        LOG_ERROR("server.worldserver", "Maximum number of lobbies reached");
        return "";
    }
    
    std::string lobbyId = GenerateLobbyId();
    while (_lobbies.find(lobbyId) != _lobbies.end())
        lobbyId = GenerateLobbyId();
    
    auto lobby = std::make_unique<WSGLobbySession>();
    lobby->id = lobbyId;
    lobby->leaderName = leaderName;
    lobby->createdAt = std::chrono::steady_clock::now();
    lobby->wsgInstanceId = 0;
    lobby->status = LobbyStatus::PENDING;
    
    // Add leader as first participant
    LobbyParticipant leader;
    leader.characterName = leaderName;
    leader.faction = faction;
    leader.characterData = characterData;
    leader.accountId = 0;
    leader.joinedAt = lobby->createdAt;
    lobby->participants.push_back(leader);
    
    _lobbies[lobbyId] = std::move(lobby);
    
    LOG_INFO("server.worldserver", "WSG Lobby created: ID={}, Leader={}, Faction={}", 
        lobbyId, leaderName, faction == TEAM_ALLIANCE ? "Alliance" : "Horde");
    
    return lobbyId;
}

bool WSGLobbyService::JoinLobby(const std::string& lobbyId, const std::string& characterName,
                                TeamId faction, const std::string& characterData)
{
    std::lock_guard<std::mutex> lock(_lobbiesMutex);

    // WSC-CL - Debug logging
    LOG_INFO("server.worldserver", "JoinLobby called: lobbyId={}, characterName={}", lobbyId, characterName);
    LOG_INFO("server.worldserver", "Current lobbies count: {}", _lobbies.size());
    for (const auto& [id, lobby] : _lobbies)
    {
        LOG_INFO("server.worldserver", "  Existing lobby: {}", id);
    }

    auto it = _lobbies.find(lobbyId);
    if (it == _lobbies.end())
    {
        LOG_ERROR("server.worldserver", "Lobby join not found: {}", lobbyId);
        return false;
    }
    
    auto& lobby = it->second;
    
    if (lobby->status != LobbyStatus::PENDING)
    {
        LOG_ERROR("server.worldserver", "Lobby {} is not accepting new players", lobbyId);
        return false;
    }
    
    std::lock_guard<std::mutex> lobbyLock(lobby->participantsMutex);
    
    // Check if player already in lobby
    auto existing = std::find_if(lobby->participants.begin(), lobby->participants.end(),
        [&characterName](const LobbyParticipant& p) { return p.characterName == characterName; });
    
    if (existing != lobby->participants.end())
    {
        LOG_ERROR("server.worldserver", "Player {} already in lobby {}", characterName, lobbyId);
        return false;
    }
    
    if (lobby->participants.size() >= _maxPlayers)
    {
        LOG_ERROR("server.worldserver", "Lobby {} is full", lobbyId);
        return false;
    }
    
    // Use the faction that the player selected (based on their race)
    LobbyParticipant participant;
    participant.characterName = characterName;
    participant.faction = faction;
    participant.characterData = characterData;
    participant.accountId = 0;
    participant.joinedAt = std::chrono::steady_clock::now();
    
    lobby->participants.push_back(participant);
    
    LOG_INFO("server.worldserver", "Player {} joined lobby {} as {}", 
        characterName, lobbyId, faction == TEAM_ALLIANCE ? "Alliance" : "Horde");
    
    return true;
}


bool WSGLobbyService::StartLobby(const std::string& lobbyId, const std::string& requestingPlayer)
{
    // WSC-CL - Fixed deadlock by properly managing locks
    // First, validate and collect data while holding lock
    std::vector<LobbyParticipant> participantsCopy;
    std::string leaderName;
    
    {
        std::lock_guard<std::mutex> lock(_lobbiesMutex);
        
        auto it = _lobbies.find(lobbyId);
        if (it == _lobbies.end())
        {
            LOG_ERROR("server.worldserver", "Lobby {} not found", lobbyId);
            return false;
        }
        
        auto& lobby = it->second;
        leaderName = lobby->leaderName;
        
        // Only leader can start
        if (leaderName != requestingPlayer)
        {
            LOG_ERROR("server.worldserver", "Only lobby leader {} can start lobby {}", leaderName, lobbyId);
            return false;
        }
        
        // Check if can start while holding participants lock
        {
            std::lock_guard<std::mutex> lobbyLock(lobby->participantsMutex);
            if (!lobby->CanStart())
            {
                LOG_ERROR("server.worldserver", "Lobby {} cannot start yet", lobbyId);
                return false;
            }
            // Copy participants for account creation
            participantsCopy = lobby->participants;
        }
    }
    
    // WSC-CL - Account creation is now handled during character creation
    LOG_INFO("server.worldserver", "Starting lobby {} with {} participants", lobbyId, participantsCopy.size());
    
    // Create WSG instance
    uint32 instanceId = CreateWSGInstanceForLobby(lobbyId);
    if (!instanceId)
    {
        LOG_ERROR("server.worldserver", "Failed to create WSG instance for lobby {}", lobbyId);
        return false;
    }
    
    // Update lobby status and participant account IDs
    {
        std::lock_guard<std::mutex> lock(_lobbiesMutex);
        auto it = _lobbies.find(lobbyId);
        if (it == _lobbies.end())
        {
            LOG_ERROR("server.worldserver", "Lobby {} disappeared during start", lobbyId);
            return false;
        }
        
        auto& lobby = it->second;
        lobby->wsgInstanceId = instanceId;
        lobby->status = LobbyStatus::STARTED;
        lobby->startedAt = std::chrono::steady_clock::now();
        
        // Participants will get accounts created during character creation
    }
    
    LOG_INFO("server.worldserver", "Lobby {} started with WSG instance {}", lobbyId, instanceId);
    
    return true;
}

WSGLobbySession* WSGLobbyService::GetLobby(const std::string& lobbyId)
{
    std::lock_guard<std::mutex> lock(_lobbiesMutex);
    
    auto it = _lobbies.find(lobbyId);
    if (it != _lobbies.end())
        return it->second.get();
        
    return nullptr;
}

std::vector<std::string> WSGLobbyService::GetActiveLobbyIds() const
{
    std::lock_guard<std::mutex> lock(_lobbiesMutex);
    
    std::vector<std::string> ids;
    for (const auto& [id, lobby] : _lobbies)
    {
        if (lobby->status == LobbyStatus::PENDING)
            ids.push_back(id);
    }
    return ids;
}

std::string WSGLobbyService::GetLobbyStatusJson(const std::string& lobbyId)
{
    // WSC-CL
    // Fixed deadlock by properly managing mutex locks
    std::lock_guard<std::mutex> lock(_lobbiesMutex);
    
    auto it = _lobbies.find(lobbyId);
    if (it == _lobbies.end())
        return "{\"error\":\"Lobby not found\"}";
    
    auto& lobby = it->second;
    
    // Gather all data while holding the lobby's participantsMutex
    std::string id = lobby->id;
    std::string leaderName = lobby->leaderName;
    LobbyStatus status = lobby->status;
    uint32 wsgInstanceId = lobby->wsgInstanceId;
    
    uint32 allianceCount = 0;
    uint32 hordeCount = 0;
    bool canStart = false;
    std::vector<LobbyParticipant> participantsCopy;
    
    {
        std::lock_guard<std::mutex> participantsLock(lobby->participantsMutex);
        
        // Count factions
        for (const auto& p : lobby->participants)
        {
            if (p.faction == TEAM_ALLIANCE)
                allianceCount++;
            else if (p.faction == TEAM_HORDE)
                hordeCount++;
        }
        
        // Check if can start
        canStart = (status == LobbyStatus::PENDING && 
                   lobby->participants.size() >= _minPlayers &&
                   allianceCount > 0 && hordeCount > 0);
        
        // Copy participants
        participantsCopy = lobby->participants;
    }
    
    // Build JSON response outside of locks
    std::stringstream json;
    json << "{";
    json << "\"id\":\"" << id << "\",";
    json << "\"leader\":\"" << leaderName << "\",";
    json << "\"status\":\"" << (status == LobbyStatus::PENDING ? "pending" :
                                status == LobbyStatus::STARTED ? "started" :
                                status == LobbyStatus::COMPLETED ? "completed" : "expired") << "\",";
    json << "\"wsg_instance_id\":" << wsgInstanceId << ",";
    json << "\"alliance_count\":" << allianceCount << ",";
    json << "\"horde_count\":" << hordeCount << ",";
    json << "\"can_start\":" << (canStart ? "true" : "false") << ",";
    json << "\"participants\":[";
    
    bool first = true;
    for (const auto& p : participantsCopy)
    {
        if (!first) json << ",";
        json << "{";
        json << "\"name\":\"" << p.characterName << "\",";
        json << "\"faction\":\"" << (p.faction == TEAM_ALLIANCE ? "Alliance" : "Horde") << "\",";
        json << "\"account_id\":" << p.accountId;
        json << "}";
        first = false;
    }
    
    json << "]}";
    return json.str();
}

void WSGLobbyService::CleanupExpiredLobbies()
{
    std::lock_guard<std::mutex> lock(_lobbiesMutex);
    
    auto it = _lobbies.begin();
    while (it != _lobbies.end())
    {
        if (it->second->IsExpired())
        {
            LOG_INFO("server.worldserver", "Removing expired lobby {}", it->first);
            
            // WSC-CL - Clean up accounts and characters for expired lobbies
            std::lock_guard<std::mutex> participantsLock(it->second->participantsMutex);
            for (const auto& participant : it->second->participants)
            {
                if (!participant.characterName.empty())
                {
                    // Get the account ID for this character
                    std::string username = participant.characterName;
                    std::transform(username.begin(), username.end(), username.begin(), ::tolower);
                    uint32 accountId = AccountMgr::GetId(username);
                    
                    if (accountId > 0)
                    {
                        // Delete characters first
                        CharacterDatabase.Execute("DELETE FROM character_account_data WHERE guid IN (SELECT guid FROM characters WHERE account = {})", accountId);
                        CharacterDatabase.Execute("DELETE FROM character_action WHERE guid IN (SELECT guid FROM characters WHERE account = {})", accountId);
                        CharacterDatabase.Execute("DELETE FROM character_aura WHERE guid IN (SELECT guid FROM characters WHERE account = {})", accountId);
                        CharacterDatabase.Execute("DELETE FROM character_homebind WHERE guid IN (SELECT guid FROM characters WHERE account = {})", accountId);
                        CharacterDatabase.Execute("DELETE FROM character_instance WHERE guid IN (SELECT guid FROM characters WHERE account = {})", accountId);
                        CharacterDatabase.Execute("DELETE FROM character_inventory WHERE guid IN (SELECT guid FROM characters WHERE account = {})", accountId);
                        CharacterDatabase.Execute("DELETE FROM item_instance WHERE owner_guid IN (SELECT guid FROM characters WHERE account = {})", accountId);
                        CharacterDatabase.Execute("DELETE FROM character_pet WHERE owner IN (SELECT guid FROM characters WHERE account = {})", accountId);
                        CharacterDatabase.Execute("DELETE FROM character_queststatus WHERE guid IN (SELECT guid FROM characters WHERE account = {})", accountId);
                        CharacterDatabase.Execute("DELETE FROM character_queststatus_rewarded WHERE guid IN (SELECT guid FROM characters WHERE account = {})", accountId);
                        CharacterDatabase.Execute("DELETE FROM character_reputation WHERE guid IN (SELECT guid FROM characters WHERE account = {})", accountId);
                        CharacterDatabase.Execute("DELETE FROM character_spell WHERE guid IN (SELECT guid FROM characters WHERE account = {})", accountId);
                        CharacterDatabase.Execute("DELETE FROM character_spell_cooldown WHERE guid IN (SELECT guid FROM characters WHERE account = {})", accountId);
                        CharacterDatabase.Execute("DELETE FROM character_stats WHERE guid IN (SELECT guid FROM characters WHERE account = {})", accountId);
                        CharacterDatabase.Execute("DELETE FROM character_skills WHERE guid IN (SELECT guid FROM characters WHERE account = {})", accountId);
                        CharacterDatabase.Execute("DELETE FROM character_glyphs WHERE guid IN (SELECT guid FROM characters WHERE account = {})", accountId);
                        CharacterDatabase.Execute("DELETE FROM character_talent WHERE guid IN (SELECT guid FROM characters WHERE account = {})", accountId);
                        CharacterDatabase.Execute("DELETE FROM characters WHERE account = {}", accountId);
                        
                        // Delete the account itself
                        LoginDatabase.Execute("DELETE FROM account WHERE id = {}", accountId);
                        
                        LOG_INFO("server.worldserver", "Cleaned up account {} (ID: {}) and character {} from expired lobby",
                                 username, accountId, participant.characterName);
                    }
                }
            }
            
            it = _lobbies.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

uint32 WSGLobbyService::CreateWSGInstanceForLobby(const std::string& lobbyId)
{
    // WSC-CL - Modified to create WSG without relying on PvPDifficulty data

    // Get the average level of participants to determine bracket
    uint32 avgLevel = 19; // Default to 10-19 bracket
    uint32 minLevel = 10;
    uint32 maxLevel = 80;
    uint32 allianceCount = 0;
    uint32 hordeCount = 0;
    std::vector<LobbyParticipant> participantsCopy;

    {
        std::lock_guard<std::mutex> lock(_lobbiesMutex);
        auto it = _lobbies.find(lobbyId);
        if (it != _lobbies.end())
        {
            auto& lobby = it->second;
            std::lock_guard<std::mutex> participantsLock(lobby->participantsMutex);

            // Parse character data to get levels
            uint32 totalLevel = 0;
            uint32 playerCount = 0;
            minLevel = 80;
            maxLevel = 1;

            for (const auto& participant : lobby->participants)
            {
                // Extract level from character data JSON
                // For now, default to level 19 if not specified
                uint32 level = 19;

                // Simple JSON parsing for level
                std::regex levelPattern("\"level\"\\s*:\\s*(\\d+)");
                std::smatch matches;
                if (std::regex_search(participant.characterData, matches, levelPattern))
                {
                    level = std::stoi(matches[1].str());
                }

                totalLevel += level;
                playerCount++;
                minLevel = std::min(minLevel, level);
                maxLevel = std::max(maxLevel, level);

                // Count faction members
                if (participant.faction == TEAM_ALLIANCE)
                    allianceCount++;
                else if (participant.faction == TEAM_HORDE)
                    hordeCount++;
            }

            if (playerCount > 0)
            {
                avgLevel = totalLevel / playerCount;
            }

            // Copy participants for later use
            participantsCopy = lobby->participants;
        }
    }
    
    LOG_INFO("server.worldserver", "Creating WSG instance for levels {}-{} (avg {})", minLevel, maxLevel, avgLevel);
    
    // WSC-CL - Create battleground directly using template
    // Get the template battleground
    Battleground* bg_template = sBattlegroundMgr->GetBattlegroundTemplate(BATTLEGROUND_WS);
    if (!bg_template)
    {
        LOG_ERROR("server.worldserver", "Failed to get WSG template");
        return 0;
    }
    
    // Create a new instance directly
    Battleground* bg = new BattlegroundWS(*dynamic_cast<BattlegroundWS*>(bg_template));
    if (!bg)
    {
        LOG_ERROR("server.worldserver", "Failed to create WSG battleground instance");
        return 0;
    }
    
    // Set up the battleground instance
    uint32 instanceId = sMapMgr->GenerateInstanceId();
    bg->SetInstanceID(instanceId);
    bg->SetLevelRange(minLevel, maxLevel);
    bg->SetRated(false);
    bg->SetArenaType(0);
    bg->SetBgTypeID(BATTLEGROUND_WS);
    bg->SetRandomTypeID(BATTLEGROUND_WS);
    bg->SetName("Warsong Gulch");
    bg->SetMapId(489); // WSG map ID
    
    // Create a client visible instance ID
    uint32 clientInstanceId = _nextInstanceId++;
    bg->SetClientInstanceID(clientInstanceId);
    
    // Initialize the battleground
    bg->Init();
    
    // Set up the battleground properly
    bg->SetupBattleground();
    
    // WSC-CL - Keep battleground in waiting state until players join
    // The normal timer will start when all queued players have joined
    bg->SetStatus(STATUS_WAIT_JOIN);
    bg->SetStartDelayTime(BG_START_DELAY_2M);  // 2 minute countdown after all join
    bg->SetDelayedStart(false);
    
    // Add to battleground manager so it can be found later
    sBattlegroundMgr->AddBattleground(bg);

    // WSC-CL - Setup lobby tracking for the battleground
    std::string lobbyLeader = "";
    std::vector<std::pair<std::string, TeamId>> lobbyPlayers;
    
    // Extract lobby leader and prepare player list
    {
        std::lock_guard<std::mutex> lock(_lobbiesMutex);
        auto it = _lobbies.find(lobbyId);
        if (it != _lobbies.end())
        {
            auto& lobby = it->second;
            std::lock_guard<std::mutex> participantsLock(lobby->participantsMutex);
            
            // Get the lobby leader (first participant)
            if (!lobby->participants.empty())
            {
                lobbyLeader = lobby->participants[0].characterName;
            }
            
            // Prepare player list for battleground tracking
            for (const auto& participant : lobby->participants)
            {
                lobbyPlayers.emplace_back(participant.characterName, participant.faction);
            }
        }
    }
    
    // Setup the lobby battleground with tracking information
    if (!lobbyLeader.empty() && !lobbyPlayers.empty())
    {
        bg->SetupLobbyBG(lobbyId, lobbyLeader, lobbyPlayers);
        LOG_INFO("server.worldserver", "Setup lobby BG {} with leader {} and {} players", 
                 lobbyId, lobbyLeader, lobbyPlayers.size());
    }

    // WSC-CL - Set minimum players to 1v1 for custom lobbies
    // This allows the BG to start with any number of players
    bg->SetMinPlayersPerTeam(1);
    bg->SetMaxPlayersPerTeam(10); // WSG is 10v10 max

    // WSC-CL - The map will be created when the first player joins
    // via the modified _ProcessJoin function in Battleground.cpp
    
    // Don't pre-reserve slots - let players join as they come online
    // The BG will start its countdown when at least 1v1 is present

    LOG_INFO("server.worldserver", "Created WSG instance {} for lobby with {} Alliance and {} Horde expected",
             instanceId, allianceCount, hordeCount);

    return instanceId;
}

bool WSGLobbyService::AssignPlayerToWSGInstance(uint32 accountId, uint32 characterGuid, uint32 instanceId)
{
    // This will be called during character import to directly assign the player to the WSG instance
    // The actual implementation will be done in CharacterWebService when importing the character
    
    // For now, just log the assignment - the actual database update will be handled elsewhere
    LOG_INFO("server.worldserver", "Assigning player {} to WSG instance {}", characterGuid, instanceId);
    
    return true;
}

// WSC-CL - This function is now integrated into StartLobby to avoid deadlock
// Keeping for backward compatibility but marking as deprecated
std::vector<WSGLobbyService::AccountCredentials> WSGLobbyService::CreateAccountsForLobby(const std::string& lobbyId)
{
    std::vector<AccountCredentials> accounts;
    LOG_WARN("server.worldserver", "CreateAccountsForLobby called directly - this is deprecated");
    return accounts;
}