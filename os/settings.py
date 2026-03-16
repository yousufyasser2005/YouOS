"""
YouOS 10 - Settings Application
settings.py - Complete settings app in PyQt6 with glassmorphic design
"""
import sys
import json
import subprocess
import hashlib
import shutil
from pathlib import Path
from PyQt6.QtWidgets import (QApplication, QMainWindow, QWidget, QVBoxLayout,
QHBoxLayout, QLabel, QPushButton, QTabWidget,
QSlider, QListWidget, QLineEdit, QMessageBox, QDialog, QCheckBox,
QGraphicsDropShadowEffect, QFrame, QScrollArea,
QGridLayout, QFileDialog, QInputDialog)
from PyQt6.QtCore import Qt, QTimer, pyqtSignal, QSize
from PyQt6.QtGui import (QFont, QColor, QPainter, QPainterPath, QPen,
QLinearGradient, QPixmap, QIcon)
from profile_widget import ProfilePictureWidget
# Configuration
BASE_DIR = Path(__file__).parent
CONFIG_PATH = BASE_DIR / 'config.json'
APP_DIR = Path.home() / '.youos'
APP_DIR.mkdir(exist_ok=True)
USERS_FILE = APP_DIR / 'users.json'
ASSETS_DIR = BASE_DIR / 'assets'
WALLPAPERS_DIR = ASSETS_DIR / 'wallpapers'
COLORS = {
    'bg_primary': '#0f0f1e',
    'bg_secondary': '#1a1a2e',
    'bg_tertiary': '#252538',
    'accent_primary': '#3b82f6',
    'accent_hover': '#60a5fa',
    'text_primary': '#ffffff',
    'text_secondary': '#9ca3af',
    'border': '#374151',
    'success': '#10b981',
    'error': '#ef4444',
    'warning': '#f59e0b',
}
def load_users():
    if USERS_FILE.exists():
        with open(USERS_FILE, 'r') as f:
            return json.load(f)
    return {}
def save_users(users):
    """Save users to file"""
    with open(USERS_FILE, 'w') as f:
        json.dump(users, f, indent=4)
def hash_password(password):
    """Hash password"""
    return hashlib.sha256(password.encode()).hexdigest()
def get_current_user():
    """Get current logged in user from YouOS config"""
    config = load_config()
    return config.get('current_user', 'admin')
def load_config():
    """Load configuration from file"""
    defaults = {
        "wifi_networks": [],
        "wifi_enabled": False,
        "bluetooth_enabled": False,
        "volume": 50,
        "background": "",
        "brightness": 50,
        "input_language": "en_US.UTF-8"
    }
    if CONFIG_PATH.exists():
        with open(CONFIG_PATH, 'r') as f:
            loaded = json.load(f)
            defaults.update(loaded)
    return defaults
def save_config(config):
    """Save configuration to file"""
    with open(CONFIG_PATH, 'w') as f:
        json.dump(config, f, indent=4)
def get_system_volume():
    """Get system volume"""
    try:
        result = subprocess.run(['amixer', 'get', 'Master'],
            capture_output=True, text=True)
        if result.returncode == 0:
            import re
            match = re.search(r'\[(\d+)%\]', result.stdout)
            if match:
                return int(match.group(1))
    except:
        pass
    return 50
def set_system_volume(volume):
    """Set system volume"""
    try:
        subprocess.run(['amixer', 'set', 'Master', f'{volume}%'], check=True)
    except:
        pass
def get_brightness():
    """Get current brightness"""
    try:
        with open('/sys/class/backlight/intel_backlight/brightness') as f:
            current = int(f.read().strip())
        with open('/sys/class/backlight/intel_backlight/max_brightness') as f:
            maximum = int(f.read().strip())
        return int((current / maximum) * 100)
    except:
        return 50
def set_brightness(percentage):
    """Set brightness"""
    try:
        with open('/sys/class/backlight/intel_backlight/max_brightness') as f:
            max_brightness = int(f.read().strip())
        brightness_value = int((percentage / 100) * max_brightness)
        with open('/sys/class/backlight/intel_backlight/brightness', 'w') as f:
            f.write(str(brightness_value))
    except:
        pass
class GlassFrame(QFrame):
    """Frame with glassmorphism effect"""
    def __init__(self, parent=None, opacity=0.1):
        super().__init__(parent)
        self.opacity = opacity
        self.setAttribute(Qt.WidgetAttribute.WA_TranslucentBackground)
        shadow = QGraphicsDropShadowEffect()
        shadow.setBlurRadius(30)
        shadow.setColor(QColor(0, 0, 0, 100))
        shadow.setOffset(0, 5)
        self.setGraphicsEffect(shadow)
    def paintEvent(self, event):
        from PyQt6.QtCore import QRectF
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        path = QPainterPath()
        path.addRoundedRect(QRectF(self.rect()), 16, 16)
        gradient = QLinearGradient(0, 0, 0, self.height())
        gradient.setColorAt(0, QColor(255, 255, 255, int(255 * self.opacity)))
        gradient.setColorAt(1, QColor(255, 255, 255, int(255 * self.opacity * 0.5)))
        painter.fillPath(path, gradient)
        painter.setPen(QPen(QColor(255, 255, 255, 51), 1))
        painter.drawPath(path)
class SettingCard(GlassFrame):
    """Individual setting card"""
    def __init__(self, title, icon="⚙️", parent=None):
        super().__init__(parent, opacity=0.12)
        self.setup_ui(title, icon)
    def setup_ui(self, title, icon):
        layout = QHBoxLayout(self)
        layout.setContentsMargins(20, 15, 20, 15)
        layout.setSpacing(15)
        # Icon
        icon_label = QLabel(icon)
        icon_label.setStyleSheet("font-size: 24px;")
        layout.addWidget(icon_label)
        # Title
        title_label = QLabel(title)
        title_label.setStyleSheet(f"""
            color: {COLORS['text_primary']};
            font-size: 14px;
            font-weight: bold;
        """)
        layout.addWidget(title_label)
        layout.addStretch()
        # Content area (to be added by subclasses)
        self.content_layout = QHBoxLayout()
        layout.addLayout(self.content_layout)
