// WSG Lobby System JavaScript

const API_BASE = 'http://localhost:8080'; // Change this to your server address

// Current page detection
const isLobbyPage = window.location.pathname.includes('lobby.html');
const isIndexPage = !isLobbyPage;

// State
let currentLobby = null;
let isLeader = false;
let updateInterval = null;

// Index page functionality
if (isIndexPage) {
    document.getElementById('create-lobby-btn').addEventListener('click', showCreateForm);
    document.getElementById('join-lobby-btn').addEventListener('click', showJoinForm);
    document.getElementById('cancel-create').addEventListener('click', hideAllForms);
    document.getElementById('cancel-join').addEventListener('click', hideAllForms);
    document.getElementById('create-form').addEventListener('submit', createLobby);
    document.getElementById('join-form').addEventListener('submit', joinLobby);
    
    // Check if coming from a share link
    const urlParams = new URLSearchParams(window.location.search);
    const lobbyId = urlParams.get('lobby');
    if (lobbyId) {
        document.getElementById('lobby-id-input').value = lobbyId;
        showJoinForm();
    }
}

// Lobby page functionality
if (isLobbyPage) {
    const lobbyId = new URLSearchParams(window.location.search).get('id');
    const leaderName = localStorage.getItem('lobby_leader_' + lobbyId);
    const participantName = localStorage.getItem('lobby_participant_' + lobbyId);
    
    // Determine if current user is the leader
    let currentUserName = leaderName || participantName;
    
    // Set share URL
    const shareUrl = window.location.origin + '/?lobby=' + lobbyId;
    document.getElementById('share-url').value = shareUrl;
    
    document.getElementById('copy-link').addEventListener('click', copyShareLink);
    
    // Only add start game listener if user is leader
    if (leaderName) {
        document.getElementById('start-game').addEventListener('click', startGame);
    }
    
    // Start polling for updates
    startPolling();
}

function showCreateForm() {
    hideAllForms();
    document.getElementById('create-lobby-form').classList.remove('hidden');
}

function showJoinForm() {
    const lobbyId = document.getElementById('lobby-id-input').value;
    if (!lobbyId) {
        showError('Please enter a lobby ID');
        return;
    }
    
    hideAllForms();
    document.getElementById('join-lobby-id').textContent = lobbyId;
    document.getElementById('join-lobby-form').classList.remove('hidden');
}

function hideAllForms() {
    document.getElementById('main-menu').classList.add('hidden');
    document.getElementById('create-lobby-form').classList.add('hidden');
    document.getElementById('join-lobby-form').classList.add('hidden');
    hideMessages();
}

async function createLobby(e) {
    e.preventDefault();
    
    const characterData = document.getElementById('character-import').value;
    
    if (!characterData) {
        showError('Character data is required');
        return;
    }
    
    // Parse character data to extract name and faction
    let parsedData;
    try {
        parsedData = JSON.parse(characterData);
    } catch (error) {
        showError('Invalid character data format. Please paste valid JSON data.');
        return;
    }
    
    // Extract character info
    const character = parsedData.character || parsedData;
    const leaderName = character.name;
    const race = character.race;
    
    if (!leaderName || !race) {
        showError('Character data must include name and race');
        return;
    }
    
    // Determine faction based on race
    const allianceRaces = ['HUMAN', 'DWARF', 'NIGHTELF', 'GNOME', 'DRAENEI'];
    const hordeRaces = ['ORC', 'UNDEAD', 'TAUREN', 'TROLL', 'BLOODELF'];
    
    let faction;
    if (allianceRaces.includes(race.toUpperCase().replace(/[\s_]/g, ''))) {
        faction = 'Alliance';
    } else if (hordeRaces.includes(race.toUpperCase().replace(/[\s_]/g, ''))) {
        faction = 'Horde';
    } else {
        showError('Unknown race: ' + race);
        return;
    }
    
    try {
        const response = await fetch(`${API_BASE}/lobby/create`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({
                leader_name: leaderName,
                faction: faction,
                character_data: characterData
            })
        });
        
        const data = await response.json();
        
        if (data.error) {
            showError(data.error);
            return;
        }
        
        // Store leader info
        localStorage.setItem('lobby_leader_' + data.lobby_id, leaderName);
        
        // Redirect to lobby page
        window.location.href = `lobby.html?id=${data.lobby_id}`;
        
    } catch (error) {
        showError('Failed to create lobby: ' + error.message);
    }
}

async function joinLobby(e) {
    e.preventDefault();
    
    const lobbyId = document.getElementById('lobby-id-input').value;
    const characterData = document.getElementById('participant-import').value;
    
    if (!characterData) {
        showError('Character data is required');
        return;
    }
    
    // Parse character data to extract name and faction
    let parsedData;
    try {
        parsedData = JSON.parse(characterData);
    } catch (error) {
        showError('Invalid character data format. Please paste valid JSON data.');
        return;
    }
    
    // Extract character info
    const character = parsedData.character || parsedData;
    const participantName = character.name;
    const race = character.race;
    
    if (!participantName || !race) {
        showError('Character data must include name and race');
        return;
    }
    
    // Determine faction based on race
    const allianceRaces = ['HUMAN', 'DWARF', 'NIGHTELF', 'GNOME', 'DRAENEI'];
    const hordeRaces = ['ORC', 'UNDEAD', 'TAUREN', 'TROLL', 'BLOODELF'];
    
    let faction;
    if (allianceRaces.includes(race.toUpperCase().replace(/[\s_]/g, ''))) {
        faction = 'Alliance';
    } else if (hordeRaces.includes(race.toUpperCase().replace(/[\s_]/g, ''))) {
        faction = 'Horde';
    } else {
        showError('Unknown race: ' + race);
        return;
    }
    
    try {
        const response = await fetch(`${API_BASE}/lobby/${lobbyId}/join`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({
                character_name: participantName,
                faction: faction,
                character_data: characterData
            })
        });
        
        const data = await response.json();
        
        if (data.error) {
            showError(data.error);
            return;
        }
        
        // Store participant info
        localStorage.setItem('lobby_participant_' + lobbyId, participantName);
        
        // Redirect to lobby page
        window.location.href = `lobby.html?id=${lobbyId}`;
        
    } catch (error) {
        showError('Failed to join lobby: ' + error.message);
    }
}

