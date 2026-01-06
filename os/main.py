"""
YouOS 10 - Complete PyQt6 Implementation
main.py - Main Application with Boot, Login, Desktop, Shutdown and all features
"""

import sys
import json
import random
import os
import time
from pathlib import Path
from PyQt6.QtWidgets import (QApplication, QMainWindow, QWidget, QVBoxLayout, 
                              QHBoxLayout, QLabel, QPushButton, QLineEdit,
                              QGridLayout, QFrame, QGraphicsDropShadowEffect,
                              QScrollArea, QStackedWidget, QComboBox, QProgressBar,
                              QGraphicsBlurEffect)
from PyQt6.QtCore import (Qt, QTimer, QTime, QDate, QPropertyAnimation, 
                           QEasingCurve, pyqtSignal, QPoint, QRect, QSize,
                           QParallelAnimationGroup, QSequentialAnimationGroup)
from PyQt6.QtGui import (QFont, QPalette, QColor, QLinearGradient, QPainter,
                         QPen, QBrush, QPixmap, QIcon, QPainterPath)
import math
import hashlib

# Configuration
APP_DIR = Path.home() / '.youos'
APP_DIR.mkdir(exist_ok=True)
USERS_FILE = APP_DIR / 'users.json'
WALLPAPERS_DIR = Path('assets/wallpapers')

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


class AuthManager:
    """User authentication manager"""
    
    def __init__(self):
        self.users_file = USERS_FILE
        self.load_users()
    
    def load_users(self):
        """Load users from file"""
        if self.users_file.exists():
            with open(self.users_file, 'r') as f:
                self.users = json.load(f)
            
            # Ensure all users have required fields
            for username, user_data in self.users.items():
                if 'pinned_apps' not in user_data:
                    user_data['pinned_apps'] = []
                if 'desktop_icons' not in user_data:
                    user_data['desktop_icons'] = {}
        else:
            # Create default admin user
            self.users = {
                'admin': {
                    'password': self.hash_password('admin'),
                    'theme': 'dark',
                    'wallpaper': 'default.jpg',
                    'pinned_apps': [],
                    'desktop_icons': {},
                    'icon_positions': {},
                    'window_positions': {}
                }
            }
            self.save_users()
    
    def save_users(self):
        """Save users to file"""
        with open(self.users_file, 'w') as f:
            json.dump(self.users, f, indent=4)
    
    def hash_password(self, password):
        """Hash password"""
        return hashlib.sha256(password.encode()).hexdigest()
    
    def authenticate(self, username, password):
        """Authenticate user"""
        if username in self.users:
            return self.users[username]['password'] == self.hash_password(password)
        return False
    
    def create_user(self, username, password):
        """Create new user"""
        if username in self.users:
            return False, "Username already exists"
        
        if len(username) < 3:
            return False, "Username must be at least 3 characters"
        
        if len(password) < 4:
            return False, "Password must be at least 4 characters"
        
        self.users[username] = {
            'password': self.hash_password(password),
            'theme': 'dark',
            'wallpaper': 'default.jpg',
            'pinned_apps': [],
            'desktop_icons': {},
            'icon_positions': {},
            'window_positions': {}
        }
        self.save_users()
        return True, "User created successfully"
    
    def get_all_usernames(self):
        """Get all usernames"""
        return list(self.users.keys())
    
    def get_user_data(self, username):
        """Get user data"""
        return self.users.get(username, {})
    
    def update_user_data(self, username, key, value):
        """Update user data"""
        if username in self.users:
            self.users[username][key] = value
            self.save_users()


class GlassWidget(QWidget):
    """Base widget with glassmorphism effect"""
    
    def __init__(self, parent=None, opacity=0.1):
        super().__init__(parent)
        self.opacity = opacity
        self.setup_glass_effect()
    
    def setup_glass_effect(self):
        """Apply glassmorphism styling"""
        self.setAttribute(Qt.WidgetAttribute.WA_TranslucentBackground)
        
        # Drop shadow for depth
        shadow = QGraphicsDropShadowEffect()
        shadow.setBlurRadius(30)
        shadow.setColor(QColor(0, 0, 0, 100))
        shadow.setOffset(0, 5)
        self.setGraphicsEffect(shadow)
    
    def paintEvent(self, event):
        """Custom paint for glass effect"""
        from PyQt6.QtCore import QRectF
        
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        
        # Create rounded rectangle path
        path = QPainterPath()
        path.addRoundedRect(QRectF(self.rect()), 16, 16)
        
        # Enhanced background with more opacity for better visibility
        gradient = QLinearGradient(0, 0, 0, self.height())
        gradient.setColorAt(0, QColor(30, 30, 60, 220))  # Darker, more opaque
        gradient.setColorAt(1, QColor(20, 20, 50, 200))
        
        painter.fillPath(path, gradient)
        
        # Border
        painter.setPen(QPen(QColor(255, 255, 255, 80), 1))
        painter.drawPath(path)


