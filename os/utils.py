"""
YouOS 10 PyQt6 - Utilities
utils.py - Sound manager, path handling, system utilities with kernel integration
"""

import os
import sys
from pathlib import Path
from PyQt6.QtCore import QUrl
from PyQt6.QtMultimedia import QMediaPlayer, QAudioOutput

# Get the correct base directory (where main.py is located)
if getattr(sys, 'frozen', False):
    BASE_DIR = Path(sys.executable).parent
else:
    BASE_DIR = Path(__file__).parent.absolute()

# Assets directories
ASSETS_DIR = BASE_DIR / 'assets'
SOUNDS_DIR = ASSETS_DIR / 'sounds'
WALLPAPERS_DIR = ASSETS_DIR / 'wallpapers'
ICONS_DIR = ASSETS_DIR / 'icons'

# Create directories if they don't exist
ASSETS_DIR.mkdir(exist_ok=True)
SOUNDS_DIR.mkdir(exist_ok=True)
WALLPAPERS_DIR.mkdir(exist_ok=True)
ICONS_DIR.mkdir(exist_ok=True)

# User data directory
USER_DATA_DIR = Path.home() / '.youos'
USER_DATA_DIR.mkdir(exist_ok=True)

# Kernel integration
try:
    from youos_kernel import get_kernel
    KERNEL_AVAILABLE = True
except ImportError:
    KERNEL_AVAILABLE = False
    print("⚠️  YouOS Kernel not available, running in compatibility mode")


class SoundManager:
    """Manages system sounds with fallback"""
    
    def __init__(self):
        self.enabled = True
        self.player = None
        self.audio_output = None
        self.current_sound = None
        self.setup_audio()
        
        print(f"🔊 Sound Manager initialized")
        print(f"📁 Looking for sounds in: {SOUNDS_DIR}")
        
        # List available sounds
        if SOUNDS_DIR.exists():
            sound_files = list(SOUNDS_DIR.glob("*.wav"))
            if sound_files:
                print("✅ Found sound files:")
                for sound_file in sound_files:
                    print(f"   - {sound_file.name}")
            else:
                print("❌ No .wav files found in sounds directory")
    
    def setup_audio(self):
        """Setup audio player"""
        try:
            self.player = QMediaPlayer()
            self.audio_output = QAudioOutput()
            self.player.setAudioOutput(self.audio_output)
            self.audio_output.setVolume(0.7)  # 70% volume
            print("✅ Audio system initialized successfully")
        except Exception as e:
            print(f"⚠️  Failed to initialize audio: {e}")
            self.enabled = False
    
    def play(self, sound_name):
        """Play a sound file"""
        if not self.enabled or not self.player:
            print("⚠️  Audio system not available")
            return
        
        if not sound_name.endswith('.wav'):
            sound_name += '.wav'
        
        sound_path = SOUNDS_DIR / sound_name
        
        if not sound_path.exists():
            print(f"⚠️  Sound file not found: {sound_path}")
            print(f"   Please place {sound_name} in: {SOUNDS_DIR}")
            return
        
        try:
            # Stop any currently playing sound
            if self.player.playbackState() == QMediaPlayer.PlaybackState.PlayingState:
                self.player.stop()
            
            # Set and play the new sound
            url = QUrl.fromLocalFile(str(sound_path.absolute()))
            self.player.setSource(url)
            self.player.play()
            self.current_sound = sound_name
            
            print(f"🔊 Playing sound: {sound_name}")
        except Exception as e:
            print(f"⚠️  Failed to play sound {sound_name}: {e}")
    
    def is_playing(self):
        """Check if sound is currently playing"""
        if self.player:
            return self.player.playbackState() == QMediaPlayer.PlaybackState.PlayingState
        return False
    
    def wait_for_completion(self):
        """Wait for current sound to finish playing"""
        import time
        while self.is_playing():
            time.sleep(0.1)
    
    def set_volume(self, volume):
        """Set volume (0.0 to 1.0)"""
        if self.audio_output:
            self.audio_output.setVolume(max(0.0, min(1.0, volume)))
    
    def mute(self):
        """Mute audio"""
        if self.audio_output:
            self.audio_output.setMuted(True)
    
    def unmute(self):
        """Unmute audio"""
        if self.audio_output:
            self.audio_output.setMuted(False)


