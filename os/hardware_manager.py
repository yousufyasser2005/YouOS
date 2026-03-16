"""
YouOS Hardware Manager
"""
import subprocess
import re
import os
from pathlib import Path

class HardwareManager:
    """Manages hardware detection and configuration"""
    
    def detect_cpu(self):
        """Detect CPU information"""
        try:
            with open('/proc/cpuinfo') as f:
                cpuinfo = f.read()
            
            model = re.search(r'model name\s+:\s+(.+)', cpuinfo)
            cores = len(re.findall(r'processor\s+:', cpuinfo))
            
            return {
                'model': model.group(1) if model else 'Unknown',
                'cores': cores,
                'architecture': os.uname().machine
            }
        except:
            return {
                'model': 'Unknown CPU',
                'cores': 1,
                'architecture': 'x86_64'
            }
    
    def detect_memory(self):
        """Detect memory information"""
        try:
            with open('/proc/meminfo') as f:
                meminfo = f.read()
            
            total = re.search(r'MemTotal:\s+(\d+)', meminfo)
            available = re.search(r'MemAvailable:\s+(\d+)', meminfo)
            
            return {
                'total_kb': int(total.group(1)) if total else 0,
                'available_kb': int(available.group(1)) if available else 0
            }
        except:
            return {'total_kb': 8388608, 'available_kb': 4194304}  # 8GB/4GB fallback
    
    def detect_disks(self):
        """Detect disk drives"""
        try:
            result = subprocess.run(['lsblk', '-J'], capture_output=True, text=True)
            import json
            return json.loads(result.stdout)
        except:
            return {'blockdevices': []}
    
    def detect_network(self):
        """Detect network interfaces"""
        try:
            result = subprocess.run(['ip', '-j', 'link'], capture_output=True, text=True)
            import json
            return json.loads(result.stdout)
        except:
            return []
    
    def detect_gpu(self):
        """Detect GPU"""
        try:
            result = subprocess.run(['lspci'], capture_output=True, text=True)
            gpu_lines = [line for line in result.stdout.split('\n') 
                        if 'VGA' in line or '3D' in line]
            
            return [
                {'description': line.split(': ')[1] if ': ' in line else line}
                for line in gpu_lines
            ]
        except:
            return [{'description': 'Unknown GPU'}]
    
    def get_system_info(self):
        """Get comprehensive system information"""
        return {
            'cpu': self.detect_cpu(),
            'memory': self.detect_memory(),
            'disks': self.detect_disks(),
            'network': self.detect_network(),
            'gpu': self.detect_gpu()
        }
    
    def set_power_mode(self, mode='balanced'):
        """Set power management mode"""
        modes = {
            'performance': 'performance',
            'balanced': 'ondemand',
            'powersave': 'powersave'
        }
        
        governor = modes.get(mode, 'ondemand')
        
        try:
            for cpu_path in Path('/sys/devices/system/cpu/').glob('cpu*/cpufreq/scaling_governor'):
                with open(cpu_path, 'w') as f:
                    f.write(governor)
            return True
        except:
            return False