async function updateLobbyStatus() {
    const lobbyId = new URLSearchParams(window.location.search).get('id');
    const leaderName = localStorage.getItem('lobby_leader_' + lobbyId);
    const participantName = localStorage.getItem('lobby_participant_' + lobbyId);
    const currentUserName = leaderName || participantName;
    
    try {
        const response = await fetch(`${API_BASE}/lobby/${lobbyId}/status`);
        const data = await response.json();
        
        if (data.error) {
            showError(data.error);
            return;
        }
        
        currentLobby = data;
        
        // Determine if current user is the leader
        isLeader = (currentUserName === data.leader);
        
        // Update UI based on whether user is leader
        if (isLeader) {
            document.getElementById('leader-controls').classList.remove('hidden');
            document.getElementById('waiting-message').classList.add('hidden');
            document.getElementById('start-game').disabled = !data.can_start;
        } else {
            document.getElementById('leader-controls').classList.add('hidden');
            document.getElementById('waiting-message').classList.remove('hidden');
            // Update waiting message to show leader name
            document.getElementById('waiting-message').innerHTML = 
                `<p>Waiting for lobby leader <strong>${data.leader}</strong> to start the match...</p>`;
        }
        
        // Update player lists
        updatePlayerLists(data.participants);
        
        // Update counts
        document.getElementById('alliance-count').textContent = data.alliance_count;
        document.getElementById('horde-count').textContent = data.horde_count;
        
        // Check if game started
        if (data.status === 'started' && data.wsg_instance_id) {
            stopPolling();
            showGameStarted(data.wsg_instance_id);
        }
        
    } catch (error) {
        console.error('Failed to update lobby status:', error);
    }
}

function updatePlayerLists(participants) {
    const allianceList = document.getElementById('alliance-players');
    const hordeList = document.getElementById('horde-players');
    
    allianceList.innerHTML = '';
    hordeList.innerHTML = '';
    
    participants.forEach(player => {
        const li = document.createElement('li');
        li.className = 'player-item';
        
        // Mark the leader with a crown icon and different styling
        if (player.name === currentLobby.leader) {
            li.innerHTML = `👑 <strong>${player.name}</strong> (Leader)`;
            li.classList.add('leader');
        } else {
            li.textContent = player.name;
        }
        
        if (player.faction === 'Alliance') {
            allianceList.appendChild(li);
        } else {
            hordeList.appendChild(li);
        }
    });
}

async function startGame() {
    const lobbyId = new URLSearchParams(window.location.search).get('id');
    const leaderName = localStorage.getItem('lobby_leader_' + lobbyId);
    
    try {
        const response = await fetch(`${API_BASE}/lobby/${lobbyId}/start`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({
                requester: leaderName
            })
        });
        
        const data = await response.json();
        
        if (data.error) {
            showError(data.error);
            return;
        }
        
        // Show game started screen with credentials
        showGameStarted(data.wsg_instance_id, data.accounts);
        
    } catch (error) {
        showError('Failed to start game: ' + error.message);
    }
}

function showGameStarted(instanceId, accounts) {
    document.getElementById('game-started').classList.remove('hidden');
    document.getElementById('wsg-instance').textContent = instanceId;
    
    if (accounts && accounts.length > 0) {
        const tbody = document.getElementById('credentials-body');
        tbody.innerHTML = '';
        
        accounts.forEach(acc => {
            const tr = document.createElement('tr');
            tr.innerHTML = `
                <td>${acc.username}</td>
                <td>${acc.password}</td>
                <td>${acc.username}</td>
            `;
            tbody.appendChild(tr);
        });
        
        document.getElementById('copy-all-credentials').addEventListener('click', () => {
            copyAllCredentials(accounts);
        });
    }
}

function copyShareLink() {
    const shareUrl = document.getElementById('share-url');
    shareUrl.select();
    document.execCommand('copy');
    showSuccess('Link copied to clipboard!');
}

function copyAllCredentials(accounts) {
    const text = accounts.map(acc => 
        `Username: ${acc.username} | Password: ${acc.password}`
    ).join('\n');
    
    const textarea = document.createElement('textarea');
    textarea.value = text;
    document.body.appendChild(textarea);
    textarea.select();
    document.execCommand('copy');
    document.body.removeChild(textarea);
    
    showSuccess('All credentials copied to clipboard!');
}

function startPolling() {
    updateLobbyStatus();
    updateInterval = setInterval(updateLobbyStatus, 5000); // Poll every 5 seconds instead of 2
}

function stopPolling() {
    if (updateInterval) {
        clearInterval(updateInterval);
        updateInterval = null;
    }
}

function showError(message) {
    const errorEl = document.getElementById('error-message');
    errorEl.textContent = message;
    errorEl.classList.remove('hidden');
    setTimeout(() => errorEl.classList.add('hidden'), 5000);
}

function showSuccess(message) {
    const successEl = document.getElementById('success-message');
    successEl.textContent = message;
    successEl.classList.remove('hidden');
    setTimeout(() => successEl.classList.add('hidden'), 3000);
}

function hideMessages() {
    document.getElementById('error-message').classList.add('hidden');
    document.getElementById('success-message').classList.add('hidden');
}