# Global sound manager instance
sound_manager = SoundManager()


def play_sound(sound_name):
    """Play a system sound"""
    sound_manager.play(sound_name)


def get_battery_info():
    """Get battery information"""
    try:
        import psutil
        battery = psutil.sensors_battery()
        
        if battery:
            return {
                'percent': battery.percent,
                'plugged': battery.power_plugged,
                'time_left': battery.secsleft if battery.secsleft != psutil.POWER_TIME_UNLIMITED else None
            }
    except ImportError:
        print("⚠️  psutil not installed, battery info unavailable")
    except Exception as e:
        print(f"⚠️  Failed to get battery info: {e}")
    
    return {'percent': 85, 'plugged': False, 'time_left': 3600}


def get_system_stats():
    """Get system statistics (CPU, RAM, Temperature)"""
    stats = {'cpu': 0, 'ram': 0, 'temp': 0}
    
    try:
        import psutil
        
        # CPU usage
        cpu_percent = psutil.cpu_percent(interval=0.1)
        stats['cpu'] = int(cpu_percent)
        
        # RAM usage
        memory = psutil.virtual_memory()
        stats['ram'] = int(memory.percent)
        
        # Temperature
        try:
            temps = psutil.sensors_temperatures()
            if temps:
                if 'coretemp' in temps:
                    temp_list = temps['coretemp']
                    if temp_list:
                        stats['temp'] = int(temp_list[0].current)
                elif 'k10temp' in temps:
                    temp_list = temps['k10temp']
                    if temp_list:
                        stats['temp'] = int(temp_list[0].current)
                elif 'cpu_thermal' in temps:
                    temp_list = temps['cpu_thermal']
                    if temp_list:
                        stats['temp'] = int(temp_list[0].current)
                else:
                    for sensor_name, sensor_list in temps.items():
                        if sensor_list:
                            stats['temp'] = int(sensor_list[0].current)
                            break
        except:
            pass
        
    except ImportError:
        print("⚠️  psutil not installed, system stats unavailable")
    except Exception as e:
        print(f"⚠️  Failed to get system stats: {e}")
    
    return stats


def get_system_info():
    """Get system information"""
    if KERNEL_AVAILABLE:
        try:
            kernel = get_kernel()
            return kernel.get_system_info()
        except:
            pass
    
    # Fallback to basic system info
    import platform
    return {
        'version': 'YouOS 10 Build 26m1.7.3',
        'system': platform.system(),
        'release': platform.release(),
        'version_info': platform.version(),
        'machine': platform.machine(),
        'processor': platform.processor(),
    }


def get_youos_version():
    """Get YouOS version string"""
    if KERNEL_AVAILABLE:
        try:
            kernel = get_kernel()
            return kernel.version
        except:
            pass
    return "YouOS 10 Build 26m1.7.3"


def get_screen_brightness():
    """Get current screen brightness (0-100)"""
    try:
        import screen_brightness_control as sbc
        return sbc.get_brightness()[0]
    except:
        return 50


def set_screen_brightness(value):
    """Set screen brightness (0-100)"""
    try:
        import screen_brightness_control as sbc
        sbc.set_brightness(value)
        return True
    except:
        return False


def get_volume():
    """Get system volume (0-100)"""
    import platform
    import subprocess
    
    system = platform.system()
    
    try:
        if system == "Windows":
            from ctypes import cast, POINTER
            from comtypes import CLSCTX_ALL
            from pycaw.pycaw import AudioUtilities, IAudioEndpointVolume
            
            devices = AudioUtilities.GetSpeakers()
            interface = devices.Activate(IAudioEndpointVolume._iid_, CLSCTX_ALL, None)
            volume = cast(interface, POINTER(IAudioEndpointVolume))
            return int(volume.GetMasterVolumeLevelScalar() * 100)
        
        elif system == "Linux":
            result = subprocess.run(
                ["pactl", "get-sink-volume", "@DEFAULT_SINK@"],
                capture_output=True, text=True
            )
            if result.returncode == 0:
                import re
                match = re.search(r'(\d+)%', result.stdout)
                if match:
                    return int(match.group(1))
        
        elif system == "Darwin":
            result = subprocess.run(
                ["osascript", "-e", "output volume of (get volume settings)"],
                capture_output=True, text=True
            )
            if result.returncode == 0:
                return int(result.stdout.strip())
    except:
        pass
    
    return 50


