#!/usr/bin/env python3
"""
Simple test script for WSG Lobby system
Creates a lobby, imports two characters from sample_exports.json, and starts the match
Does NOT manage any servers - assumes they are already running
"""

import json
import time
import requests
import sys

# Configuration
API_BASE = "http://localhost:8080"

def load_character_data():
    """Load character data from sample_exports.json"""
    json_path = "sample_exports.json"

    print(f"[INFO] Loading character data from {json_path}")

    with open(json_path, 'r') as f:
        data = json.load(f)

    characters = data.get('characters', [])
    if len(characters) < 2:
        print("[ERROR] sample_exports.json must contain at least 2 characters")
        return None

    # Extract the two characters
    char1 = characters[0]  # Thirkfears - Horde Warlock
    char2 = characters[1]  # Thirkcarry - Alliance Druid

    # Fix faction names to match what the API expects
    if char1['character']['faction'] == 'HORDE':
        char1['character']['faction'] = 'Horde'
    elif char1['character']['faction'] == 'ALLIANCE':
        char1['character']['faction'] = 'Alliance'

    if char2['character']['faction'] == 'HORDE':
        char2['character']['faction'] = 'Horde'
    elif char2['character']['faction'] == 'ALLIANCE':
        char2['character']['faction'] = 'Alliance'

    print(f"[INFO] Loaded {char1['character']['name']} ({char1['character']['faction']}) and "
          f"{char2['character']['name']} ({char2['character']['faction']})")

    return char1, char2

def create_lobby(character_data):
    """Create a new WSG lobby with the first character as leader"""
    print(f"\n[LOBBY] Creating lobby with {character_data['character']['name']} as leader...")

    # Prepare the request
    url = f"{API_BASE}/lobby/create"
    payload = {
        "leader_name": character_data['character']['name'],
        "faction": character_data['character']['faction'],
        "character_data": character_data  # Send as object, not string
    }

    try:
        response = requests.post(url, json=payload, timeout=10)
        print(f"[DEBUG] Response status: {response.status_code}")
        print(f"[DEBUG] Response text: {response.text}")

        if response.status_code == 200:
            result = response.json()
            lobby_id = result.get('lobbyId') or result.get('lobby_id') or result.get('id')
            print(f"[DEBUG] Full response JSON: {result}")
            print(f"[SUCCESS] Lobby created with ID: {lobby_id}")
            return lobby_id
        else:
            print(f"[ERROR] Failed to create lobby: {response.status_code}")
            print(f"[ERROR] Response: {response.text}")
            return None
    except Exception as e:
        print(f"[ERROR] Exception creating lobby: {e}")
        import traceback
        traceback.print_exc()
        return None

def join_lobby(lobby_id, character_data):
    """Join an existing lobby with a character"""
    print(f"\n[LOBBY] {character_data['character']['name']} joining lobby {lobby_id}...")

    # URL format is /lobby/{id}/join
    url = f"{API_BASE}/lobby/{lobby_id}/join"
    payload = {
        "character_name": character_data['character']['name'],
        "faction": character_data['character']['faction'],
        "character_data": character_data  # Send as object, not string
    }

    try:
        response = requests.post(url, json=payload, timeout=10)

        if response.status_code == 200:
            print(f"[SUCCESS] {character_data['character']['name']} joined lobby")
            return True
        else:
            print(f"[ERROR] Failed to join lobby: {response.status_code}")
            print(f"[ERROR] Response: {response.text}")
            return False
    except Exception as e:
        print(f"[ERROR] Exception joining lobby: {e}")
        return False

def get_lobby_status(lobby_id):
    """Get the current status of a lobby"""
    url = f"{API_BASE}/lobby/status/{lobby_id}"

    try:
        response = requests.get(url, timeout=10)

        if response.status_code == 200:
            result = response.json()
            print(f"[DEBUG] Status response: {result}")
            return result
        else:
            print(f"[ERROR] Failed to get lobby status: {response.status_code}")
            return None
    except Exception as e:
        print(f"[ERROR] Exception getting lobby status: {e}")
        return None

