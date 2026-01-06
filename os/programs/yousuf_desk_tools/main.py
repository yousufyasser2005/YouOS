"""
Yousuf Desk Tools - Office Suite for YouOS
main.py - Main entry point and launcher interface
"""

import sys
from pathlib import Path
from PyQt6.QtWidgets import (QApplication, QWidget, QVBoxLayout, QHBoxLayout, 
                              QLabel, QPushButton, QFrame, QStackedWidget)
from PyQt6.QtCore import Qt, pyqtSignal, QTimer, QPropertyAnimation, QEasingCurve
from PyQt6.QtGui import QFont, QIcon

try:
    from widgets import GlassFrame
    from utils import play_sound, BASE_DIR
except ImportError:
    BASE_DIR = Path(__file__).parent
    def play_sound(name):
        pass
    class GlassFrame(QFrame):
        def __init__(self, parent=None, opacity=0.15):
            super().__init__(parent)
            self.setStyleSheet(f"""
                QFrame {{
                    background: rgba(255, 255, 255, {opacity});
                    border: 1px solid rgba(255, 255, 255, 0.2);
                    border-radius: 16px;
                }}
            """)

from word_processor import WordProcessor
from spreadsheet import Spreadsheet
from presentation import Presentation

COLORS = {
    'bg_primary': '#0f0f1e',
    'bg_secondary': '#1a1a2e',
    'bg_tertiary': '#252538',
    'accent_primary': '#3b82f6',
    'accent_hover': '#60a5fa',
    'accent_word': '#2563eb',
    'accent_sheet': '#059669',
    'accent_slides': '#dc2626',
    'text_primary': '#ffffff',
    'text_secondary': '#9ca3af',
    'border': '#374151',
}


