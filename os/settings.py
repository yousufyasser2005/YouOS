"""
YouOS 10 - Settings Application
settings.py - Complete settings app in PyQt6 with glassmorphic design
"""
import sys
import json
import subprocess
import hashlib
from pathlib import Path
from PyQt6.QtWidgets import (QApplication, QMainWindow, QWidget, QVBoxLayout,
QHBoxLayout, QLabel, QPushButton, QTabWidget,
QSlider, QListWidget, QLineEdit, QMessageBox, QDialog, QCheckBox,
QGraphicsDropShadowEffect, QFrame, QScrollArea,
QGridLayout, QFileDialog, QInputDialog)
from PyQt6.QtCore import Qt, QTimer, pyqtSignal, QSize
from PyQt6.QtGui import (QFont, QColor, QPainter, QPainterPath, QPen,
QLinearGradient, QPixmap, QIcon)
# Configuration
BASE_DIR = Path(__file__).parent
CONFIG_PATH = BASE_DIR / 'config.json'
USERS_FILE = BASE_DIR / '.youos' / 'users.json'
USERS_FILE.parent.mkdir(exist_ok=True)
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
    """Get current logged in user from environment or config"""
    # Try to get from config first
    config = load_config()
    if 'current_user' in config:
        return config['current_user']
    # Fallback to system user
    import getpass
    return getpass.getuser()
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
        # Password Card
        password_card = SettingCard("Change Password", "🔒")
        password_btn = QPushButton("Change")
        password_btn.setFixedHeight(35)
        password_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        password_btn.setStyleSheet(f"""
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
        password_btn.clicked.connect(self.change_password)
        password_card.content_layout.addWidget(password_btn)
        layout.addWidget(password_card)
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
    def change_password(self):
        users = load_users()
        if self.current_user not in users:
            QMessageBox.warning(self, "Error", f"User '{self.current_user}' not found in system.")
            return
        current_pw, ok = QInputDialog.getText(self, "Change Password",
            "Enter current password:",
            QLineEdit.EchoMode.Password)
        if not ok or not current_pw:
            return
        if users[self.current_user].get('password') != hash_password(current_pw):
            QMessageBox.warning(self, "Error", "Current password is incorrect!")
            return
        new_pw, ok = QInputDialog.getText(self, "Change Password",
            "Enter new password:",
            QLineEdit.EchoMode.Password)
        if not ok or not new_pw:
            return
        if len(new_pw) < 4:
            QMessageBox.warning(self, "Error", "Password must be at least 4 characters!")
            return
        confirm_pw, ok = QInputDialog.getText(self, "Change Password",
            "Confirm new password:",
            QLineEdit.EchoMode.Password)
        if not ok or not confirm_pw:
            return
        if new_pw != confirm_pw:
            QMessageBox.warning(self, "Error", "Passwords do not match!")
            return
        users[self.current_user]['password'] = hash_password(new_pw)
        save_users(users)
        QMessageBox.information(self, "Success",
            f"Password changed successfully for user '{self.current_user}'!")
    def manage_users(self):
        dialog = QDialog(self)
        dialog.setWindowTitle("User Management")
        dialog.setStyleSheet(f"background: {COLORS['bg_primary']}; color: {COLORS['text_primary']};")
        layout = QVBoxLayout(dialog)
        users = load_users()
        list_widget = QListWidget()
        for user in users:
            list_widget.addItem(user)
        layout.addWidget(list_widget)
        add_btn = QPushButton("Add User")
        add_btn.clicked.connect(lambda: self.add_user(list_widget))
        layout.addWidget(add_btn)
        del_btn = QPushButton("Delete Selected")
        del_btn.clicked.connect(lambda: self.delete_user(list_widget.currentItem().text(), list_widget) if list_widget.currentItem() else None)
        layout.addWidget(del_btn)
        dialog.exec()
    def add_user(self, list_widget):
        username, ok = QInputDialog.getText(self, "Add User", "Enter username:")
        if ok and username:
            users = load_users()
            if username in users:
                QMessageBox.warning(self, "Error", "User exists")
                return
            pw, ok = QInputDialog.getText(self, "Add User", "Enter password:", QLineEdit.EchoMode.Password)
            if ok and pw:
                users[username] = {"password": hash_password(pw), "wallpaper": ""}
                save_users(users)
                list_widget.addItem(username)
    def delete_user(self, username, list_widget):
        if username == self.current_user:
            QMessageBox.warning(self, "Error", "Cannot delete current user")
            return
        reply = QMessageBox.question(self, "Delete User", f"Delete {username}?", QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No)
        if reply == QMessageBox.StandardButton.Yes:
            users = load_users()
            del users[username]
            save_users(users)
            item = list_widget.findItems(username, Qt.MatchFlag.MatchExactly)[0]
            list_widget.takeItem(list_widget.row(item))
    def view_logs(self):
        try:
            result = subprocess.run(['tail', '-20', '/var/log/auth.log'], capture_output=True, text=True)
            QMessageBox.information(self, "Security Logs", result.stdout if result.stdout else "No logs available")
        except:
            QMessageBox.warning(self, "Error", "Unable to read security logs")
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