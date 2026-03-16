"""
Profile Picture Widget for YouOS
Displays circular user avatars with fallback to initials
"""

from PyQt6.QtWidgets import QLabel
from PyQt6.QtCore import Qt
from PyQt6.QtGui import QPixmap, QPainter, QPainterPath, QFont, QColor
from pathlib import Path

class ProfilePictureWidget(QLabel):
    """Circular profile picture widget with fallback to initials"""
    
    def __init__(self, username="", profile_picture_path="", size=80, parent=None):
        super().__init__(parent)
        self.username = username
        self.profile_picture_path = profile_picture_path
        self.size = size
        self.setFixedSize(size, size)
        self.update_picture()
    
    def update_picture(self):
        """Update the profile picture display"""
        pixmap = QPixmap(self.size, self.size)
        pixmap.fill(Qt.GlobalColor.transparent)
        
        painter = QPainter(pixmap)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        
        # Create circular clipping path
        path = QPainterPath()
        path.addEllipse(0, 0, self.size, self.size)
        painter.setClipPath(path)
        
        # Try to load profile picture
        if self.profile_picture_path and Path(self.profile_picture_path).exists():
            # Load and scale image
            image = QPixmap(self.profile_picture_path)
            if not image.isNull():
                scaled_image = image.scaled(
                    self.size, self.size, 
                    Qt.AspectRatioMode.KeepAspectRatioByExpanding,
                    Qt.TransformationMode.SmoothTransformation
                )
                # Center the image
                x = (self.size - scaled_image.width()) // 2
                y = (self.size - scaled_image.height()) // 2
                painter.drawPixmap(x, y, scaled_image)
            else:
                self.draw_initials(painter)
        else:
            self.draw_initials(painter)
        
        # Draw border
        painter.setClipping(False)
        painter.setPen(QColor(255, 255, 255, 100))
        painter.setBrush(Qt.BrushStyle.NoBrush)
        painter.drawEllipse(1, 1, self.size-2, self.size-2)
        
        painter.end()
        self.setPixmap(pixmap)
    
    def draw_initials(self, painter):
        """Draw user initials as fallback"""
        # Background gradient
        from PyQt6.QtGui import QLinearGradient
        gradient = QLinearGradient(0, 0, self.size, self.size)
        gradient.setColorAt(0, QColor(59, 130, 246))
        gradient.setColorAt(1, QColor(37, 99, 235))
        painter.fillRect(0, 0, self.size, self.size, gradient)
        
        # Draw initials
        initials = self.get_initials()
        painter.setPen(QColor(255, 255, 255))
        font = QFont("Arial", self.size // 3, QFont.Weight.Bold)
        painter.setFont(font)
        painter.drawText(0, 0, self.size, self.size, Qt.AlignmentFlag.AlignCenter, initials)
    
    def get_initials(self):
        """Get user initials"""
        if not self.username:
            return "?"
        
        parts = self.username.split()
        if len(parts) >= 2:
            return (parts[0][0] + parts[1][0]).upper()
        else:
            return self.username[:2].upper()
    
    def set_profile_picture(self, path):
        """Update profile picture path"""
        self.profile_picture_path = path
        self.update_picture()
    
    def set_username(self, username):
        """Update username"""
        self.username = username
        self.update_picture()