"""
YouOS 10 PyQt6 - Start Menu Module
start.py - Start menu creation and logic
"""

from PyQt6.QtWidgets import (QWidget, QVBoxLayout, QHBoxLayout, QLabel, 
                              QPushButton, QGridLayout, QScrollArea, QLineEdit, QMenu)
from PyQt6.QtCore import Qt, pyqtSignal
from PyQt6.QtGui import QCursor, QAction

try:
    from widgets import GlassFrame
    from utils import play_sound
except ImportError:
    def play_sound(name):
        pass
    class GlassFrame(QWidget):
        def __init__(self, parent=None, opacity=0.15):
            super().__init__(parent)

COLORS = {
    'bg_primary': '#0f0f1e',
    'bg_secondary': '#1a1a2e',
    'bg_tertiary': '#252538',
    'accent_primary': '#3b82f6',
    'accent_hover': '#60a5fa',
    'text_primary': '#ffffff',
    'text_secondary': '#9ca3af',
    'border': '#374151',
    'error': '#ef4444',
}


class ProgramButton(QPushButton):
    """Custom program button with context menu"""
    
    pin_toggled = pyqtSignal(str, bool)
    add_to_desktop = pyqtSignal(str)
    
    def __init__(self, name, icon, desktop_manager, parent=None):
        super().__init__(f"{icon}\n{name}", parent)
        self.name = name
        self.icon = icon
        self.desktop_manager = desktop_manager
        self.setFixedSize(120, 100)
        self.setCursor(Qt.CursorShape.PointingHandCursor)
        self.setStyleSheet("""
            QPushButton { 
                background: rgba(255, 255, 255, 0.1); 
                border: 1px solid rgba(255, 255, 255, 0.2); 
                border-radius: 12px; 
                color: white; 
                font-size: 11px; 
            } 
            QPushButton:hover { 
                background: rgba(255, 255, 255, 0.2); 
            }
        """)
    
    def mousePressEvent(self, event):
        if event.button() == Qt.MouseButton.RightButton:
            self.show_context_menu(event.globalPosition().toPoint())
        else:
            super().mousePressEvent(event)
    
    def show_context_menu(self, pos):
        menu = QMenu(self)
        menu.setStyleSheet(f"""
            QMenu {{ 
                background: {COLORS['bg_secondary']}; 
                border: 1px solid {COLORS['border']}; 
                border-radius: 8px; 
                padding: 5px; 
            }}
            QMenu::item {{ 
                color: {COLORS['text_primary']}; 
                padding: 8px 20px; 
                border-radius: 4px; 
            }}
            QMenu::item:selected {{ 
                background: {COLORS['accent_primary']}; 
            }}
        """)
        
        # Check if already pinned
        is_pinned = self.name in self.desktop_manager.pinned_apps
        
        pin_text = "Unpin from Taskbar" if is_pinned else "Pin to Taskbar"
        pin_action = QAction(pin_text, self)
        pin_action.triggered.connect(lambda: self.pin_toggled.emit(self.name, not is_pinned))
        menu.addAction(pin_action)
        
        add_desktop_action = QAction("Add Icon to Desktop", self)
        add_desktop_action.triggered.connect(lambda: self.add_to_desktop.emit(self.name))
        menu.addAction(add_desktop_action)
        
        menu.exec(pos)


