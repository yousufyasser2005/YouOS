"""
YouOS Security Manager
"""
import os
import hashlib
import json
from pathlib import Path

class SecurityManager:
    """Manages system security and permissions"""
    
    def __init__(self):
        self.users_db = Path('/etc/youos/users.json')
        self.groups_db = Path('/etc/youos/groups.json')
        self.permissions_db = Path('/etc/youos/permissions.json')
        
        self.users_db.parent.mkdir(parents=True, exist_ok=True)
        self.load_databases()
    
    def load_databases(self):
        """Load security databases"""
        self.users = self.load_json(self.users_db, {})
        self.groups = self.load_json(self.groups_db, {'admin': ['root'], 'users': []})
        self.permissions = self.load_json(self.permissions_db, {})
    
    def load_json(self, path, default):
        """Load JSON file with fallback"""
        try:
            if path.exists():
                with open(path) as f:
                    return json.load(f)
        except:
            pass
        return default
    
    def save_json(self, path, data):
        """Save JSON file"""
        try:
            with open(path, 'w') as f:
                json.dump(data, f, indent=2)
            return True
        except:
            return False
    
    def hash_password(self, password):
        """Hash password with salt"""
        salt = os.urandom(32)
        pwdhash = hashlib.pbkdf2_hmac('sha256', password.encode('utf-8'), salt, 100000)
        return salt + pwdhash
    
    def verify_password(self, stored_password, provided_password):
        """Verify password against stored hash"""
        try:
            salt = stored_password[:32]
            stored_hash = stored_password[32:]
            pwdhash = hashlib.pbkdf2_hmac('sha256', provided_password.encode('utf-8'), salt, 100000)
            return pwdhash == stored_hash
        except:
            return False
    
    def create_user(self, username, password, groups=None):
        """Create system user"""
        if username in self.users:
            return False, "User already exists"
        
        if len(username) < 3:
            return False, "Username too short"
        
        if len(password) < 4:
            return False, "Password too short"
        
        self.users[username] = {
            'password_hash': self.hash_password(password),
            'groups': groups or ['users'],
            'created_at': str(time.time()),
            'active': True
        }
        
        # Add user to groups
        if groups:
            for group in groups:
                if group in self.groups:
                    if username not in self.groups[group]:
                        self.groups[group].append(username)
        
        self.save_databases()
        return True, "User created successfully"
    
    def authenticate_user(self, username, password):
        """Authenticate user credentials"""
        if username not in self.users:
            return False
        
        user = self.users[username]
        if not user.get('active', True):
            return False
        
        return self.verify_password(user['password_hash'], password)
    
    def check_permission(self, user, resource, action):
        """Check if user has permission"""
        if user not in self.users:
            return False
        
        user_groups = self.users[user].get('groups', [])
        
        # Admin group has all permissions
        if 'admin' in user_groups:
            return True
        
        # Check specific permissions
        perm_key = f"{resource}:{action}"
        if perm_key in self.permissions:
            allowed_groups = self.permissions[perm_key]
            return any(group in user_groups for group in allowed_groups)
        
        return False
    
    def set_permission(self, resource, action, groups):
        """Set permission for resource/action"""
        perm_key = f"{resource}:{action}"
        self.permissions[perm_key] = groups
        self.save_databases()
    
    def set_file_permissions(self, path, mode):
        """Set file permissions"""
        try:
            os.chmod(path, mode)
            return True
        except:
            return False
    
    def add_user_to_group(self, username, group):
        """Add user to group"""
        if username not in self.users:
            return False
        
        if group not in self.groups:
            self.groups[group] = []
        
        if username not in self.groups[group]:
            self.groups[group].append(username)
        
        if group not in self.users[username]['groups']:
            self.users[username]['groups'].append(group)
        
        self.save_databases()
        return True
    
    def remove_user_from_group(self, username, group):
        """Remove user from group"""
        if username not in self.users or group not in self.groups:
            return False
        
        if username in self.groups[group]:
            self.groups[group].remove(username)
        
        if group in self.users[username]['groups']:
            self.users[username]['groups'].remove(group)
        
        self.save_databases()
        return True
    
    def save_databases(self):
        """Save all security databases"""
        self.save_json(self.users_db, self.users)
        self.save_json(self.groups_db, self.groups)
        self.save_json(self.permissions_db, self.permissions)