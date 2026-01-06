"""
YouOS 10 PyQt6 - Desktop Module with Window Management v1.1.0
desktop.py - Desktop manager with integrated window system and battery management
"""

import os
import subprocess
import sys
import json
import requests
from pathlib import Path
from PyQt6.QtWidgets import (QWidget, QVBoxLayout, QHBoxLayout, QLabel, 
                              QPushButton, QGridLayout, QScrollArea, QLineEdit,
                              QGraphicsDropShadowEffect, QMenu, QSlider, QFrame,
                              QFileDialog, QApplication, QMainWindow, QTextEdit,
                              QComboBox, QTabWidget, QCheckBox, QListWidget,
                              QListWidgetItem)
from PyQt6.QtCore import Qt, QTimer, QTime, QDate, pyqtSignal, QPoint, QRect, QThread, pyqtSlot
from PyQt6.QtGui import (QColor, QPainter, QPainterPath, QPen, QLinearGradient, 
                        QCursor, QAction, QPixmap, QPalette, QBrush, QIcon)

from widgets import (GlassFrame, AnalogClock, SystemMonitorWidget,
                    CalendarWidget, WeatherWidget)
from start import StartMenu

try:
    from utils import play_sound, get_volume, set_volume, get_battery_info, BASE_DIR, set_brightness, get_brightness
except ImportError:
    BASE_DIR = Path(__file__).parent
    def play_sound(name):
        pass
    def get_volume():
        return 50
    def set_volume(val):
        pass
    def get_battery_info():
        return {'percent': 85, 'plugged': False}
    def set_brightness(val):
        pass
    def get_brightness():
        return 75

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

WALLPAPERS_DIR = BASE_DIR / 'assets' / 'wallpapers'
PROGRAMS_DIR = Path("/home/yousuf-yasser-elshaer/codes/os/programs")
START_ICON_PATH = Path("/home/yousuf-yasser-elshaer/codes/os/assets/start.png")


class BatteryWarningDialog(GlassFrame):
    """Battery warning dialog"""
    
    def __init__(self, title, message, icon, color, parent=None):
        super().__init__(parent, opacity=0.25)
        self.setFixedSize(400, 200)
        self.setup_ui(title, message, icon, color)
        
        # Auto-hide after 10 seconds
        QTimer.singleShot(10000, self.close)
    
    def setup_ui(self, title, message, icon, color):
        layout = QVBoxLayout(self)
        layout.setContentsMargins(30, 30, 30, 30)
        layout.setSpacing(20)
        
        # Icon
        icon_label = QLabel(icon)
        icon_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        icon_label.setStyleSheet("font-size: 48px;")
        layout.addWidget(icon_label)
        
        # Title
        title_label = QLabel(title)
        title_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        title_label.setStyleSheet(f"color: {color}; font-size: 18px; font-weight: bold;")
        layout.addWidget(title_label)
        
        # Message
        message_label = QLabel(message)
        message_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        message_label.setStyleSheet(f"color: {COLORS['text_secondary']}; font-size: 13px;")
        message_label.setWordWrap(True)
        layout.addWidget(message_label)
        
        # OK button
        ok_btn = QPushButton("OK")
        ok_btn.setFixedHeight(35)
        ok_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        ok_btn.setStyleSheet(f"""
            QPushButton {{
                background: {color};
                color: white;
                border: none;
                border-radius: 6px;
                font-size: 12px;
                font-weight: bold;
            }}
            QPushButton:hover {{
                opacity: 0.8;
            }}
        """)
        ok_btn.clicked.connect(self.close)
        layout.addWidget(ok_btn)


