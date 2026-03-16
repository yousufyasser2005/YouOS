"""
YouOS System Configuration
"""
import json
import os
from pathlib import Path

class SystemConfig:
    """Manages system-wide configuration"""
    
    CONFIG_DIR = Path('/etc/youos')
    
    def __init__(self):
        self.CONFIG_DIR.mkdir(parents=True, exist_ok=True)
        self.config = self.load_config()
    
    def load_config(self):
        """Load system configuration"""
        config_file = self.CONFIG_DIR / 'system.json'
        
        if config_file.exists():
            try:
                with open(config_file) as f:
                    return json.load(f)
            except:
                return self.default_config()
        else:
            return self.default_config()
    
    def default_config(self):
        """Default system configuration"""
        return {
            'hostname': 'youos',
            'timezone': 'UTC',
            'locale': 'en_US.UTF-8',
            'keyboard_layout': 'us',
            'display': {
                'resolution': 'auto',
                'refresh_rate': 60,
                'scaling': 1.0
            },
            'network': {
                'dhcp': True,
                'dns_servers': ['8.8.8.8', '8.8.4.4']
            },
            'power': {
                'suspend_timeout': 1800,
                'display_timeout': 600
            },
            'version': 'YouOS 10 Build 26m1.7.3'
        }
    
    def save_config(self):
        """Save system configuration"""
        config_file = self.CONFIG_DIR / 'system.json'
        try:
            with open(config_file, 'w') as f:
                json.dump(self.config, f, indent=2)
            return True
        except:
            return False
    
    def get(self, key, default=None):
        """Get configuration value"""
        keys = key.split('.')
        value = self.config
        
        for k in keys:
            if isinstance(value, dict) and k in value:
                value = value[k]
            else:
                return default
        
        return value
    
    def set(self, key, value):
        """Set configuration value"""
        keys = key.split('.')
        config = self.config
        
        for k in keys[:-1]:
            if k not in config:
                config[k] = {}
            config = config[k]
        
        config[keys[-1]] = value
        self.save_config()
    
    def set_hostname(self, hostname):
        """Set system hostname"""
        self.config['hostname'] = hostname
        try:
            with open('/etc/hostname', 'w') as f:
                f.write(hostname)
        except:
            pass
        self.save_config()
    
    def set_timezone(self, timezone):
        """Set system timezone"""
        self.config['timezone'] = timezone
        try:
            tz_path = f'/usr/share/zoneinfo/{timezone}'
            if os.path.exists(tz_path):
                if os.path.exists('/etc/localtime'):
                    os.remove('/etc/localtime')
                os.symlink(tz_path, '/etc/localtime')
        except:
            pass
        self.save_config()
    
    def get_version(self):
        """Get YouOS version"""
        return self.config.get('version', 'YouOS 10 Build 26m1.7.3')