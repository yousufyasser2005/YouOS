"""
YouOS - Desktop Environment
main.py - Main Application
"""

import sys
import json
import os
from pathlib import Path
from PyQt6.QtWidgets import (QApplication, QMainWindow, QWidget, QVBoxLayout,
                              QHBoxLayout, QLabel, QPushButton, QLineEdit,
                              QGridLayout, QFrame, QGraphicsDropShadowEffect,
                              QScrollArea, QStackedWidget, QComboBox, QProgressBar)
from PyQt6.QtCore import (Qt, QTimer, QTime, QDate, pyqtSignal, QPoint, QRect)
from PyQt6.QtGui import (QFont, QPalette, QColor, QLinearGradient, QPainter,
                         QPen, QBrush, QPixmap, QIcon, QPainterPath)
import hashlib

# Configuration
BASE_DIR = Path(__file__).parent
APP_DIR = Path.home() / '.youos'
APP_DIR.mkdir(exist_ok=True)
USERS_FILE = APP_DIR / 'users.json'

# Color scheme
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
                    'profile_picture': '',
                    'pinned_apps': [],
                    'desktop_icons': {},
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
            'profile_picture': '',
            'pinned_apps': [],
            'desktop_icons': {},
        }
        self.save_users()
        return True, "User created successfully"

    def get_all_usernames(self):
        """Get all usernames"""
        return list(self.users.keys())

    def get_user_data(self, username):
        """Get user data"""
        return self.users.get(username, {})


class BootScreen(QWidget):
    """Boot screen"""

    boot_complete = pyqtSignal()

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setup_ui()

    def setup_ui(self):
        layout = QVBoxLayout(self)
        layout.setAlignment(Qt.AlignmentFlag.AlignCenter)
        layout.setSpacing(30)

        # Loading bar
        self.loading_bar = QWidget()
        self.loading_bar.setFixedSize(400, 6)
        self.loading_bar.setStyleSheet(f"background: {COLORS['bg_tertiary']}; border-radius: 3px;")
        layout.addWidget(self.loading_bar, alignment=Qt.AlignmentFlag.AlignCenter)

        # Moving indicator
        self.indicator = QWidget(self.loading_bar)
        self.indicator.setFixedSize(80, 6)
        self.indicator.setStyleSheet(f"background: {COLORS['accent_primary']}; border-radius: 3px;")
        self.indicator.move(-80, 0)

        # Loading text
        self.loading_text = QLabel("Starting YouOS...")
        self.loading_text.setStyleSheet(f"color: {COLORS['text_secondary']}; font-size: 14px;")
        self.loading_text.setAlignment(Qt.AlignmentFlag.AlignCenter)
        layout.addWidget(self.loading_text)

        # Logo
        self.logo_label = QLabel("YouOS")
        self.logo_label.setStyleSheet(f"color: {COLORS['text_primary']}; font-size: 56px; font-weight: bold;")
        self.logo_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.logo_label.hide()
        layout.addWidget(self.logo_label)

    def start_boot(self):
        """Start boot sequence"""
        # Start moving bar animation
        self.move_timer = QTimer()
        self.move_timer.timeout.connect(self.move_indicator)
        self.move_timer.start(16)

        # Update loading text progressively
        self.loading_steps = [
            "Initializing kernel...",
            "Loading modules...",
            "Starting services...",
            "Preparing desktop...",
            "Almost ready..."
        ]
        self.current_step = 0

        self.text_timer = QTimer()
        self.text_timer.timeout.connect(self.update_loading_text)
        self.text_timer.start(400)

        # Complete boot after delay
        QTimer.singleShot(2000, self.show_logo)

    def update_loading_text(self):
        """Update loading text progressively"""
        if self.current_step < len(self.loading_steps):
            self.loading_text.setText(self.loading_steps[self.current_step])
            self.current_step += 1

    def move_indicator(self):
        """Move the loading indicator"""
        current_x = self.indicator.x()
        if current_x >= 400:
            current_x = -80
        else:
            current_x += 4
        self.indicator.move(current_x, 0)

    def show_logo(self):
        """Show logo and complete boot"""
        self.move_timer.stop()
        self.text_timer.stop()
        self.loading_bar.hide()
        self.loading_text.hide()
        self.logo_label.show()
        QTimer.singleShot(1000, self.boot_complete.emit)