class NetworkTab(QWidget):
    """Network settings tab"""
    def __init__(self, config, parent=None):
        super().__init__(parent)
        self.config = config
        self.setup_ui()
    def setup_ui(self):
        layout = QVBoxLayout(self)
        layout.setContentsMargins(20, 20, 20, 20)
        layout.setSpacing(20)
        # WiFi Card
        wifi_card = SettingCard("Wi-Fi", "📡")
        self.wifi_toggle = QCheckBox("Enabled")
        self.wifi_toggle.setChecked(self.config.get("wifi_enabled", False))
        self.wifi_toggle.stateChanged.connect(self.toggle_wifi)
        wifi_card.content_layout.addWidget(self.wifi_toggle)
        wifi_btn = QPushButton("Configure")
        wifi_btn.setFixedHeight(35)
        wifi_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        wifi_btn.setStyleSheet(f"""
            QPushButton {{
                background: {COLORS['accent_primary']};
                color: white;
                border: none;
                border-radius: 6px;
                padding: 0 20px;
                font-size: 12px;
}}
            QPushButton:hover {{
                background: {COLORS['accent_hover']};
}}
        """)
        wifi_btn.clicked.connect(self.configure_wifi)
        wifi_card.content_layout.addWidget(wifi_btn)
        layout.addWidget(wifi_card)
        # Bluetooth Card
        bluetooth_card = SettingCard("Bluetooth", "🔵")
        self.bt_toggle = QCheckBox("Enabled")
        self.bt_toggle.setChecked(self.config.get("bluetooth_enabled", False))
        self.bt_toggle.stateChanged.connect(self.toggle_bt)
        bluetooth_card.content_layout.addWidget(self.bt_toggle)
        bt_btn = QPushButton("Configure")
        bt_btn.setFixedHeight(35)
        bt_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        bt_btn.setStyleSheet(f"""
            QPushButton {{
                background: {COLORS['accent_primary']};
                color: white;
                border: none;
                border-radius: 6px;
                padding: 0 20px;
                font-size: 12px;
}}
            QPushButton:hover {{
                background: {COLORS['accent_hover']};
}}
        """)
        bt_btn.clicked.connect(self.configure_bluetooth)
        bluetooth_card.content_layout.addWidget(bt_btn)
        layout.addWidget(bluetooth_card)
        layout.addStretch()
    def toggle_wifi(self, state):
        self.config["wifi_enabled"] = bool(state)
        save_config(self.config)
        try:
            status = 'on' if state else 'off'
            subprocess.run(['nmcli', 'radio', 'wifi', status], check=True)
        except:
            pass
    def configure_wifi(self):
        try:
            result = subprocess.run(['nmcli', 'dev', 'wifi', 'list'], capture_output=True, text=True)
            if result.returncode == 0:
                lines = result.stdout.splitlines()
                networks = []
                for line in lines[1:]:
                    if line.strip():
                        parts = line.split()
                        ssid = parts[1] if parts[0] == '*' else parts[0]
                        networks.append(ssid)
            else:
                networks = []
        except:
            networks = []
        dialog = QDialog(self)
        dialog.setWindowTitle("Wi-Fi Networks")
        dialog.setStyleSheet(f"background: {COLORS['bg_primary']}; color: {COLORS['text_primary']};")
        layout = QVBoxLayout(dialog)
        list_widget = QListWidget()
        for net in networks:
            list_widget.addItem(net)
        layout.addWidget(list_widget)
        connect_btn = QPushButton("Connect")
        connect_btn.clicked.connect(lambda: self.connect_to_wifi(list_widget.currentItem().text(), dialog) if list_widget.currentItem() else None)
        layout.addWidget(connect_btn)
        dialog.exec()
    def connect_to_wifi(self, ssid, dialog):
        pw, ok = QInputDialog.getText(self, "Password", "Enter Wi-Fi password:", QLineEdit.EchoMode.Password)
        if ok:
            try:
                subprocess.run(['nmcli', 'dev', 'wifi', 'connect', ssid, 'password', pw], check=True)
                QMessageBox.information(self, "Success", f"Connected to {ssid}")
                self.config["wifi_networks"].append(ssid)
                save_config(self.config)
            except Exception as e:
                QMessageBox.warning(self, "Error", f"Failed to connect: {str(e)}")
            dialog.close()
    def toggle_bt(self, state):
        self.config["bluetooth_enabled"] = bool(state)
        save_config(self.config)
        try:
            status = 'on' if state else 'off'
            subprocess.run(['bluetoothctl', 'power', status], check=True)
        except:
            pass
    def configure_bluetooth(self):
        try:
            result = subprocess.run(['bluetoothctl', 'devices'], capture_output=True, text=True)
            lines = result.stdout.splitlines()
            devices = [line.split(maxsplit=2)[2] for line in lines if line.strip()]
        except:
            devices = []
        dialog = QDialog(self)
        dialog.setWindowTitle("Bluetooth Devices")
        dialog.setStyleSheet(f"background: {COLORS['bg_primary']}; color: {COLORS['text_primary']};")
        layout = QVBoxLayout(dialog)
        list_widget = QListWidget()
        for dev in devices:
            list_widget.addItem(dev)
        layout.addWidget(list_widget)
        dialog.exec()
