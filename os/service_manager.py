"""
YouOS Service Manager
"""
import subprocess
import json
import time
from pathlib import Path

class ServiceManager:
    """Manages system services"""
    
    def __init__(self):
        self.services = {}
        self.service_dir = Path('/etc/youos/services')
        self.service_dir.mkdir(parents=True, exist_ok=True)
        self.load_services()
    
    def load_services(self):
        """Load service definitions"""
        for service_file in self.service_dir.glob('*.service'):
            try:
                with open(service_file) as f:
                    service_config = json.load(f)
                    service_name = service_file.stem
                    self.services[service_name] = service_config
            except:
                pass
    
    def start_service(self, name):
        """Start a service"""
        if name not in self.services:
            return False
        
        service = self.services[name]
        command = service.get('command')
        
        if not command:
            return False
        
        try:
            proc = subprocess.Popen(
                command.split(),
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL
            )
            
            service['pid'] = proc.pid
            service['status'] = 'running'
            service['started_at'] = time.time()
            
            print(f"✓ Started service: {name}")
            return True
        except Exception as e:
            print(f"✗ Failed to start service {name}: {e}")
            return False
    
    def stop_service(self, name):
        """Stop a service"""
        if name not in self.services:
            return False
        
        service = self.services[name]
        pid = service.get('pid')
        
        if not pid:
            return False
        
        try:
            import os
            import signal
            os.kill(pid, signal.SIGTERM)
            
            service['status'] = 'stopped'
            service['pid'] = None
            
            print(f"✓ Stopped service: {name}")
            return True
        except:
            return False
    
    def restart_service(self, name):
        """Restart a service"""
        self.stop_service(name)
        time.sleep(1)
        return self.start_service(name)
    
    def enable_service(self, name):
        """Enable service at boot"""
        if name in self.services:
            self.services[name]['enabled'] = True
            self.save_service_config(name)
            return True
        return False
    
    def disable_service(self, name):
        """Disable service at boot"""
        if name in self.services:
            self.services[name]['enabled'] = False
            self.save_service_config(name)
            return True
        return False
    
    def get_service_status(self, name):
        """Get service status"""
        if name not in self.services:
            return 'not found'
        
        service = self.services[name]
        pid = service.get('pid')
        
        if pid:
            try:
                import os
                os.kill(pid, 0)  # Check if process exists
                return 'running'
            except:
                service['status'] = 'stopped'
                service['pid'] = None
                return 'stopped'
        
        return service.get('status', 'stopped')
    
    def list_services(self):
        """List all services"""
        return list(self.services.keys())
    
    def create_service(self, name, command, description="", enabled=False):
        """Create a new service"""
        service_config = {
            'name': name,
            'command': command,
            'description': description,
            'enabled': enabled,
            'status': 'stopped',
            'pid': None
        }
        
        self.services[name] = service_config
        self.save_service_config(name)
        return True
    
    def save_service_config(self, name):
        """Save service configuration"""
        if name in self.services:
            service_file = self.service_dir / f'{name}.service'
            try:
                with open(service_file, 'w') as f:
                    json.dump(self.services[name], f, indent=2)
                return True
            except:
                return False
        return False
    
    def start_enabled_services(self):
        """Start all enabled services"""
        for name, service in self.services.items():
            if service.get('enabled', False):
                self.start_service(name)