class ModuleTile(GlassFrame):
    """Tile for launching a module"""
    
    clicked = pyqtSignal(str)
    
    def __init__(self, module_name, icon, color, description, parent=None):
        super().__init__(parent, opacity=0.15)
        self.module_name = module_name
        self.icon = icon
        self.color = color
        self.description = description
        self.setFixedSize(280, 320)
        self.setCursor(Qt.CursorShape.PointingHandCursor)
        self.setup_ui()
    
    def setup_ui(self):
        layout = QVBoxLayout(self)
        layout.setContentsMargins(25, 30, 25, 30)
        layout.setSpacing(15)
        
        # Icon container with gradient background
        icon_container = QFrame()
        icon_container.setFixedSize(120, 120)
        icon_container.setStyleSheet(f"""
            QFrame {{
                background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                    stop:0 {self.color},
                    stop:1 rgba(59, 130, 246, 0.3));
                border: 2px solid rgba(255, 255, 255, 0.2);
                border-radius: 60px;
            }}
        """)
        
        icon_layout = QVBoxLayout(icon_container)
        icon_layout.setContentsMargins(0, 0, 0, 0)
        
        icon_label = QLabel(self.icon)
        icon_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        icon_label.setStyleSheet("font-size: 64px; background: transparent; border: none;")
        icon_layout.addWidget(icon_label)
        
        layout.addWidget(icon_container, alignment=Qt.AlignmentFlag.AlignCenter)
        
        # Module name
        name_label = QLabel(self.module_name)
        name_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        name_label.setStyleSheet(f"""
            color: {COLORS['text_primary']};
            font-size: 22px;
            font-weight: bold;
            background: transparent;
            border: none;
        """)
        layout.addWidget(name_label)
        
        # Description
        desc_label = QLabel(self.description)
        desc_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        desc_label.setWordWrap(True)
        desc_label.setStyleSheet(f"""
            color: {COLORS['text_secondary']};
            font-size: 13px;
            background: transparent;
            border: none;
        """)
        layout.addWidget(desc_label)
        
        layout.addStretch()
        
        # Launch button
        launch_btn = QPushButton("Launch")
        launch_btn.setFixedHeight(45)
        launch_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        launch_btn.setStyleSheet(f"""
            QPushButton {{
                background: {self.color};
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
        launch_btn.clicked.connect(lambda: self.clicked.emit(self.module_name))
        layout.addWidget(launch_btn)
    
    def enterEvent(self, event):
        self.setStyleSheet(f"""
            GlassFrame {{
                background: rgba(255, 255, 255, 0.25);
                border: 2px solid rgba(59, 130, 246, 0.5);
                border-radius: 16px;
            }}
        """)
        super().enterEvent(event)
    
    def leaveEvent(self, event):
        self.setStyleSheet(f"""
            GlassFrame {{
                background: rgba(255, 255, 255, 0.15);
                border: 1px solid rgba(255, 255, 255, 0.2);
                border-radius: 16px;
            }}
        """)
        super().leaveEvent(event)


class LauncherScreen(QWidget):
    """Main launcher screen with module tiles"""
    
    module_selected = pyqtSignal(str)
    
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setup_ui()
    
    def setup_ui(self):
        layout = QVBoxLayout(self)
        layout.setContentsMargins(50, 40, 50, 40)
        layout.setSpacing(30)
        
        # Header with logo
        header = QHBoxLayout()
        
        logo_container = QHBoxLayout()
        logo_icon = QLabel("📊")
        logo_icon.setStyleSheet("font-size: 48px;")
        logo_container.addWidget(logo_icon)
        
        logo_text = QLabel("Yousuf Desk Tools")
        logo_text.setStyleSheet(f"""
            color: {COLORS['text_primary']};
            font-size: 36px;
            font-weight: bold;
        """)
        logo_container.addWidget(logo_text)
        logo_container.addStretch()
        
        header.addLayout(logo_container)
        header.addStretch()
        
        # Close button
        close_btn = QPushButton("✕")
        close_btn.setFixedSize(40, 40)
        close_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        close_btn.setStyleSheet(f"""
            QPushButton {{
                background: rgba(255, 255, 255, 0.1);
                color: {COLORS['text_primary']};
                border: 1px solid rgba(255, 255, 255, 0.2);
                border-radius: 20px;
                font-size: 18px;
                font-weight: bold;
            }}
            QPushButton:hover {{
                background: rgba(239, 68, 68, 0.6);
            }}
        """)
        close_btn.clicked.connect(lambda: QApplication.quit())
        header.addWidget(close_btn)
        
        layout.addLayout(header)
        
        # Subtitle
        subtitle = QLabel("Choose your productivity tool")
        subtitle.setAlignment(Qt.AlignmentFlag.AlignCenter)
        subtitle.setStyleSheet(f"""
            color: {COLORS['text_secondary']};
            font-size: 16px;
        """)
        layout.addWidget(subtitle)
        
        layout.addSpacing(20)
        
        # Module tiles
        tiles_layout = QHBoxLayout()
        tiles_layout.setSpacing(30)
        tiles_layout.addStretch()
        
        # Word Processor
        word_tile = ModuleTile(
            "Word Processor",
            "📝",
            COLORS['accent_word'],
            "Create and edit documents with rich formatting",
            self
        )
        word_tile.clicked.connect(self.module_selected.emit)
        tiles_layout.addWidget(word_tile)
        
        # Spreadsheet
        sheet_tile = ModuleTile(
            "Spreadsheet",
            "📊",
            COLORS['accent_sheet'],
            "Analyze data with powerful formulas and charts",
            self
        )
        sheet_tile.clicked.connect(self.module_selected.emit)
        tiles_layout.addWidget(sheet_tile)
        
        # Presentation
        slides_tile = ModuleTile(
            "Presentation",
            "📽️",
            COLORS['accent_slides'],
            "Create stunning presentations with slideshow",
            self
        )
        slides_tile.clicked.connect(self.module_selected.emit)
        tiles_layout.addWidget(slides_tile)
        
        tiles_layout.addStretch()
        layout.addLayout(tiles_layout)
        
        layout.addStretch()
        
        # Footer
        footer = QLabel("© 2026 Yousuf Desk Tools • Part of YouOS")
        footer.setAlignment(Qt.AlignmentFlag.AlignCenter)
        footer.setStyleSheet(f"""
            color: {COLORS['text_secondary']};
            font-size: 11px;
        """)
        layout.addWidget(footer)


class DeskToolsMain(QWidget):
    """Main application window"""
    
    def __init__(self):
        super().__init__()
        self.current_module = None
        self.setup_ui()
        self.setup_window()
    
    def setup_window(self):
        self.setWindowTitle("Yousuf Desk Tools")
        self.setMinimumSize(1200, 800)
        
        # Set dark background
        self.setStyleSheet(f"""
            QWidget {{
                background: {COLORS['bg_primary']};
                color: {COLORS['text_primary']};
            }}
        """)
    
    def setup_ui(self):
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(0)
        
        # Stacked widget for switching between launcher and modules
        self.stack = QStackedWidget()
        
        # Launcher screen
        self.launcher = LauncherScreen()
        self.launcher.module_selected.connect(self.launch_module)
        self.stack.addWidget(self.launcher)
        
        # Module containers (will be added dynamically)
        self.word_processor = None
        self.spreadsheet = None
        self.presentation = None
        
        layout.addWidget(self.stack)
    
    def launch_module(self, module_name):
        """Launch the selected module"""
        play_sound("click.wav")
        print(f"🚀 Launching {module_name}")
        
        if module_name == "Word Processor":
            if self.word_processor is None:
                self.word_processor = WordProcessor()
                self.word_processor.back_requested.connect(self.return_to_launcher)
                self.stack.addWidget(self.word_processor)
            self.stack.setCurrentWidget(self.word_processor)
            self.current_module = "Word Processor"
            
        elif module_name == "Spreadsheet":
            if self.spreadsheet is None:
                self.spreadsheet = Spreadsheet()
                self.spreadsheet.back_requested.connect(self.return_to_launcher)
                self.stack.addWidget(self.spreadsheet)
            self.stack.setCurrentWidget(self.spreadsheet)
            self.current_module = "Spreadsheet"
            
        elif module_name == "Presentation":
            if self.presentation is None:
                self.presentation = Presentation()
                self.presentation.back_requested.connect(self.return_to_launcher)
                self.stack.addWidget(self.presentation)
            self.stack.setCurrentWidget(self.presentation)
            self.current_module = "Presentation"
        
        self.setWindowTitle(f"Yousuf Desk Tools - {module_name}")
    
    def return_to_launcher(self):
        """Return to the launcher screen"""
        play_sound("click.wav")
        self.stack.setCurrentWidget(self.launcher)
        self.setWindowTitle("Yousuf Desk Tools")
        self.current_module = None


def main():
    app = QApplication(sys.argv)
    
    # Set application-wide font
    font = QFont("Segoe UI", 10)
    app.setFont(font)
    
    window = DeskToolsMain()
    window.showMaximized()
    
    sys.exit(app.exec())


if __name__ == "__main__":
    main()