def start_lobby(lobby_id, leader_name):
    """Start the WSG match for a lobby"""
    print(f"\n[LOBBY] Starting match for lobby {lobby_id}...")

    # URL format is /lobby/{id}/start
    url = f"{API_BASE}/lobby/{lobby_id}/start"
    payload = {
        "requester": leader_name
    }

    try:
        response = requests.post(url, json=payload, timeout=10)

        if response.status_code == 200:
            result = response.json()
            print(f"[DEBUG] Start response: {result}")
            instance_id = result.get('instanceId') or result.get('instance_id') or result.get('wsg_instance_id')
            print(f"[SUCCESS] WSG match started! Instance ID: {instance_id}")
            return instance_id
        else:
            print(f"[ERROR] Failed to start lobby: {response.status_code}")
            print(f"[ERROR] Response: {response.text}")
            return None
    except Exception as e:
        print(f"[ERROR] Exception starting lobby: {e}")
        return None

def main():
    """Main test function"""
    print("=" * 60)
    print("WSG LOBBY SYSTEM TEST (Simple)")
    print("=" * 60)
    print("\n[NOTE] This test assumes all servers are already running:")
    print("  - Worldserver with REST API on port 8080")
    print("  - Web server on port 3000")
    print("")

    try:
        # Step 1: Load character data
        print("\n[STEP 1] Loading character data...")
        char_data = load_character_data()
        if not char_data:
            print("[ERROR] Failed to load character data")
            return False

        horde_char, alliance_char = char_data

        # Step 2: Create lobby with Horde character as leader
        print("\n[STEP 2] Creating WSG lobby...")
        lobby_id = create_lobby(horde_char)
        if not lobby_id:
            print("[ERROR] Failed to create lobby")
            return False

        # Step 3: Check lobby status
        print("\n[STEP 3] Checking lobby status...")
        status = get_lobby_status(lobby_id)
        if status:
            print(f"[INFO] Lobby status: {status.get('status', 'unknown')}")
            print(f"[INFO] Players: Alliance={status.get('alliance_count', 0)}, Horde={status.get('horde_count', 0)}")

        # Step 4: Join with Alliance character
        print("\n[STEP 4] Adding Alliance player to lobby...")
        if not join_lobby(lobby_id, alliance_char):
            print("[ERROR] Failed to join lobby")
            return False

        # Step 5: Check updated status
        print("\n[STEP 5] Checking updated lobby status...")
        status = get_lobby_status(lobby_id)
        if status:
            print(f"[INFO] Lobby status: {status.get('status', 'unknown')}")
            print(f"[INFO] Players: Alliance={status.get('alliance_count', 0)}, Horde={status.get('horde_count', 0)}")
            print(f"[INFO] Can start: {status.get('can_start', False)}")

        # Step 6: Start the match
        print("\n[STEP 6] Starting WSG match...")
        instance_id = start_lobby(lobby_id, horde_char['character']['name'])
        if not instance_id:
            print("[ERROR] Failed to start match")
            return False

        # Step 7: Verify match started
        print("\n[STEP 7] Verifying match status...")
        time.sleep(2)
        status = get_lobby_status(lobby_id)
        if status:
            print(f"[INFO] Final lobby status: {status.get('status', 'unknown')}")
            print(f"[INFO] WSG Instance ID: {status.get('wsg_instance_id', 'N/A')}")

        print("\n" + "=" * 60)
        print("TEST COMPLETED SUCCESSFULLY!")
        print("=" * 60)
        print("\nSummary:")
        print(f"  - Lobby ID: {lobby_id}")
        print(f"  - WSG Instance: {instance_id}")
        print(f"  - Horde Leader: {horde_char['character']['name']}")
        print(f"  - Alliance Player: {alliance_char['character']['name']}")
        print("\nThe WSG battleground has been created and is waiting for players.")
        print("Characters will be imported when they 'log in' to the lobby.")

        return True

    except Exception as e:
        print(f"\n[ERROR] Unexpected error: {e}")
        import traceback
        traceback.print_exc()
        return False

if __name__ == "__main__":
    success = main()
    sys.exit(0 if success else 1)