class StartMenu(GlassFrame):
    """Start menu with dynamic program loading"""
    
    program_clicked = pyqtSignal(str)
    logout_clicked = pyqtSignal()
    restart_clicked = pyqtSignal()
    shutdown_clicked = pyqtSignal()
    pin_toggled = pyqtSignal(str, bool)
    add_to_desktop = pyqtSignal(str)
    
    def __init__(self, installed_programs, desktop_manager, parent=None):
        super().__init__(parent, opacity=0.20)
        self.setFixedSize(600, 700)
        self.installed_programs = installed_programs
        self.desktop_manager = desktop_manager
        self.setup_ui()
    
    def setup_ui(self):
        layout = QVBoxLayout(self)
        layout.setContentsMargins(30, 30, 30, 30)
        layout.setSpacing(20)
        
        # Header
        header = QHBoxLayout()
        logo = QLabel("YouOS")
        logo.setStyleSheet("color: #3b82f6; font-size: 24px; font-weight: bold;")
        header.addWidget(logo)
        header.addStretch()
        user_label = QLabel("Welcome, User!")
        user_label.setStyleSheet("color: #9ca3af; font-size: 12px;")
        header.addWidget(user_label)
        layout.addLayout(header)
        
        # Search bar
        search = QLineEdit()
        search.setPlaceholderText("Search programs...")
        search.setFixedHeight(40)
        search.setStyleSheet("""
            QLineEdit { 
                background: rgba(255, 255, 255, 0.1); 
                border: 1px solid rgba(255, 255, 255, 0.2); 
                border-radius: 8px; 
                color: white; 
                padding: 10px; 
                font-size: 14px; 
            } 
            QLineEdit:focus { 
                border: 1px solid #3b82f6; 
            }
        """)
        search.textChanged.connect(self.filter_programs)
        layout.addWidget(search)
        
        # Programs grid
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
        scroll.setVerticalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAsNeeded)
        scroll.setStyleSheet("QScrollArea { background: transparent; border: none; }")
        programs_widget = QWidget()
        self.programs_layout = QGridLayout(programs_widget)
        self.programs_layout.setSpacing(15)
        
        self.refresh_programs()
        
        scroll.setWidget(programs_widget)
        layout.addWidget(scroll)
        
        # Power buttons
        power_layout = QHBoxLayout()
        
        self.logout_btn = QPushButton("🔓 Logout")
        self.logout_btn.setFixedHeight(40)
        self.logout_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        self.logout_btn.setStyleSheet("""
            QPushButton { 
                background: rgba(255, 255, 255, 0.1); 
                border: 1px solid rgba(255, 255, 255, 0.2); 
                border-radius: 8px; 
                color: white; 
                font-size: 12px; 
            } 
            QPushButton:hover { 
                background: rgba(239, 68, 68, 0.3); 
            }
        """)
        self.logout_btn.clicked.connect(self.on_logout)
        power_layout.addWidget(self.logout_btn)
        
        self.restart_btn = QPushButton("🔄 Restart")
        self.restart_btn.setFixedHeight(40)
        self.restart_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        self.restart_btn.setStyleSheet("""
            QPushButton { 
                background: rgba(255, 255, 255, 0.1); 
                border: 1px solid rgba(255, 255, 255, 0.2); 
                border-radius: 8px; 
                color: white; 
                font-size: 12px; 
            } 
            QPushButton:hover { 
                background: rgba(239, 68, 68, 0.3); 
            }
        """)
        self.restart_btn.clicked.connect(self.on_restart)
        power_layout.addWidget(self.restart_btn)
        
        self.shutdown_btn = QPushButton("⏻ Shutdown")
        self.shutdown_btn.setFixedHeight(40)
        self.shutdown_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        self.shutdown_btn.setStyleSheet("""
            QPushButton { 
                background: rgba(255, 255, 255, 0.1); 
                border: 1px solid rgba(255, 255, 255, 0.2); 
                border-radius: 8px; 
                color: white; 
                font-size: 12px; 
            } 
            QPushButton:hover { 
                background: rgba(239, 68, 68, 0.3); 
            }
        """)
        self.shutdown_btn.clicked.connect(self.on_shutdown)
        power_layout.addWidget(self.shutdown_btn)
        
        layout.addLayout(power_layout)
    
    def refresh_programs(self, filter_text=""):
        """Refresh the program grid with optional filtering"""
        # Clear existing widgets
        for i in reversed(range(self.programs_layout.count())):
            widget = self.programs_layout.itemAt(i).widget()
            if widget:
                widget.deleteLater()
        
        # Filter programs if search text provided
        programs_to_show = self.installed_programs
        if filter_text:
            filter_lower = filter_text.lower()
            programs_to_show = [
                (icon, name) for icon, name in self.installed_programs
                if filter_lower in name.lower()
            ]
        
        # Create program buttons
        for i, (icon, name) in enumerate(programs_to_show):
            btn = ProgramButton(name, icon, self.desktop_manager, self)
            btn.clicked.connect(lambda checked, n=name: self.on_program_click(n))
            btn.pin_toggled.connect(self.pin_toggled.emit)
            btn.add_to_desktop.connect(self.add_to_desktop.emit)
            self.programs_layout.addWidget(btn, i // 4, i % 4)
    
    def filter_programs(self, text):
        """Filter programs based on search text"""
        self.refresh_programs(text)
    
    def on_program_click(self, name):
        """Handle program button click"""
        play_sound("click.wav")
        self.program_clicked.emit(name)
    
    def on_logout(self):
        """Handle logout button click"""
        play_sound("click.wav")
        self.logout_clicked.emit()
    
    def on_restart(self):
        """Handle restart button click"""
        play_sound("click.wav")
        self.restart_clicked.emit()
    
    def on_shutdown(self):
        """Handle shutdown button click"""
        play_sound("click.wav")
        self.shutdown_clicked.emit()