class DeviceTab(QWidget):
    """Device settings tab"""
    def __init__(self, config, parent=None):
        super().__init__(parent)
        self.config = config
        self.setup_ui()
    def setup_ui(self):
        layout = QVBoxLayout(self)
        layout.setContentsMargins(20, 20, 20, 20)
        layout.setSpacing(20)
        # Volume Card
        volume_card = GlassFrame(opacity=0.12)
        vol_layout = QVBoxLayout(volume_card)
        vol_layout.setContentsMargins(20, 15, 20, 15)
        vol_layout.setSpacing(10)
        vol_header = QHBoxLayout()
        vol_icon = QLabel("🔊")
        vol_icon.setStyleSheet("font-size: 24px;")
        vol_header.addWidget(vol_icon)
        vol_title = QLabel("Volume")
        vol_title.setStyleSheet(f"color: {COLORS['text_primary']}; font-size: 14px; font-weight: bold;")
        vol_header.addWidget(vol_title)
        vol_header.addStretch()
        self.vol_value_label = QLabel(f"{get_system_volume()}%")
        self.vol_value_label.setStyleSheet(f"color: {COLORS['text_secondary']}; font-size: 12px;")
        vol_header.addWidget(self.vol_value_label)
        vol_layout.addLayout(vol_header)
        self.vol_slider = QSlider(Qt.Orientation.Horizontal)
        self.vol_slider.setRange(0, 100)
        self.vol_slider.setValue(get_system_volume())
        self.vol_slider.setStyleSheet("""
            QSlider::groove:horizontal {
                background: rgba(255, 255, 255, 0.2);
                height: 8px;
                border-radius: 4px;
            }
            QSlider::handle:horizontal {
                background: rgba(59, 130, 246, 0.9);
                width: 20px;
                margin: -6px 0;
                border-radius: 10px;
            }
        """)
        self.vol_slider.valueChanged.connect(self.on_volume_change)
        vol_layout.addWidget(self.vol_slider)
        layout.addWidget(volume_card)
        # Brightness Card
        bright_card = GlassFrame(opacity=0.12)
        bright_layout = QVBoxLayout(bright_card)
        bright_layout.setContentsMargins(20, 15, 20, 15)
        bright_layout.setSpacing(10)
        bright_header = QHBoxLayout()
        bright_icon = QLabel("🔆")
        bright_icon.setStyleSheet("font-size: 24px;")
        bright_header.addWidget(bright_icon)
        bright_title = QLabel("Brightness")
        bright_title.setStyleSheet(f"color: {COLORS['text_primary']}; font-size: 14px; font-weight: bold;")
        bright_header.addWidget(bright_title)
        bright_header.addStretch()
        self.bright_value_label = QLabel(f"{get_brightness()}%")
        self.bright_value_label.setStyleSheet(f"color: {COLORS['text_secondary']}; font-size: 12px;")
        bright_header.addWidget(self.bright_value_label)
        bright_layout.addLayout(bright_header)
        self.bright_slider = QSlider(Qt.Orientation.Horizontal)
        self.bright_slider.setRange(0, 100)
        self.bright_slider.setValue(get_brightness())
        self.bright_slider.setStyleSheet("""
            QSlider::groove:horizontal {
                background: rgba(255, 255, 255, 0.2);
                height: 8px;
                border-radius: 4px;
            }
            QSlider::handle:horizontal {
                background: rgba(59, 130, 246, 0.9);
                width: 20px;
                margin: -6px 0;
                border-radius: 10px;
            }
        """)
        self.bright_slider.valueChanged.connect(self.on_brightness_change)
        bright_layout.addWidget(self.bright_slider)
        layout.addWidget(bright_card)
        # Battery Card
        battery_card = SettingCard("Battery Status", "🔋")
        battery_info = QPushButton("View Info")
        battery_info.setFixedHeight(35)
        battery_info.setCursor(Qt.CursorShape.PointingHandCursor)
        battery_info.setStyleSheet(f"""
            QPushButton {{
                background: {COLORS['accent_primary']};
                color: white;
                border: none;
                border-radius: 6px;
                padding: 0 20px;
                font-size: 12px;
}}
            QPushButton:hover {{
                background: {COLORS['accent_hover']};
}}
        """)
        battery_info.clicked.connect(self.show_battery_info)
        battery_card.content_layout.addWidget(battery_info)
        layout.addWidget(battery_card)
        # Programs Card
        programs_card = SettingCard("Programs", "📦")
        programs_btn = QPushButton("Manage")
        programs_btn.setFixedHeight(35)
        programs_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        programs_btn.setStyleSheet(f"""
            QPushButton {{
                background: {COLORS['accent_primary']};
                color: white;
                border: none;
                border-radius: 6px;
                padding: 0 20px;
                font-size: 12px;
}}
            QPushButton:hover {{
                background: {COLORS['accent_hover']};
}}
        """)
        programs_btn.clicked.connect(self.show_programs)
        programs_card.content_layout.addWidget(programs_btn)
        layout.addWidget(programs_card)
        layout.addStretch()
    def on_volume_change(self, value):
        set_system_volume(value)
        self.vol_value_label.setText(f"{value}%")
        self.config["volume"] = value
        save_config(self.config)
    def on_brightness_change(self, value):
        set_brightness(value)
        self.bright_value_label.setText(f"{value}%")
        self.config["brightness"] = value
        save_config(self.config)
    def show_battery_info(self):
        try:
            result = subprocess.run(['upower', '-i', '/org/freedesktop/UPower/devices/battery_BAT0'],
                capture_output=True, text=True)
            QMessageBox.information(self, "Battery Info", result.stdout if result.stdout else "Battery info unavailable")
        except:
            QMessageBox.information(self, "Battery Info", "Unable to read battery information")
    
    def show_programs(self):
        """Show installed programs with uninstall option"""
        dialog = QDialog(self)
        dialog.setWindowTitle("Installed Programs")
        dialog.setFixedSize(600, 500)
        dialog.setStyleSheet(f"background: {COLORS['bg_primary']}; color: {COLORS['text_primary']};")
        layout = QVBoxLayout(dialog)
        layout.setContentsMargins(20, 20, 20, 20)
        layout.setSpacing(15)
        
        title = QLabel("Installed Programs")
        title.setStyleSheet(f"color: {COLORS['text_primary']}; font-size: 18px; font-weight: bold;")
        layout.addWidget(title)
        
        # Programs list
        scroll_area = QScrollArea()
        scroll_area.setWidgetResizable(True)
        scroll_area.setStyleSheet(f"""
            QScrollArea {{
                background: {COLORS['bg_secondary']};
                border: 1px solid {COLORS['border']};
                border-radius: 8px;
            }}
        """)
        
        programs_widget = QWidget()
        programs_layout = QVBoxLayout(programs_widget)
        programs_layout.setSpacing(5)
        
        # Get installed programs
        installed_programs = self.get_installed_programs()
        
        for program in installed_programs:
            program_frame = QFrame()
            program_frame.setFixedHeight(50)
            program_frame.setStyleSheet(f"""
                QFrame {{
                    background: rgba(255, 255, 255, 0.05);
                    border: 1px solid rgba(255, 255, 255, 0.1);
                    border-radius: 6px;
                    padding: 5px;
                }}
                QFrame:hover {{
                    background: rgba(255, 255, 255, 0.1);
                }}
            """)
            
            frame_layout = QHBoxLayout(program_frame)
            frame_layout.setContentsMargins(10, 5, 10, 5)
            
            # Program name
            name_label = QLabel(program['name'])
            name_label.setStyleSheet(f"color: {COLORS['text_primary']}; font-size: 14px; font-weight: bold;")
            frame_layout.addWidget(name_label)
            
            # Version (if available)
            if program.get('version'):
                version_label = QLabel(f"v{program['version']}")
                version_label.setStyleSheet(f"color: {COLORS['text_secondary']}; font-size: 12px;")
                frame_layout.addWidget(version_label)
            
            frame_layout.addStretch()
            
            # Uninstall button (only for user-installed packages)
            if program.get('removable', True):
                uninstall_btn = QPushButton("Uninstall")
                uninstall_btn.setFixedHeight(30)
                uninstall_btn.setFixedWidth(80)
                uninstall_btn.setCursor(Qt.CursorShape.PointingHandCursor)
                uninstall_btn.setStyleSheet(f"""
                    QPushButton {{
                        background: {COLORS['error']};
                        color: white;
                        border: none;
                        border-radius: 4px;
                        font-size: 11px;
                    }}
                    QPushButton:hover {{
                        background: #dc2626;
                    }}
                """)
                uninstall_btn.clicked.connect(lambda checked, p=program: self.uninstall_program(p, dialog))
                frame_layout.addWidget(uninstall_btn)
            else:
                system_label = QLabel("System")
                system_label.setStyleSheet(f"color: {COLORS['text_secondary']}; font-size: 11px;")
                frame_layout.addWidget(system_label)
            
            programs_layout.addWidget(program_frame)
        
        scroll_area.setWidget(programs_widget)
        layout.addWidget(scroll_area)
        
        # Close button
        close_btn = QPushButton("Close")
        close_btn.setFixedHeight(35)
        close_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        close_btn.setStyleSheet(f"""
            QPushButton {{
                background: {COLORS['bg_tertiary']};
                color: {COLORS['text_primary']};
                border: 1px solid {COLORS['border']};
                border-radius: 6px;
                padding: 0 20px;
                font-size: 12px;
            }}
            QPushButton:hover {{
                background: {COLORS['border']};
            }}
        """)
        close_btn.clicked.connect(dialog.close)
        layout.addWidget(close_btn)
        
        dialog.exec()
    
    def get_installed_programs(self):
        """Get list of installed programs from YouOS programs directory"""
        programs = []
        programs_dir = BASE_DIR / 'programs'
        
        if programs_dir.exists():
            for program_path in programs_dir.iterdir():
                if program_path.is_dir():
                    # Check for program info file
                    info_file = program_path / 'info.json'
                    if info_file.exists():
                        try:
                            with open(info_file, 'r') as f:
                                info = json.load(f)
                            programs.append({
                                'name': info.get('name', program_path.name),
                                'version': info.get('version', '1.0'),
                                'path': str(program_path),
                                'removable': True
                            })
                        except:
                            # Fallback if info.json is invalid
                            programs.append({
                                'name': program_path.name.replace('_', ' ').title(),
                                'path': str(program_path),
                                'removable': True
                            })
                    else:
                        # No info file, use directory name
                        programs.append({
                            'name': program_path.name.replace('_', ' ').title(),
                            'path': str(program_path),
                            'removable': True
                        })
        
        return sorted(programs, key=lambda x: x['name'].lower())
    
    def uninstall_program(self, program, parent_dialog):
        """Uninstall selected program from YouOS programs directory"""
        reply = QMessageBox.question(
            parent_dialog, 
            "Uninstall Program", 
            f"Are you sure you want to uninstall '{program['name']}'?",
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No
        )
        
        if reply == QMessageBox.StandardButton.Yes:
            try:
                import shutil
                program_path = Path(program['path'])
                if program_path.exists():
                    shutil.rmtree(program_path)
                    QMessageBox.information(parent_dialog, "Success", f"'{program['name']}' has been uninstalled successfully!")
                    parent_dialog.close()
                    self.show_programs()
                else:
                    QMessageBox.warning(parent_dialog, "Error", f"Program directory not found: {program_path}")
            except Exception as e:
                QMessageBox.critical(parent_dialog, "Error", f"Failed to uninstall '{program['name']}':\n{str(e)}")
