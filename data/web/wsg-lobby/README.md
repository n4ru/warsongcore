# WSG Lobby System

## Overview
The WSG Lobby System allows players to create and join Warsong Gulch battleground lobbies through a web interface. Players can import their characters, form teams, and start private WSG matches.

## Components

### 1. Core Server Components
- **WSGLobbyService** (`src/server/game/Server/WSGLobbyService.cpp/h`) - Main service handling lobby logic
- **CharacterWebService** - Handles character imports and account creation
- **Battleground Integration** - Creates and manages WSG instances for lobbies

### 2. Web Interface
- **Location**: `data/web/wsg-lobby/`
- **Main Files**:
  - `index.html` - Landing page for creating/joining lobbies
  - `lobby.html` - Lobby waiting room interface
  - `lobby.js` - Client-side JavaScript logic
  - `server.js` - Express server for hosting the web interface
  - `styles.css` - Styling for the web interface

### 3. Database Schema
Two main tables track lobby data:
- `wsg_lobby_sessions` - Stores lobby session information
- `wsg_lobby_participants` - Stores participant data for each lobby
- `wsg_lobby_players` - Tracks players who should join WSG on login

## Setup Instructions

### 1. Database Setup
Run the SQL update scripts:
```sql
-- Run in characters database
source data/sql/updates/db_characters/2025_09_10_00_wsg_lobby.sql
source data/sql/updates/db_characters/2025_09_13_00_wsg_lobby_players.sql
```

### 2. Server Configuration
Edit `worldserver.conf` to configure the WSG Lobby:
```conf
# Enable WSG Lobby System
WSGLobby.Enable = 1

# Maximum concurrent lobbies
WSGLobby.MaxLobbies = 10

# Lobby timeout in seconds (1 hour default)
WSGLobby.LobbyTimeout = 3600

# Player limits
WSGLobby.MinPlayers = 2
WSGLobby.MaxPlayers = 20
```

### 3. Web Server Setup
1. Navigate to the web directory:
   ```bash
   cd data/web/wsg-lobby
   ```

2. Install dependencies:
   ```bash
   npm install
   ```

3. Start the web server:
   ```bash
   node server.js
   ```

4. Access the interface at: http://localhost:3000

### 4. WorldServer API
The worldserver must be running with the REST API enabled on port 8080 for the web interface to communicate with the game server.

## How to Use

### Creating a Lobby
1. Visit http://localhost:3000
2. Click "Create Lobby"
3. Paste your character export data (JSON format)
4. The system will create a lobby and provide a shareable link

### Joining a Lobby
1. Use the shared lobby link OR
2. Visit http://localhost:3000 and click "Join Lobby"
3. Enter the lobby ID
4. Paste your character export data
5. Wait for the lobby leader to start the game

### Starting a Match
1. The lobby leader can start the match once minimum players have joined
2. Both Alliance and Horde players must be present
3. The system will:
   - Create temporary accounts for all participants
   - Import their characters
   - Create a WSG instance
   - Assign players to the battleground

### After Match Completion
- Temporary accounts and characters are automatically cleaned up
- Players return to the web interface
- The lobby expires after the configured timeout

## Technical Details

### Character Import Flow
1. Player provides character JSON data through web interface
2. CharacterWebService creates a temporary account
3. Character is imported with gear, talents, and abilities
4. Player is assigned to the WSG instance
5. On login, player automatically joins the battleground

### Lobby Lifecycle
1. **PENDING** - Lobby created, accepting players
2. **STARTED** - Match has begun, no new players
3. **COMPLETED** - Match finished
4. **EXPIRED** - Lobby timed out without starting

### Security Considerations
- Temporary accounts use random passwords
- Character data is validated before import
- Lobbies expire after timeout to prevent resource exhaustion
- Maximum lobby limit prevents spam

## Troubleshooting

### Web Interface Can't Connect
- Ensure worldserver is running
- Check that REST API is enabled on port 8080
- Verify CORS is configured correctly

### Players Can't Join Battleground
- Check `wsg_lobby_players` table for player entries
- Verify battleground instance exists
- Check worldserver logs for errors

### Lobby Not Starting
- Ensure minimum players requirement is met
- Verify both factions have players
- Check that lobby hasn't expired

## Development Notes

### Adding New Features
Key files to modify:
- `WSGLobbyService.cpp/h` - Core lobby logic
- `lobby.js` - Web interface functionality
- `CharacterHandler.cpp` - Login flow for auto-joining WSG

### API Endpoints
The system uses these REST endpoints:
- `POST /api/lobby/create` - Create new lobby
- `POST /api/lobby/join` - Join existing lobby
- `GET /api/lobby/status/{id}` - Get lobby status
- `POST /api/lobby/start` - Start the match (leader only)

## Known Limitations
- Players must have character export data in JSON format
- Temporary accounts are deleted after lobby expiration
- Maximum 20 players per lobby
- Only Warsong Gulch is supported (other BGs could be added)

## Future Enhancements
- Support for other battlegrounds (AB, AV, etc.)
- Spectator mode
- Match history and statistics
- Tournament bracket system
- Integration with Discord for notifications