class BootScreen(QWidget):
    """Boot screen with animated logo and progress"""
    
    boot_complete = pyqtSignal()
    
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setup_ui()
    
    def setup_ui(self):
        """Setup boot screen UI"""
        layout = QVBoxLayout(self)
        layout.setAlignment(Qt.AlignmentFlag.AlignCenter)
        
        # Logo image
        self.logo_label = QLabel()
        pixmap = QPixmap("/home/yousuf-yasser-elshaer/codes/os/assets/start.png")
        if not pixmap.isNull():
            scaled_pixmap = pixmap.scaled(200, 200, Qt.AspectRatioMode.KeepAspectRatio, Qt.TransformationMode.SmoothTransformation)
            self.logo_label.setPixmap(scaled_pixmap)
        else:
            self.logo_label.setText("YouOS")
            self.logo_label.setStyleSheet(f"color: {COLORS['text_primary']}; font-size: 64px; font-weight: bold;")
        self.logo_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        layout.addWidget(self.logo_label)
        
        layout.addSpacing(30)
        
        # Progress bar
        self.progress = QProgressBar()
        self.progress.setFixedWidth(300)
        self.progress.setFixedHeight(6)
        self.progress.setTextVisible(False)
        self.progress.setStyleSheet(f"""
            QProgressBar {{
                background: {COLORS['bg_tertiary']};
                border-radius: 3px;
                border: none;
            }}
            QProgressBar::chunk {{
                background: {COLORS['accent_primary']};
                border-radius: 3px;
            }}
        """)
        layout.addWidget(self.progress, alignment=Qt.AlignmentFlag.AlignCenter)
        
        layout.addSpacing(15)
        
        # Status label
        self.status_label = QLabel("Initializing...")
        self.status_label.setStyleSheet(f"""
            color: {COLORS['text_secondary']};
            font-size: 12px;
        """)
        self.status_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        layout.addWidget(self.status_label)
    
    def start_boot(self):
        """Start boot animation"""
        # Play startup sound
        try:
            from utils import play_sound
            play_sound('startup.wav')
        except:
            pass
        
        self.progress_value = 0
        self.boot_timer = QTimer()
        self.boot_timer.timeout.connect(self.update_boot)
        self.boot_timer.start(30)
    
    def update_boot(self):
        """Update boot progress"""
        self.progress_value += 1
        self.progress.setValue(self.progress_value)
        
        # Update status text
        if self.progress_value < 30:
            self.status_label.setText("Loading Kernel...")
        elif self.progress_value < 60:
            self.status_label.setText("Starting Services...")
        elif self.progress_value < 90:
            self.status_label.setText("Loading User Interface...")
        else:
            self.status_label.setText("Ready.")
        
        if self.progress_value >= 100:
            self.boot_timer.stop()
            QTimer.singleShot(500, self.boot_complete.emit)


class LoadingCircle(QWidget):
    """Windows 11-style loading circle"""
    
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setFixedSize(60, 60)
        self.angle = 0
        
        # Animation timer
        self.timer = QTimer()
        self.timer.timeout.connect(self.rotate)
        self.timer.start(50)  # 50ms for smooth animation
    
    def paintEvent(self, event):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        
        # Center point
        center = self.rect().center()
        radius = 20
        
        # Draw dots in circle
        for i in range(8):
            angle = (self.angle + i * 45) % 360
            opacity = max(0.2, 1.0 - (i * 0.1))
            
            # Calculate position
            x = center.x() + radius * 0.7 * math.cos(math.radians(angle))
            y = center.y() + radius * 0.7 * math.sin(math.radians(angle))
            
            # Draw dot
            painter.setBrush(QBrush(QColor(59, 130, 246, int(255 * opacity))))
            painter.setPen(Qt.PenStyle.NoPen)
            painter.drawEllipse(int(x-3), int(y-3), 6, 6)
    
    def rotate(self):
        self.angle = (self.angle + 15) % 360
        self.update()