class SecurityTab(QWidget):
    """Security settings tab"""
    def __init__(self, config, parent=None):
        super().__init__(parent)
        self.config = config
        self.current_user = get_current_user()
        self.setup_ui()
    def setup_ui(self):
        layout = QVBoxLayout(self)
        layout.setContentsMargins(20, 20, 20, 20)
        layout.setSpacing(20)
        # Current user info
        user_card = GlassFrame(opacity=0.12)
        user_layout = QHBoxLayout(user_card)
        user_layout.setContentsMargins(20, 15, 20, 15)
        user_icon = QLabel("👤")
        user_icon.setStyleSheet("font-size: 24px;")
        user_layout.addWidget(user_icon)
        user_info_layout = QVBoxLayout()
        user_label = QLabel("Current User")
        user_label.setStyleSheet(f"color: {COLORS['text_secondary']}; font-size: 11px;")
        user_info_layout.addWidget(user_label)
        username_label = QLabel(self.current_user)
        username_label.setStyleSheet(f"color: {COLORS['text_primary']}; font-size: 14px; font-weight: bold;")
        user_info_layout.addWidget(username_label)
        user_layout.addLayout(user_info_layout)
        user_layout.addStretch()
        layout.addWidget(user_card)
        
        # User Management Card
        users_card = SettingCard("User Management", "👥")
        users_btn = QPushButton("Manage")
        users_btn.setFixedHeight(35)
        users_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        users_btn.setStyleSheet(f"""
            QPushButton {{
                background: {COLORS['accent_primary']};
                color: white;
                border: none;
                border-radius: 6px;
                padding: 0 20px;
                font-size: 12px;
}}
            QPushButton:hover {{
                background: {COLORS['accent_hover']};
}}
        """)
        users_btn.clicked.connect(self.manage_users)
        users_card.content_layout.addWidget(users_btn)
        layout.addWidget(users_card)
        
        # Biometric Card
        biometric_card = SettingCard("Biometric Authentication", "🔐")
        biometric_btn = QPushButton("Manage")
        biometric_btn.setFixedHeight(35)
        biometric_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        biometric_btn.setStyleSheet(f"""
            QPushButton {{
                background: {COLORS['accent_primary']};
                color: white;
                border: none;
                border-radius: 6px;
                padding: 0 20px;
                font-size: 12px;
}}
            QPushButton:hover {{
                background: {COLORS['accent_hover']};
}}
        """)
        biometric_btn.clicked.connect(self.manage_biometric)
        biometric_card.content_layout.addWidget(biometric_btn)
        layout.addWidget(biometric_card)
        
        # Security Logs Card
        logs_card = SettingCard("Security Logs", "📋")
        logs_btn = QPushButton("View")
        logs_btn.setFixedHeight(35)
        logs_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        logs_btn.setStyleSheet(f"""
            QPushButton {{
                background: {COLORS['accent_primary']};
                color: white;
                border: none;
                border-radius: 6px;
                padding: 0 20px;
                font-size: 12px;
}}
            QPushButton:hover {{
                background: {COLORS['accent_hover']};
}}
        """)
        logs_btn.clicked.connect(self.view_logs)
        logs_card.content_layout.addWidget(logs_btn)
        layout.addWidget(logs_card)
        layout.addStretch()
    def manage_users(self):
        """Manage current user profile only"""
        dialog = QDialog(self)
        dialog.setWindowTitle("My Profile")
        dialog.setFixedSize(450, 400)
        dialog.setStyleSheet(f"background: {COLORS['bg_primary']}; color: {COLORS['text_primary']};")
        layout = QVBoxLayout(dialog)
        layout.setContentsMargins(30, 30, 30, 30)
        layout.setSpacing(20)
        
        # Title
        title = QLabel("My Profile")
        title.setStyleSheet(f"color: {COLORS['text_primary']}; font-size: 20px; font-weight: bold;")
        title.setAlignment(Qt.AlignmentFlag.AlignCenter)
        layout.addWidget(title)
        
        # User info card
        users = load_users()
        user_data = users.get(self.current_user, {})
        
        info_frame = QFrame()
        info_frame.setStyleSheet(f"""
            QFrame {{
                background: rgba(255, 255, 255, 0.05);
                border: 1px solid rgba(255, 255, 255, 0.1);
                border-radius: 8px;
                padding: 20px;
            }}
        """)
        info_layout = QVBoxLayout(info_frame)
        info_layout.setSpacing(15)
        
        # Profile picture
        pic_layout = QHBoxLayout()
        pic_layout.addStretch()
        profile_pic = ProfilePictureWidget(
            username=self.current_user,
            profile_picture_path=user_data.get('profile_picture', ''),
            size=80
        )
        pic_layout.addWidget(profile_pic)
        pic_layout.addStretch()
        info_layout.addLayout(pic_layout)
        
        # Username
        username_label = QLabel(self.current_user)
        username_label.setStyleSheet(f"color: {COLORS['text_primary']}; font-size: 18px; font-weight: bold;")
        username_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        info_layout.addWidget(username_label)
        
        layout.addWidget(info_frame)
        
        # Action buttons
        change_pw_btn = QPushButton("Change Password")
        change_pw_btn.setFixedHeight(45)
        change_pw_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        change_pw_btn.setStyleSheet(f"""
            QPushButton {{
                background: {COLORS['accent_primary']};
                color: white;
                border: none;
                border-radius: 8px;
                font-size: 14px;
                font-weight: bold;
            }}
            QPushButton:hover {{
                background: {COLORS['accent_hover']};
            }}
        """)
        change_pw_btn.clicked.connect(lambda: self.change_user_password(self.current_user))
        layout.addWidget(change_pw_btn)
        
        profile_btn = QPushButton("Change Profile Picture")
        profile_btn.setFixedHeight(45)
        profile_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        profile_btn.setStyleSheet(f"""
            QPushButton {{
                background: {COLORS['warning']};
                color: white;
                border: none;
                border-radius: 8px;
                font-size: 14px;
                font-weight: bold;
            }}
            QPushButton:hover {{
                background: #d97706;
            }}
        """)
        profile_btn.clicked.connect(lambda: self.set_profile_picture(self.current_user) and dialog.close() and self.manage_users())
        layout.addWidget(profile_btn)
        
        layout.addStretch()
        
        # Close button
        close_btn = QPushButton("Close")
        close_btn.setFixedHeight(40)
        close_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        close_btn.setStyleSheet(f"""
            QPushButton {{
                background: {COLORS['bg_tertiary']};
                color: {COLORS['text_primary']};
                border: 1px solid {COLORS['border']};
                border-radius: 8px;
                padding: 0 20px;
                font-size: 12px;
            }}
            QPushButton:hover {{
                background: {COLORS['border']};
            }}
        """)
        close_btn.clicked.connect(dialog.close)
        layout.addWidget(close_btn)
        
        dialog.exec()
    
    def add_user(self, users_widget, users_layout):
        # Create user dialog
        dialog = QDialog(self)
        dialog.setWindowTitle("Create New User")
        dialog.setFixedSize(400, 300)
        dialog.setStyleSheet(f"background: {COLORS['bg_primary']}; color: {COLORS['text_primary']};")
        layout = QVBoxLayout(dialog)
        layout.setContentsMargins(30, 30, 30, 30)
        layout.setSpacing(20)
        
        # Title
        title = QLabel("Create New User")
        title.setStyleSheet(f"color: {COLORS['text_primary']}; font-size: 18px; font-weight: bold;")
        title.setAlignment(Qt.AlignmentFlag.AlignCenter)
        layout.addWidget(title)
        
        # Username input
        username_label = QLabel("Username:")
        username_label.setStyleSheet(f"color: {COLORS['text_secondary']}; font-size: 12px;")
        layout.addWidget(username_label)
        
        username_input = QLineEdit()
        username_input.setPlaceholderText("Enter username (min 3 characters)")
        username_input.setFixedHeight(35)
        username_input.setStyleSheet(f"""
            QLineEdit {{
                background: {COLORS['bg_secondary']};
                color: {COLORS['text_primary']};
                border: 1px solid {COLORS['border']};
                border-radius: 6px;
                padding: 8px;
                font-size: 12px;
            }}
            QLineEdit:focus {{
                border-color: {COLORS['accent_primary']};
            }}
        """)
        layout.addWidget(username_input)
        
        # Password input
        password_label = QLabel("Password:")
        password_label.setStyleSheet(f"color: {COLORS['text_secondary']}; font-size: 12px;")
        layout.addWidget(password_label)
        
        password_input = QLineEdit()
        password_input.setPlaceholderText("Enter password (min 4 characters)")
        password_input.setEchoMode(QLineEdit.EchoMode.Password)
        password_input.setFixedHeight(35)
        password_input.setStyleSheet(f"""
            QLineEdit {{
                background: {COLORS['bg_secondary']};
                color: {COLORS['text_primary']};
                border: 1px solid {COLORS['border']};
                border-radius: 6px;
                padding: 8px;
                font-size: 12px;
            }}
            QLineEdit:focus {{
                border-color: {COLORS['accent_primary']};
            }}
        """)
        layout.addWidget(password_input)
        
        # Confirm password input
        confirm_label = QLabel("Confirm Password:")
        confirm_label.setStyleSheet(f"color: {COLORS['text_secondary']}; font-size: 12px;")
        layout.addWidget(confirm_label)
        
        confirm_input = QLineEdit()
        confirm_input.setPlaceholderText("Confirm password")
        confirm_input.setEchoMode(QLineEdit.EchoMode.Password)
        confirm_input.setFixedHeight(35)
        confirm_input.setStyleSheet(f"""
            QLineEdit {{
                background: {COLORS['bg_secondary']};
                color: {COLORS['text_primary']};
                border: 1px solid {COLORS['border']};
                border-radius: 6px;
                padding: 8px;
                font-size: 12px;
            }}
            QLineEdit:focus {{
                border-color: {COLORS['accent_primary']};
            }}
        """)
        layout.addWidget(confirm_input)
        
        # Button layout
        btn_layout = QHBoxLayout()
        
        cancel_btn = QPushButton("Cancel")
        cancel_btn.setFixedHeight(35)
        cancel_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        cancel_btn.setStyleSheet(f"""
            QPushButton {{
                background: {COLORS['bg_tertiary']};
                color: {COLORS['text_primary']};
                border: 1px solid {COLORS['border']};
                border-radius: 6px;
                padding: 0 20px;
                font-size: 12px;
            }}
            QPushButton:hover {{
                background: {COLORS['border']};
            }}
        """)
        cancel_btn.clicked.connect(dialog.close)
        btn_layout.addWidget(cancel_btn)
        
        create_btn = QPushButton("Create User")
        create_btn.setFixedHeight(35)
        create_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        create_btn.setStyleSheet(f"""
            QPushButton {{
                background: {COLORS['success']};
                color: white;
                border: none;
                border-radius: 6px;
                padding: 0 20px;
                font-size: 12px;
                font-weight: bold;
            }}
            QPushButton:hover {{
                background: #059669;
            }}
        """)
        
        def create_user_action():
            username = username_input.text().strip()
            password = password_input.text()
            confirm = confirm_input.text()
            
            # Validation
            if not username:
                QMessageBox.warning(dialog, "Error", "Username is required!")
                return
            
            if len(username) < 3:
                QMessageBox.warning(dialog, "Error", "Username must be at least 3 characters!")
                return
            
            if not password:
                QMessageBox.warning(dialog, "Error", "Password is required!")
                return
            
            if len(password) < 4:
                QMessageBox.warning(dialog, "Error", "Password must be at least 4 characters!")
                return
            
            if password != confirm:
                QMessageBox.warning(dialog, "Error", "Passwords do not match!")
                return
            
            # Check if user exists
            users = load_users()
            if username in users:
                QMessageBox.warning(dialog, "Error", "Username already exists!")
                return
            
            # Create user
            users[username] = {
                "password": hash_password(password),
                "theme": "dark",
                "wallpaper": "default.jpg",
                "profile_picture": "",
                "pinned_apps": [],
                "desktop_icons": {},
                "icon_positions": {},
                "window_positions": {}
            }
            save_users(users)
            
            # Update list widget
            self.refresh_user_list(users_widget, users_layout)
            
            QMessageBox.information(dialog, "Success", f"User '{username}' created successfully!")
            dialog.close()
        
        create_btn.clicked.connect(create_user_action)
        btn_layout.addWidget(create_btn)
        
        layout.addLayout(btn_layout)
        
        dialog.exec()
    

    
    def change_user_password(self, username):
        """Change password for current user only"""
        if username != self.current_user:
            QMessageBox.warning(self, "Error", "You can only change your own password!")
            return
            
        users = load_users()
        if username not in users:
            QMessageBox.warning(self, "Error", f"User '{username}' not found!")
            return
        
        # Create password change dialog
        dialog = QDialog(self)
        dialog.setWindowTitle(f"Change Password - {username}")
        dialog.setFixedSize(400, 250)
        dialog.setStyleSheet(f"background: {COLORS['bg_primary']}; color: {COLORS['text_primary']};")
        layout = QVBoxLayout(dialog)
        layout.setContentsMargins(30, 30, 30, 30)
        layout.setSpacing(20)
        
        # Title
        title = QLabel(f"Change Password for {username}")
        title.setStyleSheet(f"color: {COLORS['text_primary']}; font-size: 16px; font-weight: bold;")
        title.setAlignment(Qt.AlignmentFlag.AlignCenter)
        layout.addWidget(title)
        
        # New password input
        new_label = QLabel("New Password:")
        new_label.setStyleSheet(f"color: {COLORS['text_secondary']}; font-size: 12px;")
        layout.addWidget(new_label)
        
        new_input = QLineEdit()
        new_input.setPlaceholderText("Enter new password (min 4 characters)")
        new_input.setEchoMode(QLineEdit.EchoMode.Password)
        new_input.setFixedHeight(35)
        new_input.setStyleSheet(f"""
            QLineEdit {{
                background: {COLORS['bg_secondary']};
                color: {COLORS['text_primary']};
                border: 1px solid {COLORS['border']};
                border-radius: 6px;
                padding: 8px;
                font-size: 12px;
            }}
            QLineEdit:focus {{
                border-color: {COLORS['accent_primary']};
            }}
        """)
        layout.addWidget(new_input)
        
        # Confirm password input
        confirm_label = QLabel("Confirm Password:")
        confirm_label.setStyleSheet(f"color: {COLORS['text_secondary']}; font-size: 12px;")
        layout.addWidget(confirm_label)
        
        confirm_input = QLineEdit()
        confirm_input.setPlaceholderText("Confirm new password")
        confirm_input.setEchoMode(QLineEdit.EchoMode.Password)
        confirm_input.setFixedHeight(35)
        confirm_input.setStyleSheet(f"""
            QLineEdit {{
                background: {COLORS['bg_secondary']};
                color: {COLORS['text_primary']};
                border: 1px solid {COLORS['border']};
                border-radius: 6px;
                padding: 8px;
                font-size: 12px;
            }}
            QLineEdit:focus {{
                border-color: {COLORS['accent_primary']};
            }}
        """)
        layout.addWidget(confirm_input)
        
        # Button layout
        btn_layout = QHBoxLayout()
        
        cancel_btn = QPushButton("Cancel")
        cancel_btn.setFixedHeight(35)
        cancel_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        cancel_btn.setStyleSheet(f"""
            QPushButton {{
                background: {COLORS['bg_tertiary']};
                color: {COLORS['text_primary']};
                border: 1px solid {COLORS['border']};
                border-radius: 6px;
                padding: 0 20px;
                font-size: 12px;
            }}
            QPushButton:hover {{
                background: {COLORS['border']};
            }}
        """)
        cancel_btn.clicked.connect(dialog.close)
        btn_layout.addWidget(cancel_btn)
        
        change_btn = QPushButton("Change Password")
        change_btn.setFixedHeight(35)
        change_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        change_btn.setStyleSheet(f"""
            QPushButton {{
                background: {COLORS['accent_primary']};
                color: white;
                border: none;
                border-radius: 6px;
                padding: 0 20px;
                font-size: 12px;
                font-weight: bold;
            }}
            QPushButton:hover {{
                background: {COLORS['accent_hover']};
            }}
        """)
        
        def change_password_action():
            new_pw = new_input.text()
            confirm_pw = confirm_input.text()
            
            if not new_pw:
                QMessageBox.warning(dialog, "Error", "New password is required!")
                return
            
            if len(new_pw) < 4:
                QMessageBox.warning(dialog, "Error", "Password must be at least 4 characters!")
                return
            
            if new_pw != confirm_pw:
                QMessageBox.warning(dialog, "Error", "Passwords do not match!")
                return
            
            users[username]['password'] = hash_password(new_pw)
            save_users(users)
            
            QMessageBox.information(dialog, "Success", f"Password changed successfully for user '{username}'!")
            dialog.close()
        
        change_btn.clicked.connect(change_password_action)
        btn_layout.addWidget(change_btn)
        
        layout.addLayout(btn_layout)
        
        dialog.exec()
    def view_logs(self):
        try:
            result = subprocess.run(['tail', '-20', '/var/log/auth.log'], capture_output=True, text=True)
            QMessageBox.information(self, "Security Logs", result.stdout if result.stdout else "No logs available")
        except:
            QMessageBox.warning(self, "Error", "Unable to read security logs")
    
    def set_profile_picture(self, username):
        """Set profile picture for current user only"""
        if username != self.current_user:
            QMessageBox.warning(self, "Error", "You can only change your own profile picture!")
            return False
            
        users = load_users()
        if username not in users:
            QMessageBox.warning(self, "Error", f"User '{username}' not found!")
            return False
        
        # File dialog to select image
        file_path, _ = QFileDialog.getOpenFileName(
            self, 
            "Select Profile Picture",
            str(Path.home()),
            "Images (*.png *.jpg *.jpeg *.bmp *.gif)"
        )
        
        if file_path:
            # Save profile picture path
            users[username]['profile_picture'] = file_path
            save_users(users)
            
            QMessageBox.information(self, "Success", "Profile picture updated successfully!")
            return True
        return False
    
    def manage_biometric(self):
        """Manage biometric settings"""
        try:
            from face_recognition_module import FaceRecognitionEngine, FaceEnrollmentDialog
            
            dialog = QDialog(self)
            dialog.setWindowTitle("Biometric Authentication")
            dialog.setFixedSize(500, 400)
            dialog.setStyleSheet(f"background: {COLORS['bg_primary']}; color: {COLORS['text_primary']};")
            
            layout = QVBoxLayout(dialog)
            layout.setContentsMargins(30, 30, 30, 30)
            layout.setSpacing(20)
            
            # Title
            title = QLabel("Biometric Authentication")
            title.setStyleSheet(f"color: {COLORS['text_primary']}; font-size: 20px; font-weight: bold;")
            title.setAlignment(Qt.AlignmentFlag.AlignCenter)
            layout.addWidget(title)
            
            # Check enrollment status
            engine = FaceRecognitionEngine()
            is_enrolled = engine.has_enrolled_face(self.current_user)
            
            # Status info
            status_frame = QFrame()
            status_frame.setStyleSheet(f"""
                QFrame {{
                    background: rgba(255, 255, 255, 0.05);
                    border: 1px solid rgba(255, 255, 255, 0.1);
                    border-radius: 8px;
                    padding: 15px;
                }}
            """)
            status_layout = QVBoxLayout(status_frame)
            
            status_icon = QLabel("✓" if is_enrolled else "○")
            status_icon.setStyleSheet(f"font-size: 48px; color: {COLORS['success'] if is_enrolled else COLORS['text_secondary']};")
            status_icon.setAlignment(Qt.AlignmentFlag.AlignCenter)
            status_layout.addWidget(status_icon)
            
            status_text = QLabel("Face Recognition Enabled" if is_enrolled else "Face Recognition Not Configured")
            status_text.setStyleSheet(f"color: {COLORS['text_primary']}; font-size: 16px; font-weight: bold;")
            status_text.setAlignment(Qt.AlignmentFlag.AlignCenter)
            status_layout.addWidget(status_text)
            
            if is_enrolled:
                status_desc = QLabel("Your face is enrolled. You can use face recognition to unlock.")
            else:
                status_desc = QLabel("Enroll your face to enable biometric unlock on login screen.")
            status_desc.setStyleSheet(f"color: {COLORS['text_secondary']}; font-size: 12px;")
            status_desc.setAlignment(Qt.AlignmentFlag.AlignCenter)
            status_desc.setWordWrap(True)
            status_layout.addWidget(status_desc)
            
            layout.addWidget(status_frame)
            
            # Face Recognition option
            face_btn = QPushButton("👤 Face Recognition" if not is_enrolled else "👤 Re-enroll Face")
            face_btn.setFixedHeight(50)
            face_btn.setCursor(Qt.CursorShape.PointingHandCursor)
            face_btn.setStyleSheet(f"""
                QPushButton {{
                    background: {COLORS['accent_primary']};
                    color: white;
                    border: none;
                    border-radius: 8px;
                    font-size: 14px;
                    font-weight: bold;
                }}
                QPushButton:hover {{
                    background: {COLORS['accent_hover']};
                }}
            """)
            face_btn.clicked.connect(lambda: self.enroll_face(dialog))
            layout.addWidget(face_btn)
            
            # Delete enrollment button (if enrolled)
            if is_enrolled:
                delete_btn = QPushButton("🗑️ Remove Face Data")
                delete_btn.setFixedHeight(45)
                delete_btn.setCursor(Qt.CursorShape.PointingHandCursor)
                delete_btn.setStyleSheet(f"""
                    QPushButton {{
                        background: {COLORS['error']};
                        color: white;
                        border: none;
                        border-radius: 8px;
                        font-size: 13px;
                        font-weight: bold;
                    }}
                    QPushButton:hover {{
                        background: #dc2626;
                    }}
                """)
                delete_btn.clicked.connect(lambda: self.delete_face_data(dialog))
                layout.addWidget(delete_btn)
            
            layout.addStretch()
            
            # Close button
            close_btn = QPushButton("Close")
            close_btn.setFixedHeight(40)
            close_btn.setCursor(Qt.CursorShape.PointingHandCursor)
            close_btn.setStyleSheet(f"""
                QPushButton {{
                    background: {COLORS['bg_tertiary']};
                    color: {COLORS['text_primary']};
                    border: 1px solid {COLORS['border']};
                    border-radius: 8px;
                    padding: 0 20px;
                    font-size: 12px;
                }}
                QPushButton:hover {{
                    background: {COLORS['border']};
                }}
            """)
            close_btn.clicked.connect(dialog.close)
            layout.addWidget(close_btn)
            
            dialog.exec()
            
        except Exception as e:
            QMessageBox.critical(self, "Error", f"Failed to open biometric settings: {str(e)}")
    
    def enroll_face(self, parent_dialog):
        """Enroll user's face"""
        try:
            from face_recognition_module import FaceEnrollmentDialog
            
            enrollment_dialog = FaceEnrollmentDialog(self.current_user, parent_dialog)
            enrollment_dialog.enrollment_complete.connect(lambda success: self.on_enrollment_complete(success, parent_dialog))
            enrollment_dialog.exec()
            
        except Exception as e:
            QMessageBox.critical(parent_dialog, "Error", f"Face enrollment failed: {str(e)}")
    
    def on_enrollment_complete(self, success, parent_dialog):
        """Handle enrollment completion"""
        if success:
            parent_dialog.close()
            self.manage_biometric()  # Reopen with updated status
    
    def delete_face_data(self, parent_dialog):
        """Delete enrolled face data"""
        reply = QMessageBox.question(
            parent_dialog,
            "Remove Face Data",
            "Are you sure you want to remove your enrolled face data?\nYou will need to re-enroll to use face unlock.",
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No
        )
        
        if reply == QMessageBox.StandardButton.Yes:
            try:
                from face_recognition_module import FaceRecognitionEngine
                engine = FaceRecognitionEngine()
                
                if engine.delete_enrolled_face(self.current_user):
                    QMessageBox.information(parent_dialog, "Success", "Face data removed successfully!")
                    parent_dialog.close()
                    self.manage_biometric()  # Reopen with updated status
                else:
                    QMessageBox.warning(parent_dialog, "Error", "Failed to remove face data.")
                    
            except Exception as e:
                QMessageBox.critical(parent_dialog, "Error", f"Failed to remove face data: {str(e)}")
