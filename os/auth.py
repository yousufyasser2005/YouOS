# auth.py - Authentication System
"""
YouOS Authentication and User Management
Handles user accounts, password hashing, and session management
"""

import json
import os
import hashlib
import secrets
from config import USERS_FILE

class AuthManager:
    """Manages user authentication and accounts"""
    
    def __init__(self):
        self.users = self.load_users()
    
    def load_users(self):
        """Load users from JSON file"""
        if os.path.exists(USERS_FILE):
            try:
                with open(USERS_FILE, 'r') as f:
                    return json.load(f)
            except Exception as e:
                print(f"Error loading users: {e}")
        return {}
    
    def save_users(self):
        """Save users to JSON file"""
        try:
            with open(USERS_FILE, 'w') as f:
                json.dump(self.users, f, indent=4)
        except Exception as e:
            print(f"Error saving users: {e}")
    
    def hash_password(self, password, salt=None):
        """Hash password with salt using PBKDF2"""
        if salt is None:
            salt = secrets.token_hex(16)
        
        pwd_hash = hashlib.pbkdf2_hmac(
            'sha256',
            password.encode(),
            salt.encode(),
            100000
        )
        return salt + ':' + pwd_hash.hex()
    
    def verify_password(self, stored_password, provided_password):
        """Verify password against stored hash"""
        try:
            salt, pwd_hash = stored_password.split(':')
            new_hash = hashlib.pbkdf2_hmac(
                'sha256',
                provided_password.encode(),
                salt.encode(),
                100000
            )
            return pwd_hash == new_hash.hex()
        except:
            # Fallback for old format (SHA256 without salt)
            return stored_password == hashlib.sha256(
                provided_password.encode()
            ).hexdigest()
    
    def create_user(self, username, password):
        """Create a new user account"""
        if not username or not username.strip():
            return False, "Username cannot be empty"
        
        if username in self.users:
            return False, "Username already exists"
        
        if len(password) < 4:
            return False, "Password must be at least 4 characters"
        
        self.users[username] = {
            "password": self.hash_password(password),
            "theme": "dark",
            "wallpaper": None,
            "icon_positions": {},
            "window_positions": {}
        }
        self.save_users()
        return True, "Account created successfully"
    
    def authenticate(self, username, password):
        """Authenticate a user"""
        if username not in self.users:
            return False
        
        return self.verify_password(
            self.users[username]["password"],
            password
        )
    
    def get_user_data(self, username):
        """Get user data"""
        return self.users.get(username, {})
    
    def update_user_data(self, username, key, value):
        """Update user data"""
        if username in self.users:
            self.users[username][key] = value
            self.save_users()
    
    def get_all_usernames(self):
        """Get list of all usernames"""
        return list(self.users.keys())