class ShutdownScreen(QWidget):
    """Shutdown screen with animation"""
    
    shutdown_complete = pyqtSignal()
    
    def __init__(self, action="shutdown", parent=None):
        super().__init__(parent)
        self.action = action
        self.setup_ui()
    
    def setup_ui(self):
        """Setup shutdown screen UI"""
        layout = QVBoxLayout(self)
        layout.setAlignment(Qt.AlignmentFlag.AlignCenter)
        
        # Logo image
        logo_label = QLabel()
        pixmap = QPixmap("/home/yousuf-yasser-elshaer/codes/os/assets/start.png")
        if not pixmap.isNull():
            scaled_pixmap = pixmap.scaled(150, 150, Qt.AspectRatioMode.KeepAspectRatio, Qt.TransformationMode.SmoothTransformation)
            logo_label.setPixmap(scaled_pixmap)
        else:
            # Fallback to icon if image not found
            icons = {'shutdown': '⏻', 'restart': '🔄', 'logout': '🔓'}
            logo_label.setText(icons.get(self.action, '⏻'))
            logo_label.setStyleSheet(f"color: {COLORS['text_primary']}; font-size: 72px;")
        logo_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        layout.addWidget(logo_label)
        
        layout.addSpacing(20)
        
        # Message
        messages = {'shutdown': 'Shutting down...', 'restart': 'Restarting...', 'logout': 'Logging out...'}
        self.message_label = QLabel(messages.get(self.action, 'Shutting down...'))
        self.message_label.setStyleSheet(f"color: {COLORS['text_primary']}; font-size: 24px; font-weight: 500;")
        self.message_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        layout.addWidget(self.message_label)
        
        layout.addSpacing(15)
        
        # Loading circle
        circle_container = QHBoxLayout()
        circle_container.addStretch()
        self.loading_circle = LoadingCircle()
        circle_container.addWidget(self.loading_circle)
        circle_container.addStretch()
        layout.addLayout(circle_container)
        
        # Status label
        self.status_label = QLabel("Please wait...")
        self.status_label.setStyleSheet(f"color: {COLORS['text_secondary']}; font-size: 14px;")
        self.status_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        layout.addWidget(self.status_label)
    
    def start_shutdown(self):
        """Start shutdown animation and sound"""
        # Play appropriate sound based on action
        try:
            from utils import sound_manager
            
            if self.action == 'logout':
                sound_manager.play('logoff.wav')
                QTimer.singleShot(2000, self.shutdown_complete.emit)
            else:
                sound_manager.play('shutdownsound.wav')
                QTimer.singleShot(4500, self.shutdown_complete.emit)
        except Exception as e:
            print(f"Error playing sound: {e}")
            QTimer.singleShot(2000, self.shutdown_complete.emit)


class ConfirmDialog(GlassWidget):
    """Glassmorphic confirmation dialog"""
    
    confirmed = pyqtSignal()
    cancelled = pyqtSignal()
    
    def __init__(self, title, message, icon="⚠️", parent=None):
        super().__init__(parent, opacity=0.15)
        self.title = title
        self.message = message
        self.icon = icon
        self.setFixedSize(400, 250)
        self.setup_ui()
    
    def setup_ui(self):
        """Setup dialog UI"""
        layout = QVBoxLayout(self)
        layout.setContentsMargins(30, 30, 30, 30)
        layout.setSpacing(20)
        
        # Icon
        icon_label = QLabel(self.icon)
        icon_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        icon_label.setStyleSheet("font-size: 48px;")
        layout.addWidget(icon_label)
        
        # Title
        title_label = QLabel(self.title)
        title_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        title_label.setStyleSheet(f"""
            color: {COLORS['text_primary']};
            font-size: 18px;
            font-weight: bold;
        """)
        title_label.setWordWrap(True)
        layout.addWidget(title_label)
        
        # Message
        message_label = QLabel(self.message)
        message_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        message_label.setStyleSheet(f"""
            color: {COLORS['text_secondary']};
            font-size: 13px;
        """)
        message_label.setWordWrap(True)
        layout.addWidget(message_label)
        
        layout.addSpacing(10)
        
        # Buttons
        button_layout = QHBoxLayout()
        button_layout.setSpacing(10)
        
        cancel_btn = QPushButton("Cancel")
        cancel_btn.setFixedHeight(40)
        cancel_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        cancel_btn.setStyleSheet(f"""
            QPushButton {{
                background: rgba(255, 255, 255, 0.1);
                color: white;
                border: 1px solid rgba(255, 255, 255, 0.2);
                border-radius: 8px;
                font-size: 14px;
                font-weight: 500;
            }}
            QPushButton:hover {{
                background: rgba(255, 255, 255, 0.2);
            }}
        """)
        cancel_btn.clicked.connect(self.cancelled.emit)
        button_layout.addWidget(cancel_btn)
        
        confirm_btn = QPushButton("Yes")
        confirm_btn.setFixedHeight(40)
        confirm_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        confirm_btn.setStyleSheet(f"""
            QPushButton {{
                background: {COLORS['error']};
                color: white;
                border: none;
                border-radius: 8px;
                font-size: 14px;
                font-weight: bold;
            }}
            QPushButton:hover {{
                background: #dc2626;
            }}
        """)
        confirm_btn.clicked.connect(self.confirmed.emit)
        button_layout.addWidget(confirm_btn)
        
        layout.addLayout(button_layout)