class PropertiesTab(QWidget):
    """Properties/About tab showing system information"""
    def __init__(self, config, parent=None):
        super().__init__(parent)
        self.config = config
        self.setup_ui()
    
    def setup_ui(self):
        layout = QVBoxLayout(self)
        layout.setContentsMargins(20, 20, 20, 20)
        layout.setSpacing(20)
        
        # System Info Card
        system_card = GlassFrame(opacity=0.12)
        system_layout = QVBoxLayout(system_card)
        system_layout.setContentsMargins(30, 25, 30, 25)
        system_layout.setSpacing(20)
        
        # OS Logo/Icon
        logo_layout = QHBoxLayout()
        logo_layout.addStretch()
        
        logo_label = QLabel()
        logo_pixmap = QPixmap("/home/yousuf-yasser-elshaer/codes/os/assets/start.png")
        if not logo_pixmap.isNull():
            scaled_logo = logo_pixmap.scaled(80, 80, Qt.AspectRatioMode.KeepAspectRatio, Qt.TransformationMode.SmoothTransformation)
            logo_label.setPixmap(scaled_logo)
        else:
            logo_label.setText("YouOS")
            logo_label.setStyleSheet(f"color: {COLORS['text_primary']}; font-size: 32px; font-weight: bold;")
        logo_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        logo_layout.addWidget(logo_label)
        logo_layout.addStretch()
        system_layout.addLayout(logo_layout)
        
        # OS Name and Version
        os_name = QLabel("YouOS 10")
        os_name.setStyleSheet(f"color: {COLORS['text_primary']}; font-size: 24px; font-weight: bold;")
        os_name.setAlignment(Qt.AlignmentFlag.AlignCenter)
        system_layout.addWidget(os_name)
        
        os_version = QLabel("Build 26m1.7.3")
        os_version.setStyleSheet(f"color: {COLORS['text_secondary']}; font-size: 16px;")
        os_version.setAlignment(Qt.AlignmentFlag.AlignCenter)
        system_layout.addWidget(os_version)
        
        system_layout.addSpacing(20)
        
        # System Information
        info_layout = QVBoxLayout()
        info_layout.setSpacing(10)
        
        # Edition
        edition_layout = QHBoxLayout()
        edition_label = QLabel("Edition:")
        edition_label.setStyleSheet(f"color: {COLORS['text_secondary']}; font-size: 14px;")
        edition_value = QLabel("Professional")
        edition_value.setStyleSheet(f"color: {COLORS['text_primary']}; font-size: 14px; font-weight: bold;")
        edition_layout.addWidget(edition_label)
        edition_layout.addWidget(edition_value)
        edition_layout.addStretch()
        info_layout.addLayout(edition_layout)
        
        # Architecture
        arch_layout = QHBoxLayout()
        arch_label = QLabel("Architecture:")
        arch_label.setStyleSheet(f"color: {COLORS['text_secondary']}; font-size: 14px;")
        arch_value = QLabel("64-bit")
        arch_value.setStyleSheet(f"color: {COLORS['text_primary']}; font-size: 14px; font-weight: bold;")
        arch_layout.addWidget(arch_label)
        arch_layout.addWidget(arch_value)
        arch_layout.addStretch()
        info_layout.addLayout(arch_layout)
        
        # Install Date
        install_layout = QHBoxLayout()
        install_label = QLabel("Installed:")
        install_label.setStyleSheet(f"color: {COLORS['text_secondary']}; font-size: 14px;")
        from PyQt6.QtCore import QDate
        install_value = QLabel(QDate.currentDate().toString("MMMM d, yyyy"))
        install_value.setStyleSheet(f"color: {COLORS['text_primary']}; font-size: 14px; font-weight: bold;")
        install_layout.addWidget(install_label)
        install_layout.addWidget(install_value)
        install_layout.addStretch()
        info_layout.addLayout(install_layout)
        
        system_layout.addLayout(info_layout)
        layout.addWidget(system_card)
        
        # Copyright Card
        copyright_card = GlassFrame(opacity=0.08)
        copyright_layout = QVBoxLayout(copyright_card)
        copyright_layout.setContentsMargins(20, 15, 20, 15)
        copyright_layout.setSpacing(10)
        
        copyright_text = QLabel("© 2024 YouOS. All rights reserved.")
        copyright_text.setStyleSheet(f"color: {COLORS['text_secondary']}; font-size: 12px;")
        copyright_text.setAlignment(Qt.AlignmentFlag.AlignCenter)
        copyright_layout.addWidget(copyright_text)
        
        layout.addWidget(copyright_card)
        layout.addStretch()

