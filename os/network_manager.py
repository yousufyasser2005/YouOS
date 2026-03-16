"""
YouOS Network Manager
Manages network connections, WiFi, Ethernet, VPN, and firewall
"""

import os
import subprocess
import re
import json
import time
from pathlib import Path
from typing import Dict, List, Optional, Tuple
from dataclasses import dataclass


@dataclass
class NetworkInterface:
    """Network interface information"""
    name: str
    mac_address: str
    ip_address: Optional[str]
    netmask: Optional[str]
    status: str  # up/down
    type: str  # ethernet/wifi/loopback


@dataclass
class WiFiNetwork:
    """WiFi network information"""
    ssid: str
    signal_strength: int  # 0-100
    security: str  # WPA2/WPA3/Open
    frequency: str  # 2.4GHz/5GHz
    encrypted: bool


class NetworkManager:
    """Manages all network operations"""
    
    def __init__(self):
        self.config_dir = Path('/etc/youos/network')
        self.config_dir.mkdir(parents=True, exist_ok=True)
        
        self.saved_networks = self._load_saved_networks()
        self.firewall_rules = []
    
    def _load_saved_networks(self) -> Dict:
        """Load saved network configurations"""
        config_file = self.config_dir / 'saved_networks.json'
        if config_file.exists():
            try:
                with open(config_file) as f:
                    return json.load(f)
            except:
                return {}
        return {}
    
    def _save_networks(self):
        """Save network configurations"""
        config_file = self.config_dir / 'saved_networks.json'
        try:
            with open(config_file, 'w') as f:
                json.dump(self.saved_networks, f, indent=2)
        except:
            pass
    
    def get_interfaces(self) -> List[NetworkInterface]:
        """Get all network interfaces"""
        interfaces = []
        
        try:
            result = subprocess.run(['ip', 'link', 'show'], capture_output=True, text=True)
            
            for line in result.stdout.split('\n'):
                if ': ' in line and not line.startswith(' '):
                    parts = line.split(': ')
                    if len(parts) >= 2:
                        name = parts[1].split('@')[0]
                        
                        # Get MAC address
                        mac_match = re.search(r'link/ether ([0-9a-f:]+)', line)
                        mac = mac_match.group(1) if mac_match else None
                        
                        # Get status
                        status = 'up' if 'UP' in line else 'down'
                        
                        # Determine type
                        if name.startswith('wl'):
                            iface_type = 'wifi'
                        elif name.startswith('en') or name.startswith('eth'):
                            iface_type = 'ethernet'
                        elif name == 'lo':
                            iface_type = 'loopback'
                        else:
                            iface_type = 'unknown'
                        
                        # Get IP address
                        ip_result = subprocess.run(['ip', 'addr', 'show', name], capture_output=True, text=True)
                        ip_match = re.search(r'inet (\d+\.\d+\.\d+\.\d+)/(\d+)', ip_result.stdout)
                        ip = ip_match.group(1) if ip_match else None
                        netmask = self._cidr_to_netmask(int(ip_match.group(2))) if ip_match else None
                        
                        interfaces.append(NetworkInterface(
                            name=name,
                            mac_address=mac,
                            ip_address=ip,
                            netmask=netmask,
                            status=status,
                            type=iface_type
                        ))
        except:
            pass
        
        return interfaces
    
    def _cidr_to_netmask(self, cidr: int) -> str:
        """Convert CIDR to netmask"""
        mask = (0xffffffff >> (32 - cidr)) << (32 - cidr)
        return '.'.join([str((mask >> (24 - i * 8)) & 0xff) for i in range(4)])
    
    def scan_wifi(self) -> List[WiFiNetwork]:
        """Scan for available WiFi networks"""
        networks = []
        
        try:
            wifi_interfaces = [i for i in self.get_interfaces() if i.type == 'wifi']
            if not wifi_interfaces:
                return networks
            
            wifi_iface = wifi_interfaces[0].name
            
            result = subprocess.run(['iwlist', wifi_iface, 'scan'], capture_output=True, text=True, timeout=10)
            
            current_network = {}
            for line in result.stdout.split('\n'):
                line = line.strip()
                
                if 'Cell ' in line:
                    if current_network:
                        networks.append(self._parse_wifi_network(current_network))
                    current_network = {}
                
                elif 'ESSID:' in line:
                    ssid = re.search(r'ESSID:"(.+)"', line)
                    if ssid:
                        current_network['ssid'] = ssid.group(1)
                
                elif 'Quality=' in line:
                    quality = re.search(r'Quality=(\d+)/(\d+)', line)
                    if quality:
                        current_network['quality'] = int(quality.group(1))
                        current_network['max_quality'] = int(quality.group(2))
                
                elif 'Encryption key:' in line:
                    current_network['encrypted'] = 'on' in line.lower()
                
                elif 'WPA' in line:
                    if 'WPA3' in line:
                        current_network['security'] = 'WPA3'
                    elif 'WPA2' in line:
                        current_network['security'] = 'WPA2'
                    else:
                        current_network['security'] = 'WPA'
            
            if current_network:
                networks.append(self._parse_wifi_network(current_network))
        
        except:
            pass
        
        return networks
    
    def _parse_wifi_network(self, data: Dict) -> WiFiNetwork:
        """Parse WiFi network data"""
        quality = data.get('quality', 0)
        max_quality = data.get('max_quality', 100)
        signal_strength = int((quality / max_quality) * 100) if max_quality > 0 else 0
        
        return WiFiNetwork(
            ssid=data.get('ssid', 'Unknown'),
            signal_strength=signal_strength,
            security=data.get('security', 'Unknown'),
            frequency=data.get('frequency', '2.4GHz'),
            encrypted=data.get('encrypted', True)
        )
    
    def connect_wifi(self, ssid: str, password: Optional[str] = None) -> bool:
        """Connect to WiFi network"""
        try:
            wifi_interfaces = [i for i in self.get_interfaces() if i.type == 'wifi']
            if not wifi_interfaces:
                return False
            
            wifi_iface = wifi_interfaces[0].name
            
            # Use nmcli if available
            try:
                if password:
                    subprocess.run(['nmcli', 'dev', 'wifi', 'connect', ssid, 'password', password], 
                                 check=True, timeout=30)
                else:
                    subprocess.run(['nmcli', 'dev', 'wifi', 'connect', ssid], 
                                 check=True, timeout=30)
                
                self.saved_networks[ssid] = {'password': password, 'auto_connect': True}
                self._save_networks()
                return True
            except:
                pass
            
            # Fallback to wpa_supplicant
            config_file = self.config_dir / 'wpa_supplicant.conf'
            config = f'''ctrl_interface=/var/run/wpa_supplicant
update_config=1

network={{
    ssid="{ssid}"
'''
            
            if password:
                config += f'    psk="{password}"\n'
            else:
                config += '    key_mgmt=NONE\n'
            
            config += '}\n'
            
            with open(config_file, 'w') as f:
                f.write(config)
            
            subprocess.run(['killall', 'wpa_supplicant'], capture_output=True)
            subprocess.Popen(['wpa_supplicant', '-B', '-i', wifi_iface, '-c', str(config_file)])
            time.sleep(2)
            subprocess.run(['dhclient', wifi_iface], timeout=10)
            
            self.saved_networks[ssid] = {'password': password, 'auto_connect': True}
            self._save_networks()
            return True
            
        except:
            return False
    
    def disconnect_wifi(self) -> bool:
        """Disconnect from WiFi"""
        try:
            subprocess.run(['nmcli', 'dev', 'disconnect', 'iface', 'wifi'], capture_output=True)
            subprocess.run(['killall', 'wpa_supplicant'], capture_output=True)
            return True
        except:
            return False
    
    def configure_ethernet(self, interface: str, mode: str = 'dhcp',
                          ip: Optional[str] = None,
                          netmask: Optional[str] = None,
                          gateway: Optional[str] = None) -> bool:
        """Configure ethernet interface"""
        try:
            if mode == 'dhcp':
                subprocess.run(['dhclient', interface], timeout=10)
            else:
                if not (ip and netmask):
                    return False
                
                subprocess.run(['ip', 'addr', 'add', f'{ip}/{netmask}', 'dev', interface])
                subprocess.run(['ip', 'link', 'set', interface, 'up'])
                
                if gateway:
                    subprocess.run(['ip', 'route', 'add', 'default', 'via', gateway])
            
            return True
        except:
            return False
    
    def set_dns_servers(self, dns_servers: List[str]) -> bool:
        """Set DNS servers"""
        try:
            with open('/etc/resolv.conf', 'w') as f:
                for dns in dns_servers:
                    f.write(f'nameserver {dns}\n')
            return True
        except:
            return False
    
    def enable_firewall(self, rules: Optional[List[Dict]] = None) -> bool:
        """Enable firewall with rules"""
        try:
            subprocess.run(['iptables', '-F'])
            subprocess.run(['iptables', '-P', 'INPUT', 'DROP'])
            subprocess.run(['iptables', '-P', 'FORWARD', 'DROP'])
            subprocess.run(['iptables', '-P', 'OUTPUT', 'ACCEPT'])
            
            # Allow loopback
            subprocess.run(['iptables', '-A', 'INPUT', '-i', 'lo', '-j', 'ACCEPT'])
            
            # Allow established connections
            subprocess.run(['iptables', '-A', 'INPUT', '-m', 'state', 
                          '--state', 'ESTABLISHED,RELATED', '-j', 'ACCEPT'])
            
            if rules:
                for rule in rules:
                    self._apply_firewall_rule(rule)
            
            self.firewall_rules = rules or []
            return True
            
        except:
            return False
    
    def _apply_firewall_rule(self, rule: Dict):
        """Apply a single firewall rule"""
        try:
            action = rule.get('action', 'ACCEPT')
            protocol = rule.get('protocol', 'tcp')
            port = rule.get('port')
            source = rule.get('source')
            
            cmd = ['iptables', '-A', 'INPUT']
            
            if protocol:
                cmd.extend(['-p', protocol])
            if port:
                cmd.extend(['--dport', str(port)])
            if source:
                cmd.extend(['-s', source])
            
            cmd.extend(['-j', action])
            subprocess.run(cmd)
        except:
            pass
    
    def disable_firewall(self) -> bool:
        """Disable firewall"""
        try:
            subprocess.run(['iptables', '-F'])
            subprocess.run(['iptables', '-P', 'INPUT', 'ACCEPT'])
            subprocess.run(['iptables', '-P', 'FORWARD', 'ACCEPT'])
            subprocess.run(['iptables', '-P', 'OUTPUT', 'ACCEPT'])
            return True
        except:
            return False
    
    def get_network_stats(self) -> Dict:
        """Get network statistics"""
        stats = {}
        
        try:
            for iface in self.get_interfaces():
                if iface.type == 'loopback':
                    continue
                
                stats_dir = Path(f'/sys/class/net/{iface.name}/statistics')
                
                if stats_dir.exists():
                    try:
                        rx_bytes = int((stats_dir / 'rx_bytes').read_text().strip())
                        tx_bytes = int((stats_dir / 'tx_bytes').read_text().strip())
                        rx_packets = int((stats_dir / 'rx_packets').read_text().strip())
                        tx_packets = int((stats_dir / 'tx_packets').read_text().strip())
                        
                        stats[iface.name] = {
                            'rx_bytes': rx_bytes,
                            'tx_bytes': tx_bytes,
                            'rx_packets': rx_packets,
                            'tx_packets': tx_packets
                        }
                    except:
                        pass
        except:
            pass
        
        return stats
    
    def ping(self, host: str, count: int = 4) -> Tuple[bool, Dict]:
        """Ping a host"""
        try:
            result = subprocess.run(['ping', '-c', str(count), host],
                                  capture_output=True, text=True, timeout=count + 5)
            
            success = result.returncode == 0
            stats = {'success': success, 'packet_loss': 100}
            
            if success:
                loss_match = re.search(r'(\d+)% packet loss', result.stdout)
                if loss_match:
                    stats['packet_loss'] = int(loss_match.group(1))
                
                time_match = re.search(
                    r'rtt min/avg/max/mdev = ([\d.]+)/([\d.]+)/([\d.]+)/([\d.]+)',
                    result.stdout
                )
                if time_match:
                    stats['min_ms'] = float(time_match.group(1))
                    stats['avg_ms'] = float(time_match.group(2))
                    stats['max_ms'] = float(time_match.group(3))
            
            return success, stats
            
        except:
            return False, {'error': 'Ping failed'}


if __name__ == '__main__':
    nm = NetworkManager()
    
    print("Network Interfaces:")
    print("=" * 60)
    for iface in nm.get_interfaces():
        print(f"{iface.name} ({iface.type})")
        print(f"  Status: {iface.status}")
        print(f"  MAC: {iface.mac_address}")
        print(f"  IP: {iface.ip_address}")
        print()