class LoginScreen(QWidget):
    """Two-phase login screen with proper background painting"""
    
    login_success = pyqtSignal(str)
    shutdown_requested = pyqtSignal()
    restart_requested = pyqtSignal()
    
    def __init__(self, auth_manager, parent=None):
        super().__init__(parent)
        self.auth = auth_manager
        self.login_form_visible = False
        self.background_pixmap = None
        self.setup_ui()
        self.start_clock()
    
    def paintEvent(self, event):
        """Custom paint event for background"""
        painter = QPainter(self)
        
        if self.background_pixmap and not self.background_pixmap.isNull():
            # Scale and draw background image
            scaled_pixmap = self.background_pixmap.scaled(
                self.size(), 
                Qt.AspectRatioMode.KeepAspectRatioByExpanding, 
                Qt.TransformationMode.SmoothTransformation
            )
            
            # Center the image
            x = (self.width() - scaled_pixmap.width()) // 2
            y = (self.height() - scaled_pixmap.height()) // 2
            painter.drawPixmap(x, y, scaled_pixmap)
        else:
            # Fallback gradient
            gradient = QLinearGradient(0, 0, self.width(), self.height())
            gradient.setColorAt(0, QColor('#0f0f1e'))
            gradient.setColorAt(0.5, QColor('#1a1a3e'))
            gradient.setColorAt(1, QColor('#0f0f1e'))
            painter.fillRect(self.rect(), gradient)
    
    def setup_ui(self):
        """Setup login screen UI"""
        # Set random wallpaper background
        self.set_random_wallpaper()
        
        # Main layout
        main_layout = QVBoxLayout(self)
        main_layout.setContentsMargins(0, 0, 0, 0)
        main_layout.setSpacing(0)
        
        # Top spacer
        main_layout.addStretch(2)
        
        # Center area (empty for clean look)
        center_container = QWidget()
        center_container.setStyleSheet("background: transparent;")
        center_layout = QVBoxLayout(center_container)
        center_layout.setAlignment(Qt.AlignmentFlag.AlignCenter)
        
        main_layout.addWidget(center_container)
        main_layout.addStretch(2)
        
        # Bottom bar with power buttons and clock
        bottom_bar = QWidget()
        bottom_bar.setStyleSheet("background: transparent;")
        bottom_bar.setFixedHeight(150)
        bottom_layout = QHBoxLayout(bottom_bar)
        bottom_layout.setContentsMargins(30, 20, 30, 30)
        
        # Power buttons (left side)
        power_container = QWidget()
        power_container.setFixedWidth(70)
        power_layout = QVBoxLayout(power_container)
        power_layout.setSpacing(10)
        power_layout.setContentsMargins(0, 0, 0, 0)
        
        self.shutdown_btn = QPushButton("⏻")
        self.shutdown_btn.setFixedSize(50, 50)
        self.shutdown_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        self.shutdown_btn.setStyleSheet(f"""
            QPushButton {{ 
                background: rgba(0,0,0,0.6); 
                color: white; 
                border: 2px solid rgba(255,255,255,0.3);
                border-radius: 25px; 
                font-size: 20px;
            }} 
            QPushButton:hover {{ 
                background: {COLORS['error']}; 
                border-color: {COLORS['error']};
            }}
        """)
        self.shutdown_btn.clicked.connect(self.handle_shutdown)
        
        self.restart_btn = QPushButton("🔄")
        self.restart_btn.setFixedSize(50, 50)
        self.restart_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        self.restart_btn.setStyleSheet(f"""
            QPushButton {{ 
                background: rgba(0,0,0,0.6); 
                color: white; 
                border: 2px solid rgba(255,255,255,0.3);
                border-radius: 25px; 
                font-size: 18px;
            }} 
            QPushButton:hover {{ 
                background: {COLORS['warning']}; 
                border-color: {COLORS['warning']};
            }}
        """)
        self.restart_btn.clicked.connect(self.handle_restart)
        
        power_layout.addWidget(self.shutdown_btn)
        power_layout.addWidget(self.restart_btn)
        power_layout.addStretch()
        
        bottom_layout.addWidget(power_container)
        bottom_layout.addStretch()
        
        # Clock (right side)
        self.clock_widget = QWidget()
        clock_layout = QVBoxLayout(self.clock_widget)
        clock_layout.setContentsMargins(0, 0, 0, 0)
        clock_layout.setSpacing(5)
        
        self.time_label = QLabel()
        self.time_label.setStyleSheet(f"""
            color: white; 
            font-size: 64px; 
            font-weight: bold;
        """)
        self.time_label.setAlignment(Qt.AlignmentFlag.AlignRight)
        clock_layout.addWidget(self.time_label)
        
        self.date_label = QLabel()
        self.date_label.setStyleSheet(f"""
            color: white; 
            font-size: 24px;
        """)
        self.date_label.setAlignment(Qt.AlignmentFlag.AlignRight)
        clock_layout.addWidget(self.date_label)
        
        bottom_layout.addWidget(self.clock_widget)
        
        main_layout.addWidget(bottom_bar)
        
        # Login form (initially hidden, positioned absolutely)
        self.setup_login_form()
        
        # Enable mouse tracking for hover effect
        self.setMouseTracking(True)
    
    def set_random_wallpaper(self):
        """Set random wallpaper from assets/wallpapers/"""
        wallpaper_dir = Path('/home/yousuf-yasser-elshaer/codes/os/assets/wallpapers')
        if wallpaper_dir.exists():
            wallpapers = list(wallpaper_dir.glob('*.jpg')) + list(wallpaper_dir.glob('*.png'))
            if wallpapers:
                wallpaper = random.choice(wallpapers)
                self.background_pixmap = QPixmap(str(wallpaper))
                self.update()  # Trigger repaint
                return
        # No wallpaper found, will use gradient in paintEvent
        self.background_pixmap = None
        self.update()
    
    def setup_login_form(self):
        """Setup login form"""
        self.login_card = GlassWidget(self, opacity=0.15)
        self.login_card.setFixedSize(420, 520)
        self.login_card.hide()
        
        card_layout = QVBoxLayout(self.login_card)
        card_layout.setContentsMargins(40, 50, 40, 40)
        card_layout.setSpacing(20)
        
        # Logo
        logo = QLabel("YouOS")
        logo.setStyleSheet(f"""
            color: {COLORS['text_primary']}; 
            font-size: 36px; 
            font-weight: bold;
        """)
        logo.setAlignment(Qt.AlignmentFlag.AlignCenter)
        card_layout.addWidget(logo)
        
        # Welcome text
        welcome = QLabel("Welcome back")
        welcome.setStyleSheet(f"""
            color: {COLORS['text_secondary']}; 
            font-size: 15px;
        """)
        welcome.setAlignment(Qt.AlignmentFlag.AlignCenter)
        card_layout.addWidget(welcome)
        
        card_layout.addSpacing(20)
        
        # User dropdown
        usernames = self.auth.get_all_usernames()
        self.username_combo = QComboBox()
        self.username_combo.addItems(usernames)
        self.username_combo.setFixedHeight(45)
        self.username_combo.setCursor(Qt.CursorShape.PointingHandCursor)
        self.username_combo.setStyleSheet(f"""
            QComboBox {{ 
                background: {COLORS['bg_tertiary']}; 
                color: {COLORS['text_primary']}; 
                border: 2px solid {COLORS['border']}; 
                border-radius: 10px; 
                padding: 10px 15px; 
                font-size: 14px;
            }} 
            QComboBox:hover {{
                border-color: {COLORS['accent_primary']};
            }}
            QComboBox::drop-down {{
                border: none;
                padding-right: 10px;
            }}
            QComboBox QAbstractItemView {{ 
                background: {COLORS['bg_secondary']}; 
                color: {COLORS['text_primary']}; 
                selection-background-color: {COLORS['accent_primary']};
                border: 1px solid {COLORS['border']};
                border-radius: 8px;
                padding: 5px;
            }}
        """)
        card_layout.addWidget(self.username_combo)
        
        # Password entry
        self.password_entry = QLineEdit()
        self.password_entry.setPlaceholderText("Enter your password")
        self.password_entry.setEchoMode(QLineEdit.EchoMode.Password)
        self.password_entry.setFixedHeight(45)
        self.password_entry.setStyleSheet(f"""
            QLineEdit {{ 
                background: {COLORS['bg_tertiary']}; 
                color: {COLORS['text_primary']}; 
                border: 2px solid {COLORS['border']}; 
                border-radius: 10px; 
                padding: 10px 15px; 
                font-size: 14px;
            }} 
            QLineEdit:focus {{ 
                border: 2px solid {COLORS['accent_primary']}; 
            }}
        """)
        self.password_entry.returnPressed.connect(self.attempt_login)
        card_layout.addWidget(self.password_entry)
        
        card_layout.addSpacing(10)
        
        # Login button
        self.login_btn = QPushButton("Sign In →")
        self.login_btn.setFixedHeight(45)
        self.login_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        self.login_btn.setStyleSheet(f"""
            QPushButton {{ 
                background: {COLORS['accent_primary']}; 
                color: white; 
                border: none; 
                border-radius: 10px; 
                font-size: 15px; 
                font-weight: bold;
            }} 
            QPushButton:hover {{ 
                background: {COLORS['accent_hover']}; 
            }}
            QPushButton:pressed {{
                background: #2563eb;
            }}
        """)
        self.login_btn.clicked.connect(self.attempt_login)
        card_layout.addWidget(self.login_btn)
        
        card_layout.addStretch()
        
        # Back button
        back_btn = QPushButton("← Back")
        back_btn.setFixedHeight(40)
        back_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        back_btn.setStyleSheet(f"""
            QPushButton {{ 
                background: transparent; 
                color: {COLORS['text_secondary']}; 
                border: 1px solid {COLORS['border']}; 
                border-radius: 8px; 
                font-size: 13px;
            }} 
            QPushButton:hover {{ 
                background: rgba(255, 255, 255, 0.1);
                color: {COLORS['text_primary']};
            }}
        """)
        back_btn.clicked.connect(self.hide_login_form)
        card_layout.addWidget(back_btn)
    
    def enterEvent(self, event):
        """Mouse enter event"""
        pass
    
    def leaveEvent(self, event):
        """Mouse leave event"""
        pass
    
    def mousePressEvent(self, event):
        """Show login form on click"""
        if not self.login_form_visible and event.button() == Qt.MouseButton.LeftButton:
            self.show_login_form()
    
    def show_login_form(self):
        """Show login form with fade animation"""
        if not self.login_form_visible:
            self.login_form_visible = True
            
            # Switch to login.png background for login form
            login_bg = Path('/home/yousuf-yasser-elshaer/codes/os/assets/login.png')
            if login_bg.exists():
                self.background_pixmap = QPixmap(str(login_bg))
                self.update()
            
            # Position login card in center
            self.position_login_card()
            
            # Fade in animation
            self.login_card.setWindowOpacity(0)
            self.login_card.show()
            
            self.fade_anim = QPropertyAnimation(self.login_card, b"windowOpacity")
            self.fade_anim.setDuration(300)
            self.fade_anim.setStartValue(0)
            self.fade_anim.setEndValue(1)
            self.fade_anim.setEasingCurve(QEasingCurve.Type.OutCubic)
            self.fade_anim.start()
            
            # Focus password field
            self.password_entry.setFocus()
    
    def hide_login_form(self):
        """Hide login form"""
        if self.login_form_visible:
            self.login_form_visible = False
            self.login_card.hide()
            self.password_entry.clear()
    
    def position_login_card(self):
        """Position login card in center of screen"""
        if hasattr(self, 'login_card'):
            x = (self.width() - self.login_card.width()) // 2
            y = (self.height() - self.login_card.height()) // 2
            self.login_card.move(x, y)
    
    def resizeEvent(self, event):
        """Handle resize event to reposition login card"""
        super().resizeEvent(event)
        if hasattr(self, 'login_card') and self.login_form_visible:
            self.position_login_card()
    
    def start_clock(self):
        """Start clock update timer"""
        self.update_clock()
        self.clock_timer = QTimer()
        self.clock_timer.timeout.connect(self.update_clock)
        self.clock_timer.start(1000)
    
    def update_clock(self):
        """Update clock display"""
        time = QTime.currentTime()
        date = QDate.currentDate()
        
        self.time_label.setText(time.toString('hh:mm'))
        self.date_label.setText(date.toString('dddd, MMMM d, yyyy'))
    
    def attempt_login(self):
        """Attempt to log in"""
        username = self.username_combo.currentText()
        password = self.password_entry.text()
        
        if not password:
            self.shake_animation()
            return
        
        if self.auth.authenticate(username, password):
            self.show_loading()
            QTimer.singleShot(1500, lambda: self.login_success.emit(username))
        else:
            self.shake_animation()
    
    def shake_animation(self):
        """Shake animation for wrong password"""
        self.password_entry.clear()
        self.password_entry.setStyleSheet(f"""
            QLineEdit {{ 
                background: {COLORS['bg_tertiary']}; 
                color: {COLORS['text_primary']}; 
                border: 2px solid {COLORS['error']}; 
                border-radius: 10px; 
                padding: 10px 15px; 
                font-size: 14px;
            }}
        """)
        
        # Reset style after delay
        QTimer.singleShot(500, lambda: self.password_entry.setStyleSheet(f"""
            QLineEdit {{ 
                background: {COLORS['bg_tertiary']}; 
                color: {COLORS['text_primary']}; 
                border: 2px solid {COLORS['border']}; 
                border-radius: 10px; 
                padding: 10px 15px; 
                font-size: 14px;
            }} 
            QLineEdit:focus {{ 
                border: 2px solid {COLORS['accent_primary']}; 
            }}
        """))
        
        self.password_entry.setFocus()
    
    def show_loading(self):
        """Show loading animation after successful login"""
        # Background already set to login.png from login form, no need to change
        
        # Hide all widgets except loading
        self.login_card.hide()
        self.clock_widget.hide()
        self.shutdown_btn.hide()
        self.restart_btn.hide()
        
        # Create loading container
        self.loading_container = QWidget(self)
        loading_layout = QVBoxLayout(self.loading_container)
        loading_layout.setAlignment(Qt.AlignmentFlag.AlignCenter)
        
        # Welcome message with loading circle
        welcome_container = QHBoxLayout()
        welcome_container.setSpacing(20)
        
        # Loading circle
        loading_circle = LoadingCircle()
        welcome_container.addWidget(loading_circle)
        
        # Welcome text
        welcome_label = QLabel("Welcome")
        welcome_label.setStyleSheet(f"""
            color: white;
            font-size: 56px;
            font-weight: bold;
        """)
        welcome_container.addWidget(welcome_label)
        
        loading_layout.addLayout(welcome_container)
        
        # Position and show
        self.loading_container.setGeometry(0, 0, self.width(), self.height())
        self.loading_container.show()
    
    def handle_shutdown(self):
        """Handle shutdown button click"""
        self.shutdown_requested.emit()
    
    def handle_restart(self):
        """Handle restart button click"""
        self.restart_requested.emit()