class MoreTab(QWidget):
    """More settings tab"""
    def __init__(self, config, parent=None):
        super().__init__(parent)
        self.config = config
        self.current_user = get_current_user()
        self.setup_ui()
    def setup_ui(self):
        layout = QVBoxLayout(self)
        layout.setContentsMargins(20, 20, 20, 20)
        layout.setSpacing(20)
        # Language Card
        language_card = SettingCard("Input Language", "🌍")
        lang_edit = QLineEdit(self.config.get("input_language", "en_US.UTF-8"))
        lang_edit.textChanged.connect(lambda text: self.config.update({"input_language": text}) or save_config(self.config))
        language_card.content_layout.addWidget(lang_edit)
        layout.addWidget(language_card)
        # Background Card with preview
        background_card = GlassFrame(opacity=0.12)
        bg_layout = QVBoxLayout(background_card)
        bg_layout.setContentsMargins(20, 15, 20, 15)
        bg_layout.setSpacing(15)
        # Header
        bg_header = QHBoxLayout()
        bg_icon = QLabel("🖼️")
        bg_icon.setStyleSheet("font-size: 24px;")
        bg_header.addWidget(bg_icon)
        bg_title = QLabel("Wallpaper")
        bg_title.setStyleSheet(f"color: {COLORS['text_primary']}; font-size: 14px; font-weight: bold;")
        bg_header.addWidget(bg_title)
        bg_header.addStretch()
        bg_layout.addLayout(bg_header)
        # Wallpaper grid
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setFixedHeight(250)
        scroll.setStyleSheet("""
            QScrollArea {
                background: transparent;
                border: none;
            }
            QScrollBar:vertical {
                background: rgba(255, 255, 255, 0.1);
                width: 8px;
                border-radius: 4px;
            }
            QScrollBar::handle:vertical {
                background: rgba(59, 130, 246, 0.5);
                border-radius: 4px;
            }
        """)
        wallpapers_widget = QWidget()
        wallpapers_layout = QGridLayout(wallpapers_widget)
        wallpapers_layout.setSpacing(10)
        # Load wallpapers
        if WALLPAPERS_DIR.exists():
            wallpapers = list(WALLPAPERS_DIR.glob("*.jpg")) + \
                list(WALLPAPERS_DIR.glob("*.png")) + \
                list(WALLPAPERS_DIR.glob("*.jpeg"))
            for i, wallpaper_path in enumerate(wallpapers):
                btn = QPushButton()
                btn.setFixedSize(120, 80)
                btn.setCursor(Qt.CursorShape.PointingHandCursor)
                # Load thumbnail
                pixmap = QPixmap(str(wallpaper_path))
                if not pixmap.isNull():
                    pixmap = pixmap.scaled(120, 80,
                        Qt.AspectRatioMode.KeepAspectRatioByExpanding,
                        Qt.TransformationMode.SmoothTransformation)
                btn.setIcon(QIcon(pixmap))
                btn.setIconSize(QSize(120, 80))
                btn.setStyleSheet("""
                    QPushButton {
                        background: rgba(255, 255, 255, 0.1);
                        border: 2px solid rgba(255, 255, 255, 0.2);
                        border-radius: 8px;
                    }
                    QPushButton:hover {
                        border: 2px solid rgba(59, 130, 246, 0.8);
                    }
                """)
                btn.clicked.connect(lambda checked, p=str(wallpaper_path): self.set_wallpaper(p))
                wallpapers_layout.addWidget(btn, i // 3, i % 3)
        else:
            no_wall_label = QLabel("No wallpapers found in assets/wallpapers")
            no_wall_label.setStyleSheet(f"color: {COLORS['text_secondary']};")
            wallpapers_layout.addWidget(no_wall_label)
        scroll.setWidget(wallpapers_widget)
        bg_layout.addWidget(scroll)
        layout.addWidget(background_card)
        # Reset Card
        reset_card = SettingCard("Reset to Default", "🔄")
        reset_btn = QPushButton("Reset")
        reset_btn.setFixedHeight(35)
        reset_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        reset_btn.setStyleSheet(f"""
            QPushButton {{
                background: {COLORS['error']};
                color: white;
                border: none;
                border-radius: 6px;
                padding: 0 20px;
                font-size: 12px;
}}
            QPushButton:hover {{
                background: #dc2626;
}}
        """)
        reset_btn.clicked.connect(self.reset_settings)
        reset_card.content_layout.addWidget(reset_btn)
        layout.addWidget(reset_card)
        layout.addStretch()
    def set_wallpaper(self, wallpaper_path):
        """Set wallpaper for current user"""
        try:
            # Load users
            users = load_users()
            if self.current_user in users:
                # Save to user profile
                users[self.current_user]['wallpaper'] = wallpaper_path
                save_users(users)
                # Apply wallpaper (assuming feh is available)
                subprocess.run(['feh', '--bg-fill', wallpaper_path])
                QMessageBox.information(self, "Success",
                    f"Wallpaper updated and applied!\n\n{Path(wallpaper_path).name}")
            else:
                QMessageBox.warning(self, "Error",
                    f"User '{self.current_user}' not found in system.")
        except Exception as e:
            QMessageBox.critical(self, "Error", f"Failed to set wallpaper: {str(e)}")
    def change_background(self):
        """Old method - kept for compatibility"""
        path, _ = QFileDialog.getOpenFileName(self, "Choose Background Image",
            str(WALLPAPERS_DIR),
            "Images (*.jpg *.png *.jpeg)")
        if path:
            self.set_wallpaper(path)
    def reset_settings(self):
        reply = QMessageBox.question(self, "Reset Settings",
            "Are you sure you want to reset all settings to default?",
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No)
        if reply == QMessageBox.StandardButton.Yes:
            self.config.clear()
            self.config.update({
                "wifi_networks": [],
                "wifi_enabled": False,
                "bluetooth_enabled": False,
                "volume": 50,
                "background": "",
                "brightness": 50,
                "input_language": "en_US.UTF-8",
                "current_user": self.current_user
            })
            save_config(self.config)
            QMessageBox.information(self, "Reset Complete",
                "Settings have been reset to default.")
class SettingsWindow(QMainWindow):
    """Main settings window"""
    def __init__(self):
        super().__init__()
        self.config = load_config()
        self.setup_ui()
    def setup_ui(self):
        self.setWindowTitle("YouOS Settings")
        self.setMinimumSize(800, 600)
        # Apply dark theme
        self.setStyleSheet(f"""
            QMainWindow {{
                background: {COLORS['bg_primary']};
}}
            QTabWidget::pane {{
                border: none;
                background: transparent;
}}
            QTabBar::tab {{
                background: {COLORS['bg_secondary']};
                color: {COLORS['text_secondary']};
                padding: 12px 24px;
                border: none;
                border-top-left-radius: 8px;
                border-top-right-radius: 8px;
                margin-right: 2px;
}}
            QTabBar::tab:selected {{
                background: {COLORS['bg_tertiary']};
                color: {COLORS['text_primary']};
}}
            QTabBar::tab:hover {{
                background: {COLORS['bg_tertiary']};
}}
        """)
        # Central widget
        central = QWidget()
        self.setCentralWidget(central)
        layout = QVBoxLayout(central)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(0)
        # Header
        header = QWidget()
        header.setStyleSheet(f"background: {COLORS['bg_secondary']}; padding: 20px;")
        header_layout = QHBoxLayout(header)
        title = QLabel("⚙️ Settings")
        title.setStyleSheet(f"color: {COLORS['text_primary']}; font-size: 24px; font-weight: bold;")
        header_layout.addWidget(title)
        header_layout.addStretch()
        layout.addWidget(header)
        # Tab widget
        tabs = QTabWidget()
        tabs.setStyleSheet(f"background: {COLORS['bg_primary']};")
        # Add tabs
        tabs.addTab(NetworkTab(self.config), "🌐 Network")
        tabs.addTab(DeviceTab(self.config), "💻 Device")
        tabs.addTab(SecurityTab(self.config), "🔒 Security")
        tabs.addTab(PropertiesTab(self.config), "ℹ️ Properties")
        tabs.addTab(MoreTab(self.config), "⋯ More")
        layout.addWidget(tabs)
def main():
    app = QApplication(sys.argv)
    # Set global font
    font = QFont("Segoe UI", 10)
    app.setFont(font)
    window = SettingsWindow()
    window.show()
    sys.exit(app.exec())
if __name__ == "__main__":
    main()