"""
YouOS Package Manager (ypm)
"""
import json
import subprocess
import hashlib
import time
from pathlib import Path

class PackageManager:
    """Manages software installation and updates"""
    
    def __init__(self):
        self.repo_url = "https://repo.youos.org/packages"
        self.installed_db = Path("/var/lib/ypm/installed.json")
        self.cache_dir = Path("/var/cache/ypm")
        
        self.cache_dir.mkdir(parents=True, exist_ok=True)
        self.load_installed()
    
    def load_installed(self):
        """Load installed packages database"""
        if self.installed_db.exists():
            with open(self.installed_db) as f:
                self.installed = json.load(f)
        else:
            self.installed = {}
    
    def search(self, query):
        """Search for packages"""
        try:
            import requests
            resp = requests.get(f"{self.repo_url}/search?q={query}")
            return resp.json()
        except:
            return []
    
    def install(self, package_name):
        """Install a package"""
        print(f"Installing {package_name}...")
        
        try:
            pkg_data = self.download_package(package_name)
            if not self.verify_package(pkg_data):
                raise ValueError("Package signature invalid")
            
            self.extract_package(pkg_data, package_name)
            
            self.installed[package_name] = {
                'version': pkg_data.get('version', '1.0'),
                'installed_at': str(time.time())
            }
            self.save_installed()
            
            print(f"✓ {package_name} installed")
            return True
        except Exception as e:
            print(f"✗ Failed to install {package_name}: {e}")
            return False
    
    def remove(self, package_name):
        """Remove a package"""
        if package_name not in self.installed:
            print(f"{package_name} not installed")
            return False
        
        try:
            self.remove_package_files(package_name)
            del self.installed[package_name]
            self.save_installed()
            
            print(f"✓ {package_name} removed")
            return True
        except Exception as e:
            print(f"✗ Failed to remove {package_name}: {e}")
            return False
    
    def update(self):
        """Update all packages"""
        try:
            import requests
            resp = requests.get(f"{self.repo_url}/updates")
            updates = resp.json()
            
            for pkg_name, new_version in updates.items():
                if pkg_name in self.installed:
                    current = self.installed[pkg_name]['version']
                    if self.compare_versions(new_version, current) > 0:
                        print(f"Updating {pkg_name}: {current} -> {new_version}")
                        self.install(pkg_name)
        except Exception as e:
            print(f"Update failed: {e}")
    
    def download_package(self, name):
        """Download package from repository"""
        # Simulate package download
        return {
            'name': name,
            'version': '1.0',
            'archive': 'fake_archive_data'
        }
    
    def verify_package(self, pkg_data):
        """Verify package signature"""
        return True  # Simplified verification
    
    def extract_package(self, pkg_data, name):
        """Extract package files"""
        # Simulate package extraction
        pass
    
    def remove_package_files(self, name):
        """Remove package files"""
        # Simulate file removal
        pass
    
    def compare_versions(self, v1, v2):
        """Compare version strings"""
        return 1 if v1 > v2 else -1 if v1 < v2 else 0
    
    def save_installed(self):
        """Save installed packages database"""
        self.installed_db.parent.mkdir(parents=True, exist_ok=True)
        with open(self.installed_db, 'w') as f:
            json.dump(self.installed, f, indent=2)