class YouOSMainWindow(QMainWindow):
    """Main YouOS application window"""
    
    def __init__(self):
        super().__init__()
        self.setWindowTitle("YouOS 10")
        self.showFullScreen()
        
        # Initialize auth manager
        self.auth = AuthManager()
        self.current_user = None
        self.desktop_widget = None
        
        # Setup stacked widget for screens
        self.stacked_widget = QStackedWidget()
        self.setCentralWidget(self.stacked_widget)
        
        # Apply background gradient
        self.apply_background()
        
        # Show boot screen
        self.show_boot_screen()
    
    def apply_background(self):
        """Apply background based on current screen"""
        pass  # Background will be set per screen
    
    def show_boot_screen(self):
        """Show boot screen"""
        self.setStyleSheet("QMainWindow { background: black; }")
        self.boot_screen = BootScreen()
        self.boot_screen.boot_complete.connect(self.show_login_screen)
        self.stacked_widget.addWidget(self.boot_screen)
        self.stacked_widget.setCurrentWidget(self.boot_screen)
        self.boot_screen.start_boot()
    
    def show_login_screen(self):
        """Show login screen"""
        self.login_screen = LoginScreen(self.auth)
        self.login_screen.login_success.connect(self.create_desktop)
        self.login_screen.shutdown_requested.connect(self.handle_shutdown)
        self.login_screen.restart_requested.connect(self.handle_restart)
        self.stacked_widget.addWidget(self.login_screen)
        self.stacked_widget.setCurrentWidget(self.login_screen)
    
    def create_desktop(self, username):
        """Create desktop environment"""
        self.current_user = username
        
        # Save current user to config for settings app
        try:
            import json
            from pathlib import Path
            config_path = Path.home() / '.youos' / '../config.json'
            if not config_path.parent.exists():
                config_path.parent.mkdir(parents=True, exist_ok=True)
            
            config = {}
            if config_path.exists():
                with open(config_path, 'r') as f:
                    config = json.load(f)
            
            config['current_user'] = username
            
            with open(config_path, 'w') as f:
                json.dump(config, f, indent=4)
        except Exception as e:
            print(f"⚠️  Failed to save current user to config: {e}")
        
        # Import desktop manager here to avoid circular imports
        from desktop import DesktopManager
        
        self.desktop_widget = DesktopManager(self.auth, username, self)
        self.desktop_widget.logout_requested.connect(self.handle_logout)
        self.desktop_widget.restart_requested.connect(self.handle_restart)
        self.desktop_widget.shutdown_requested.connect(self.handle_shutdown)
        
        self.stacked_widget.addWidget(self.desktop_widget)
        self.stacked_widget.setCurrentWidget(self.desktop_widget)
        
        # Play logon sound
        try:
            from utils import play_sound
            play_sound('logon.wav')
        except:
            pass
        
        print(f"Logged in as: {username}")
    
    def handle_logout(self):
        """Handle logout"""
        # Play logoff sound
        try:
            from utils import play_sound
            play_sound('logoff.wav')
        except:
            pass
        
        self.show_shutdown_screen('logout')
    
    def handle_restart(self):
        """Handle restart"""
        self.show_shutdown_screen('restart')
    
    def handle_shutdown(self):
        """Handle shutdown"""
        self.show_shutdown_screen('shutdown')
    
    def show_shutdown_screen(self, action):
        """Show shutdown screen"""
        self.setStyleSheet("QMainWindow { background: black; }")
        shutdown_screen = ShutdownScreen(action)
        shutdown_screen.shutdown_complete.connect(lambda: self.complete_action(action))
        self.stacked_widget.addWidget(shutdown_screen)
        self.stacked_widget.setCurrentWidget(shutdown_screen)
        shutdown_screen.start_shutdown()
    
    def complete_action(self, action):
        """Complete the shutdown/restart/logout action"""
        if action == 'logout':
            # Remove desktop widget and go back to login
            if self.desktop_widget:
                self.stacked_widget.removeWidget(self.desktop_widget)
                self.desktop_widget.deleteLater()
                self.desktop_widget = None
            self.show_login_screen()
        elif action == 'restart':
            # Restart the application
            import sys
            import os
            python = sys.executable
            os.execl(python, python, *sys.argv)
        else:  # shutdown
            # Close application
            QApplication.quit()