class DesktopManager(QWidget):
    """Desktop manager - main desktop functionality with window management and battery monitoring"""
    
    logout_requested = pyqtSignal()
    restart_requested = pyqtSignal()
    shutdown_requested = pyqtSignal()
    
    def __init__(self, auth_manager, username, parent=None):
        super().__init__(parent)
        self.auth = auth_manager
        self.username = username
        
        # Battery monitoring
        self.battery_dialog = None
        self.battery_warned_25 = False
        self.battery_warned_15 = False
        self.battery_timer = QTimer()
        self.battery_timer.timeout.connect(self.check_battery_status)
        self.battery_timer.start(10000)  # Check every 10 seconds
        
        # Initialize update manager
        self.update_manager = None
        QTimer.singleShot(5000, self.initialize_updates)
        
        print(f"✅ Desktop v1.1.0 initialized for user: {username} with battery monitoring")
    
    def initialize_updates(self):
        """Initialize system update manager"""
        try:
            from sysupdate import UpdateManager
            self.update_manager = UpdateManager(self)
            print("✅ System update manager initialized")
        except Exception as e:
            print(f"❌ Failed to initialize update manager: {e}")
    
    def check_battery_status(self):
        """Check battery status and show warnings/shutdown"""
        try:
            battery_info = get_battery_info()
            percent = int(battery_info.get('percent', 100))
            plugged = battery_info.get('plugged', True)
            
            # Skip if plugged in
            if plugged:
                self.battery_warned_25 = False
                self.battery_warned_15 = False
                return
            
            # Auto shutdown at 10%
            if percent <= 10:
                self.show_battery_critical_shutdown()
                return
            
            # Critical warning at 15%
            if percent <= 15 and not self.battery_warned_15:
                self.show_battery_critical_dialog(percent)
                self.battery_warned_15 = True
                return
            
            # Low warning at 25%
            if percent <= 25 and not self.battery_warned_25:
                self.show_battery_low_dialog(percent)
                self.battery_warned_25 = True
                return
            
            # Reset warnings if battery goes above thresholds
            if percent > 25:
                self.battery_warned_25 = False
            if percent > 15:
                self.battery_warned_15 = False
                
        except Exception as e:
            print(f"Error checking battery: {e}")
    
    def show_battery_low_dialog(self, percent):
        """Show low battery dialog at 25%"""
        try:
            play_sound('batterylow.wav')
        except:
            pass
        
        if self.battery_dialog:
            self.battery_dialog.deleteLater()
        
        self.battery_dialog = BatteryWarningDialog(
            "Low Battery",
            f"Battery is low ({percent}%). Please connect charger.",
            "🪫",
            COLORS['warning'],
            self
        )
        self.battery_dialog.move(
            (self.width() - 400) // 2,
            (self.height() - 200) // 2
        )
        self.battery_dialog.show()
    
    def show_battery_critical_dialog(self, percent):
        """Show critical battery dialog at 15%"""
        try:
            play_sound('batterycritical.wav')
        except:
            pass
        
        if self.battery_dialog:
            self.battery_dialog.deleteLater()
        
        self.battery_dialog = BatteryWarningDialog(
            "Critical Battery",
            f"Battery critically low ({percent}%)! System will shutdown at 10%.",
            "🔋",
            COLORS['error'],
            self
        )
        self.battery_dialog.move(
            (self.width() - 400) // 2,
            (self.height() - 200) // 2
        )
        self.battery_dialog.show()
    
    def show_battery_critical_shutdown(self):
        """Auto shutdown at 10% battery"""
        try:
            play_sound('batterycritical.wav')
        except:
            pass
        
        # Show shutdown notification
        if hasattr(self, 'add_notification'):
            self.add_notification(
                "Auto Shutdown",
                "Battery critically low (10%). System shutting down...",
                "⚠️",
                "Now"
            )
        
        # Shutdown after 3 seconds
        QTimer.singleShot(3000, self.shutdown_requested.emit)
    
    def closeEvent(self, event):
        """Clean up resources on close"""
        if hasattr(self, 'update_manager') and self.update_manager:
            self.update_manager.stop()
        if hasattr(self, 'battery_timer'):
            self.battery_timer.stop()
        super().closeEvent(event)


if __name__ == "__main__":
    app = QApplication(sys.argv)
    class MockAuth:
        def __init__(self):
            self.users = {"User": {}}
        def save_users(self):
            pass
    window = DesktopManager(MockAuth(), "User")
    window.showMaximized()
    sys.exit(app.exec())