class UserSelectionWidget(QWidget):
    """User selection screen"""

    user_selected = pyqtSignal(str)

    def __init__(self, auth_manager, parent=None):
        super().__init__(parent)
        self.auth = auth_manager
        self.setup_ui()

    def setup_ui(self):
        layout = QVBoxLayout(self)
        layout.setContentsMargins(50, 50, 50, 50)
        layout.setSpacing(30)

        # Title
        title = QLabel("Select User")
        title.setStyleSheet(f"color: {COLORS['text_primary']}; font-size: 32px; font-weight: bold;")
        title.setAlignment(Qt.AlignmentFlag.AlignCenter)
        layout.addWidget(title)

        # User grid
        users_container = QWidget()
        users_layout = QGridLayout(users_container)
        users_layout.setSpacing(25)

        usernames = self.auth.get_all_usernames()
        for i, username in enumerate(usernames):
            user_data = self.auth.get_user_data(username)

            # User frame
            user_frame = QFrame()
            user_frame.setFixedSize(140, 170)
            user_frame.setStyleSheet(f"""
                QFrame {{
                    background: {COLORS['bg_secondary']};
                    border: 2px solid {COLORS['border']};
                    border-radius: 16px;
                }}
                QFrame:hover {{
                    border-color: {COLORS['accent_primary']};
                }}
            """)

            frame_layout = QVBoxLayout(user_frame)
            frame_layout.setContentsMargins(15, 20, 15, 15)
            frame_layout.setSpacing(12)
            frame_layout.setAlignment(Qt.AlignmentFlag.AlignCenter)

            # Profile picture placeholder
            profile_pic = QLabel("👤")
            profile_pic.setStyleSheet(f"font-size: 60px; color: {COLORS['text_secondary']};")
            frame_layout.addWidget(profile_pic)

            # Username
            username_label = QLabel(username)
            username_label.setStyleSheet(f"color: {COLORS['text_primary']}; font-size: 14px; font-weight: 600;")
            username_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
            frame_layout.addWidget(username_label)

            # Make clickable
            user_frame.mousePressEvent = lambda e, u=username: self.user_selected.emit(u)

            # Add to grid (3 columns)
            row = i // 3
            col = i % 3
            users_layout.addWidget(user_frame, row, col)

        layout.addWidget(users_container, alignment=Qt.AlignmentFlag.AlignCenter)


class UserLoginWidget(QWidget):
    """User login form"""

    login_success = pyqtSignal(str)
    back_requested = pyqtSignal()

    def __init__(self, auth_manager, username, parent=None):
        super().__init__(parent)
        self.auth = auth_manager
        self.username = username
        self.setup_ui()

    def setup_ui(self):
        self.setFixedSize(420, 350)
        layout = QVBoxLayout(self)
        layout.setContentsMargins(40, 40, 40, 40)
        layout.setSpacing(25)

        # User info header
        header_layout = QHBoxLayout()

        # Profile picture
        profile_pic = QLabel("👤")
        profile_pic.setStyleSheet(f"font-size: 70px; color: {COLORS['text_secondary']};")
        header_layout.addWidget(profile_pic)

        # Username
        username_label = QLabel(self.username)
        username_label.setStyleSheet(f"color: {COLORS['text_primary']}; font-size: 24px; font-weight: bold;")
        username_label.setAlignment(Qt.AlignmentFlag.AlignLeft | Qt.AlignmentFlag.AlignVCenter)
        header_layout.addWidget(username_label)

        header_layout.addStretch()
        layout.addLayout(header_layout)

        # Password field
        self.password_entry = QLineEdit()
        self.password_entry.setPlaceholderText("Enter password")
        self.password_entry.setEchoMode(QLineEdit.EchoMode.Password)
        self.password_entry.setFixedHeight(50)
        self.password_entry.returnPressed.connect(self.attempt_login)
        self.password_entry.setStyleSheet(f"""
            QLineEdit {{
                background: {COLORS['bg_tertiary']};
                color: {COLORS['text_primary']};
                border: 2px solid {COLORS['border']};
                border-radius: 10px;
                padding: 12px 16px;
                font-size: 14px;
            }}
            QLineEdit:focus {{
                border-color: {COLORS['accent_primary']};
            }}
        """)
        layout.addWidget(self.password_entry)

        # Login button
        self.login_btn = QPushButton("Sign In")
        self.login_btn.setFixedHeight(50)
        self.login_btn.clicked.connect(self.attempt_login)
        self.login_btn.setStyleSheet(f"""
            QPushButton {{
                background: {COLORS['accent_primary']};
                color: white;
                border: none;
                border-radius: 10px;
                font-size: 16px;
                font-weight: 600;
            }}
            QPushButton:hover {{
                background: {COLORS['accent_hover']};
            }}
        """)
        layout.addWidget(self.login_btn)

        # Back button
        back_btn = QPushButton("← Back")
        back_btn.setFixedHeight(45)
        back_btn.clicked.connect(self.back_requested.emit)
        back_btn.setStyleSheet(f"""
            QPushButton {{
                background: transparent;
                color: {COLORS['text_secondary']};
                border: none;
                font-size: 14px;
            }}
            QPushButton:hover {{
                color: {COLORS['accent_primary']};
            }}
        """)
        layout.addWidget(back_btn)

        # Focus password field
        self.password_entry.setFocus()

    def attempt_login(self):
        """Attempt login"""
        password = self.password_entry.text()
        if not password:
            return

        if self.auth.authenticate(self.username, password):
            self.login_success.emit(self.username)
        else:
            self.password_entry.clear()
            self.password_entry.setFocus()


