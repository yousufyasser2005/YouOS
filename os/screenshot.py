"""
YouOS Screenshot Tool
Simple screenshot utility
"""

import sys
from pathlib import Path
from PyQt6.QtWidgets import QWidget, QVBoxLayout, QPushButton, QLabel, QApplication
from PyQt6.QtCore import Qt, QTimer
from PyQt6.QtGui import QPixmap, QScreen

class ScreenshotTool(QWidget):
    def __init__(self, is_standalone=True):
        super().__init__()
        self.is_standalone = is_standalone
        self.setup_ui()
    
    def setup_ui(self):
        self.setStyleSheet("background: #1a1a2e; color: white;")
        layout = QVBoxLayout(self)
        layout.setAlignment(Qt.AlignmentFlag.AlignCenter)
        
        icon = QLabel("📸")
        icon.setStyleSheet("font-size: 72px;")
        icon.setAlignment(Qt.AlignmentFlag.AlignCenter)
        layout.addWidget(icon)
        
        title = QLabel("Screenshot Tool")
        title.setStyleSheet("font-size: 24px; font-weight: bold;")
        title.setAlignment(Qt.AlignmentFlag.AlignCenter)
        layout.addWidget(title)
        
        layout.addSpacing(20)
        
        capture_btn = QPushButton("📸 Capture Screen")
        capture_btn.setFixedHeight(50)
        capture_btn.setStyleSheet("""
            QPushButton {
                background: #3b82f6;
                border: none;
                border-radius: 8px;
                font-size: 16px;
                font-weight: bold;
            }
            QPushButton:hover {
                background: #60a5fa;
            }
        """)
        capture_btn.clicked.connect(self.take_screenshot)
        layout.addWidget(capture_btn)
        
        self.status_label = QLabel("")
        self.status_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.status_label.setStyleSheet("color: #9ca3af; font-size: 12px;")
        layout.addWidget(self.status_label)
    
    def take_screenshot(self):
        self.status_label.setText("Taking screenshot in 3 seconds...")
        QTimer.singleShot(3000, self.capture)
    
    def capture(self):
        try:
            screen = QApplication.primaryScreen()
            pixmap = screen.grabWindow(0)
            
            # Save to user files
            save_path = Path.home() / "Pictures" / f"screenshot_{int(QTimer().remainingTime())}.png"
            save_path.parent.mkdir(exist_ok=True)
            
            pixmap.save(str(save_path))
            self.status_label.setText(f"Screenshot saved to {save_path}")
        except Exception as e:
            self.status_label.setText(f"Error: {e}")

if __name__ == "__main__":
    app = QApplication(sys.argv)
    tool = ScreenshotTool()
    tool.show()
    sys.exit(app.exec())