class DesktopManager(GlassWidget):
    """Desktop manager with browser launch capability"""
    
    logout_requested = pyqtSignal()
    restart_requested = pyqtSignal()
    shutdown_requested = pyqtSignal()
    
    def __init__(self, auth_manager, username, parent=None):
        super().__init__(parent)
        self.auth = auth_manager
        self.username = username
        self.setup_ui()
    
    def setup_ui(self):
        """Setup desktop UI"""
        layout = QVBoxLayout(self)
        layout.setAlignment(Qt.AlignmentFlag.AlignCenter)
        
        # Browser button
        browser_btn = QPushButton("Open Browser")
        browser_btn.setFixedHeight(40)
        browser_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        browser_btn.setStyleSheet(f"""
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
        browser_btn.clicked.connect(self.launch_browser)
        layout.addWidget(browser_btn)
        
        # Other desktop elements would go here
    
    def launch_browser(self):
        """Launch browser"""
        from browser import Browser
        self.browser_window = Browser(is_standalone=False)
        self.browser_window.show()
    
    # Other methods for desktop management would go here


def main():
    """Main application entry point"""
    app = QApplication(sys.argv)
    
    # Set global font
    font = QFont("Segoe UI", 10)
    app.setFont(font)
    
    # Set Fusion style
    app.setStyle("Fusion")
    
    # Create and show main window
    window = YouOSMainWindow()
    window.show()
    
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