def set_volume(value):
    """Set system volume (0-100)"""
    import platform
    import subprocess
    
    system = platform.system()
    
    try:
        if system == "Windows":
            from ctypes import cast, POINTER
            from comtypes import CLSCTX_ALL
            from pycaw.pycaw import AudioUtilities, IAudioEndpointVolume
            
            devices = AudioUtilities.GetSpeakers()
            interface = devices.Activate(IAudioEndpointVolume._iid_, CLSCTX_ALL, None)
            volume = cast(interface, POINTER(IAudioEndpointVolume))
            volume.SetMasterVolumeLevelScalar(value / 100.0, None)
            return True
        
        elif system == "Linux":
            subprocess.run(
                ["pactl", "set-sink-volume", "@DEFAULT_SINK@", f"{value}%"],
                capture_output=True
            )
            return True
        
        elif system == "Darwin":
            subprocess.run(
                ["osascript", "-e", f"set volume output volume {value}"],
                capture_output=True
            )
            return True
    except:
        pass
    
    return False


def get_weather(city="Cairo"):
    """Get weather information"""
    try:
        import requests
        
        response = requests.get(f"https://wttr.in/{city}?format=j1", timeout=10)
        
        if response.status_code == 200:
            data = response.json()
            current = data['current_condition'][0]
            
            return {
                'temp_c': current['temp_C'],
                'temp_f': current['temp_F'],
                'description': current['weatherDesc'][0]['value'],
                'icon': get_weather_icon(int(current['weatherCode'])),
                'humidity': current['humidity'],
                'wind_speed': current['windspeedKmph'],
            }
    except:
        pass
    
    return {
        'temp_c': '22',
        'temp_f': '72',
        'description': 'Partly Cloudy',
        'icon': '🌤️',
        'humidity': '60',
        'wind_speed': '15',
    }


def get_weather_icon(weather_code):
    """Get emoji icon for weather code"""
    if weather_code in [113]:
        return "☀️"
    elif weather_code in [116, 119, 122]:
        return "⛅"
    elif weather_code in [143, 248, 260]:
        return "🌫️"
    elif weather_code in [176, 263, 266, 281, 284, 293, 296, 299, 302, 305, 308]:
        return "🌧️"
    elif weather_code in [179, 227, 230, 320, 323, 326, 329, 332, 335, 338, 350]:
        return "❄️"
    elif weather_code in [200, 386, 389]:
        return "⛈️"
    else:
        return "🌤️"


def get_brightness():
    """Get current brightness as a percentage (0-100)"""
    try:
        max_brightness = get_max_brightness()
        with open('/sys/class/backlight/intel_backlight/brightness') as f:
            current_value = int(f.read().strip())
        # Convert actual brightness value to percentage
        percentage = int((current_value / max_brightness) * 100)
        return percentage
    except Exception as e:
        print(f"⚠️  Failed to get brightness: {e}")
        return 50


def set_brightness(percentage):
    """Set brightness as a percentage (0-100)"""
    try:
        max_brightness = get_max_brightness()
        # Convert percentage to actual brightness value
        brightness_value = int((percentage / 100) * max_brightness)
        with open('/sys/class/backlight/intel_backlight/brightness', 'w') as f:
            f.write(str(brightness_value))
    except PermissionError:
        print("⚠️  Permission denied: Run with sudo for brightness control")
    except Exception as e:
        print(f"⚠️  Failed to set brightness: {e}")


def get_max_brightness():
    """Get the maximum brightness value for the backlight device"""
    try:
        with open('/sys/class/backlight/intel_backlight/max_brightness') as f:
            return int(f.read().strip())
    except Exception as e:
        print(f"⚠️  Error getting max brightness: {e}")
        return 100  # Fallback value


__all__ = [
    'ASSETS_DIR', 'SOUNDS_DIR', 'WALLPAPERS_DIR', 'ICONS_DIR',
    'USER_DATA_DIR', 'BASE_DIR', 'SoundManager', 'sound_manager',
    'play_sound', 'get_battery_info', 'get_system_info', 'get_system_stats',
    'get_screen_brightness', 'set_screen_brightness', 'get_volume',
    'set_volume', 'get_weather', 'get_brightness', 'set_brightness',
]