class LoginScreen(QWidget):
    """Login screen"""

    login_success = pyqtSignal(str)
    shutdown_requested = pyqtSignal()
    restart_requested = pyqtSignal()

    def __init__(self, auth_manager, parent=None):
        super().__init__(parent)
        self.auth = auth_manager
        self.current_widget = None
        self.setup_ui()
        self.start_clock()

    def paintEvent(self, event):
        """Custom paint for background"""
        painter = QPainter(self)
        gradient = QLinearGradient(0, 0, self.width(), self.height())
        gradient.setColorAt(0, QColor('#0f0f1e'))
        gradient.setColorAt(0.4, QColor('#1a1a3e'))
        gradient.setColorAt(0.6, QColor('#15153a'))
        gradient.setColorAt(1, QColor('#0f0f1e'))
        painter.fillRect(self.rect(), gradient)

    def setup_ui(self):
        main_layout = QVBoxLayout(self)
        main_layout.setContentsMargins(0, 0, 0, 0)
        main_layout.setSpacing(0)

        main_layout.addStretch(2)

        # Center area
        self.center_container = QWidget()
        self.center_container.setStyleSheet("background: transparent;")
        self.center_container.setFixedSize(800, 600)
        main_layout.addWidget(self.center_container, alignment=Qt.AlignmentFlag.AlignCenter)
        main_layout.addStretch(2)

        # Bottom bar
        bottom_bar = QWidget()
        bottom_bar.setStyleSheet("background: transparent;")
        bottom_bar.setFixedHeight(160)
        bottom_layout = QHBoxLayout(bottom_bar)
        bottom_layout.setContentsMargins(40, 25, 40, 35)

        # Power buttons
        power_container = QWidget()
        power_container.setFixedWidth(80)
        power_layout = QVBoxLayout(power_container)
        power_layout.setSpacing(12)

        # Shutdown button
        self.shutdown_btn = QPushButton("⏻")
        self.shutdown_btn.setFixedSize(55, 55)
        self.shutdown_btn.clicked.connect(self.shutdown_requested.emit)
        self.shutdown_btn.setStyleSheet(f"""
            QPushButton {{
                background: rgba(0,0,0,0.6);
                color: white;
                border: 2px solid rgba(255,255,255,0.3);
                border-radius: 28px;
                font-size: 22px;
            }}
            QPushButton:hover {{
                background: {COLORS['error']};
                border-color: {COLORS['error']};
            }}
        """)
        power_layout.addWidget(self.shutdown_btn)

        # Restart button
        self.restart_btn = QPushButton("🔄")
        self.restart_btn.setFixedSize(55, 55)
        self.restart_btn.clicked.connect(self.restart_requested.emit)
        self.restart_btn.setStyleSheet(f"""
            QPushButton {{
                background: rgba(0,0,0,0.6);
                color: white;
                border: 2px solid rgba(255,255,255,0.3);
                border-radius: 28px;
                font-size: 20px;
            }}
            QPushButton:hover {{
                background: {COLORS['warning']};
                border-color: {COLORS['warning']};
            }}
        """)
        power_layout.addWidget(self.restart_btn)

        power_layout.addStretch()
        bottom_layout.addWidget(power_container)
        bottom_layout.addStretch()

        # Clock widget
        self.clock_widget = QWidget()
        clock_layout = QVBoxLayout(self.clock_widget)
        clock_layout.setContentsMargins(0, 0, 0, 0)
        clock_layout.setSpacing(5)

        self.time_label = QLabel()
        self.time_label.setStyleSheet("color: white; font-size: 72px; font-weight: bold; letter-spacing: -2px;")
        self.time_label.setAlignment(Qt.AlignmentFlag.AlignRight)
        clock_layout.addWidget(self.time_label)

        self.date_label = QLabel()
        self.date_label.setStyleSheet("color: rgba(255,255,255,0.8); font-size: 26px; font-weight: 500;")
        self.date_label.setAlignment(Qt.AlignmentFlag.AlignRight)
        clock_layout.addWidget(self.date_label)

        bottom_layout.addWidget(self.clock_widget)
        main_layout.addWidget(bottom_bar)

        self.setMouseTracking(True)

    def mousePressEvent(self, event):
        if event.button() == Qt.MouseButton.LeftButton and not self.current_widget:
            self.show_user_selection()

    def show_user_selection(self):
        """Show user selection screen"""
        if self.current_widget:
            self.current_widget.hide()
            self.current_widget.deleteLater()

        self.user_selection = UserSelectionWidget(self.auth, self)
        self.user_selection.user_selected.connect(self.show_user_login)
        self.user_selection.setFixedSize(650, 450)

        self.user_selection.move(
            (self.width() - self.user_selection.width()) // 2,
            (self.height() - self.user_selection.height()) // 2
        )
        self.user_selection.show()
        self.current_widget = self.user_selection

    def show_user_login(self, username):
        """Show login form for user"""
        if self.current_widget:
            self.current_widget.hide()
            self.current_widget.deleteLater()

        self.user_login = UserLoginWidget(self.auth, username, self)
        self.user_login.login_success.connect(self.handle_login_success)
        self.user_login.back_requested.connect(self.show_user_selection)

        self.user_login.move(
            (self.width() - self.user_login.width()) // 2,
            (self.height() - self.user_login.height()) // 2
        )
        self.user_login.show()
        self.current_widget = self.user_login

    def resizeEvent(self, event):
        super().resizeEvent(event)
        if self.current_widget:
            self.current_widget.move(
                (self.width() - self.current_widget.width()) // 2,
                (self.height() - self.current_widget.height()) // 2
            )

    def handle_login_success(self, username):
        """Handle successful login"""
        self.show_loading()
        QTimer.singleShot(500, lambda: self.login_success.emit(username))

    def show_loading(self):
        """Show loading screen"""
        if self.current_widget:
            self.current_widget.hide()

        self.clock_widget.hide()
        self.shutdown_btn.hide()
        self.restart_btn.hide()

        self.loading_container = QWidget(self)
        loading_layout = QVBoxLayout(self.loading_container)
        loading_layout.setAlignment(Qt.AlignmentFlag.AlignCenter)
        loading_layout.setSpacing(25)

        # Welcome container
        welcome_container = QHBoxLayout()
        welcome_container.setSpacing(25)
        welcome_container.setAlignment(Qt.AlignmentFlag.AlignCenter)

        # Loading circle
        loading_circle = QLabel("⟳")
        loading_circle.setStyleSheet("font-size: 60px; color: #3b82f6;")
        welcome_container.addWidget(loading_circle)

        # Welcome label
        welcome_label = QLabel("Welcome")
        welcome_label.setStyleSheet("color: white; font-size: 64px; font-weight: bold; letter-spacing: 2px;")
        welcome_container.addWidget(welcome_label)

        loading_layout.addLayout(welcome_container)

        self.loading_container.setGeometry(0, 0, self.width(), self.height())
        self.loading_container.show()

    def start_clock(self):
        self.update_clock()
        self.clock_timer = QTimer()
        self.clock_timer.timeout.connect(self.update_clock)
        self.clock_timer.start(1000)

    def update_clock(self):
        time = QTime.currentTime()
        date = QDate.currentDate()
        self.time_label.setText(time.toString('HH:mm'))
        self.date_label.setText(date.toString('dddd, MMMM d, yyyy'))


class YouOSMainWindow(QMainWindow):
    """Main YouOS application window"""

    def __init__(self):
        super().__init__()
        self.setWindowTitle("YouOS")
        self.showFullScreen()

        # Initialize auth manager
        self.auth = AuthManager()
        self.current_user = None

        # Setup stacked widget for screens
        self.stacked_widget = QStackedWidget()
        self.setCentralWidget(self.stacked_widget)

        # Show boot screen
        self.show_boot_screen()

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

        # Import desktop manager here to avoid circular imports
        from desktop import DesktopManager

        self.desktop_widget = DesktopManager(self.auth, username, self)
        self.desktop_widget.logout_requested.connect(self.handle_logout)
        self.desktop_widget.restart_requested.connect(self.handle_restart)
        self.desktop_widget.shutdown_requested.connect(self.handle_shutdown)

        self.stacked_widget.addWidget(self.desktop_widget)
        self.stacked_widget.setCurrentWidget(self.desktop_widget)

    def handle_logout(self):
        """Handle logout"""
        if self.desktop_widget:
            self.stacked_widget.removeWidget(self.desktop_widget)
            self.desktop_widget.deleteLater()
            self.desktop_widget = None
        self.show_login_screen()

    def handle_restart(self):
        """Handle restart"""
        import sys
        import os
        python = sys.executable
        os.execl(python, python, *sys.argv)

    def handle_shutdown(self):
        """Handle shutdown"""
        QApplication.quit()


def main():
    """Main application entry point"""
    app = QApplication(sys.argv)

    # Set global font
    font = QFont("Segoe UI", 10)
    app.setFont(font)

    # Create and show main window
    window = YouOSMainWindow()
    window.show()

    sys.exit(app.exec())


if __name__ == "__main__":
    main()