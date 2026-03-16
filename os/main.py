"""
YouOS 10 - Enhanced PyQt6 Implementation with Professional Animations and Advanced Glassmorphism
main.py - Main Application with Boot, Login, Desktop, Shutdown and all features
"""

import sys
import json
import random
import os
import time
import subprocess
from pathlib import Path
from PyQt6.QtWidgets import (QApplication, QMainWindow, QWidget, QVBoxLayout, 
                              QHBoxLayout, QLabel, QPushButton, QLineEdit,
                              QGridLayout, QFrame, QGraphicsDropShadowEffect,
                              QScrollArea, QStackedWidget, QComboBox, QProgressBar,
                              QGraphicsBlurEffect, QGraphicsOpacityEffect)
from PyQt6.QtCore import (Qt, QTimer, QTime, QDate, QPropertyAnimation, 
                           QEasingCurve, pyqtSignal, QPoint, QRect, QSize,
                           QParallelAnimationGroup, QSequentialAnimationGroup, 
                           QByteArray, pyqtProperty)
from PyQt6.QtGui import (QFont, QPalette, QColor, QLinearGradient, QPainter,
                         QPen, QBrush, QPixmap, QIcon, QPainterPath, 
                         QLinearGradient, QRadialGradient)
import math
import hashlib
from PyQt6.QtCore import QThread
from profile_widget import ProfilePictureWidget


REQUIRED_PACKAGES = [
    ('PyQt6', 'PyQt6>=6.6.0'),
    ('PyQt6.QtWebEngineWidgets', 'PyQt6-WebEngine>=6.6.0'),
    ('requests', 'requests>=2.31.0'),
    ('psutil', 'psutil>=5.9.0'),
    ('PIL', 'Pillow>=10.0.0'),
    ('screen_brightness_control', 'screen-brightness-control>=0.20.0'),
]


class DependencyWorker(QThread):
    progress = pyqtSignal(int, str)   # (percent, message)
    finished = pyqtSignal()

    def run(self):
        total = len(REQUIRED_PACKAGES)
        missing = []

        # Fast pass: just try importing each package
        for i, (import_name, pip_spec) in enumerate(REQUIRED_PACKAGES):
            pkg_display = pip_spec.split('>=')[0].split('>')[0].split('==')[0]
            self.progress.emit(int(((i + 1) / total) * 100), f'Checking {pkg_display}...')
            try:
                __import__(import_name)
            except ImportError:
                missing.append((import_name, pip_spec))

        # Only run pip if something is actually missing
        if missing:
            for i, (import_name, pip_spec) in enumerate(missing):
                pkg_display = pip_spec.split('>=')[0].split('>')[0].split('==')[0]
                self.progress.emit(int((i / len(missing)) * 100), f'Installing {pkg_display}...')
                result = subprocess.run(
                    [sys.executable, '-m', 'pip', 'install', '--quiet', pip_spec],
                    capture_output=True
                )
                if result.returncode != 0:
                    self.progress.emit(int((i / len(missing)) * 100), f'Reinstalling {pkg_display}...')
                    subprocess.run(
                        [sys.executable, '-m', 'pip', 'install', '--force-reinstall', '--quiet', pip_spec],
                        capture_output=True
                    )
                self.progress.emit(int(((i + 1) / len(missing)) * 100), f'{pkg_display} ✓')

        self.finished.emit()

# Configuration
BASE_DIR = Path(__file__).parent
APP_DIR = Path.home() / '.youos'
APP_DIR.mkdir(exist_ok=True)
USERS_FILE = APP_DIR / 'users.json'
WALLPAPERS_DIR = Path('assets/wallpapers')

# Enhanced Color Palette with more depth
COLORS = {
    'bg_primary': '#0f0f1e',
    'bg_secondary': '#1a1a2e',
    'bg_tertiary': '#252538',
    'accent_primary': '#3b82f6',
    'accent_hover': '#60a5fa',
    'accent_light': '#93c5fd',
    'text_primary': '#ffffff',
    'text_secondary': '#9ca3af',
    'text_tertiary': '#6b7280',
    'border': '#374151',
    'border_light': '#4b5563',
    'success': '#10b981',
    'error': '#ef4444',
    'warning': '#f59e0b',
    'glass_base': 'rgba(255, 255, 255, 0.08)',
    'glass_highlight': 'rgba(255, 255, 255, 0.15)',
    'glass_border': 'rgba(255, 255, 255, 0.2)',
}

# Animation Timing Constants
ANIMATION = {
    'short': 150,
    'medium': 300,
    'long': 500,
    'entrance': 600,
    'stagger': 50,
}


class EasingCurves:
    """Professional easing curves for animations"""
    
    @staticmethod
    def elastic_out():
        return QEasingCurve.Type.OutElastic
    
    @staticmethod
    def smooth_stop():
        return QEasingCurve.Type.OutCubic
    
    @staticmethod
    def smooth_start():
        return QEasingCurve.Type.InCubic
    
    @staticmethod
    def natural():
        return QEasingCurve.Type.OutQuart
    
    @staticmethod
    def bounce():
        return QEasingCurve.Type.OutBounce


class AnimationManager:
    """Centralized animation manager for consistent, professional animations"""
    
    @staticmethod
    def create_scale_animation(target, scale_from, scale_to, duration=ANIMATION['medium']):
        """Create a scale animation with elastic easing"""
        anim = QPropertyAnimation(target, b"scale")
        anim.setDuration(duration)
        anim.setStartValue(scale_from)
        anim.setEndValue(scale_to)
        anim.setEasingCurve(EasingCurves.elastic_out())
        return anim
    
    @staticmethod
    def create_fade_animation(target, opacity_from, opacity_to, duration=ANIMATION['medium']):
        """Create a fade animation"""
        anim = QPropertyAnimation(target, b"windowOpacity")
        anim.setDuration(duration)
        anim.setStartValue(opacity_from)
        anim.setEndValue(opacity_to)
        anim.setEasingCurve(EasingCurves.smooth_stop())
        return anim
    
    @staticmethod
    def create_slide_animation(target, direction='up', duration=ANIMATION['entrance']):
        """Create a slide animation with entrance effect"""
        anim = QPropertyAnimation(target, b"pos")
        anim.setDuration(duration)
        anim.setEasingCurve(EasingCurves.natural())
        return anim
    
    @staticmethod
    def create_ripple_animation(widget, x, y, duration=400):
        """Create ripple effect animation for buttons"""
        anim = QPropertyAnimation(widget, b"rippleOpacity")
        anim.setDuration(duration)
        anim.setStartValue(0.8)
        anim.setEndValue(0.0)
        anim.setEasingCurve(QEasingCurve.Type.OutQuad)
        return anim


class NoiseGenerator:
    """Generate noise texture for glassmorphism"""
    
    @staticmethod
    def generate_noise_texture(size=(200, 200), opacity=0.03):
        """Generate a noise texture pixmap"""
        pixmap = QPixmap(size[0], size[1])
        pixmap.fill(Qt.GlobalColor.transparent)
        
        painter = QPainter(pixmap)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing, False)
        
        # Simple noise generation
        for i in range(size[0]):
            for j in range(size[1]):
                if random.random() < opacity:
                    painter.setPen(QColor(255, 255, 255, random.randint(1, 30)))
                    painter.drawPoint(i, j)
        
        painter.end()
        return pixmap


class EnhancedGlassWidget(QWidget):
    """Enhanced glassmorphic widget with multi-layer effects, noise texture, and dynamic lighting"""
    
    def __init__(self, parent=None, intensity=0.15, corner_radius=16):
        super().__init__(parent)
        self.intensity = intensity
        self.corner_radius = corner_radius
        self.mouse_pos = QPoint(-1, -1)
        
        # Noise texture
        self.noise_pixmap = NoiseGenerator.generate_noise_texture((400, 400), 0.04)
        self.noise_offset = 0
        
        # Lighting angle for dynamic effects
        self.lighting_angle = 45
        
        # Setup
        self.setAttribute(Qt.WidgetAttribute.WA_TranslucentBackground)
        self.setAttribute(Qt.WidgetAttribute.WA_NoSystemBackground)
        self.setup_enhanced_glass_effect()
        self.start_noise_animation()
    
    def setup_enhanced_glass_effect(self):
        """Apply enhanced glassmorphism styling with multiple layers"""
        # Ambient shadow for depth
        shadow = QGraphicsDropShadowEffect()
        shadow.setBlurRadius(40)
        shadow.setColor(QColor(0, 0, 0, 80))
        shadow.setOffset(0, 8)
        self.setGraphicsEffect(shadow)
        
        # Inner glow effect
        self.inner_glow = QGraphicsDropShadowEffect()
        self.inner_glow.setBlurRadius(20)
        self.inner_glow.setColor(QColor(59, 130, 246, 40))
        self.inner_glow.setOffset(0, 0)
        self.inner_glow.setEnabled(False)
    
    def setMouseTrackingEnabled(self, enable):
        """Enable mouse tracking for dynamic lighting"""
        super().setMouseTracking(enable)
        if enable:
            self.setMouseTracking(True)
    
    def enterEvent(self, event):
        """Handle mouse enter for hover effects"""
        # Use widget center as default position for enter event
        center_pos = QPoint(self.width() // 2, self.height() // 2)
        self.update_lighting(center_pos)
        super().enterEvent(event)
    
    def leaveEvent(self, event):
        """Handle mouse leave"""
        self.mouse_pos = QPoint(-1, -1)
        self.update()
        super().leaveEvent(event)
    
    def mouseMoveEvent(self, event):
        """Handle mouse movement for dynamic lighting"""
        self.mouse_pos = event.pos()
        self.update_lighting(event.pos())
        super().mouseMoveEvent(event)
    
    def update_lighting(self, pos):
        """Update lighting angle based on mouse position"""
        if self.width() > 0 and self.height() > 0:
            center = QPoint(self.width() // 2, self.height() // 2)
            self.lighting_angle = math.atan2(pos.y() - center.y(), pos.x() - center.x())
            self.update()
    
    def start_noise_animation(self):
        """Start subtle noise animation"""
        self.noise_timer = QTimer()
        self.noise_timer.timeout.connect(self.animate_noise)
        self.noise_timer.start(100)
    
    def animate_noise(self):
        """Animate noise texture subtly"""
        self.noise_offset = (self.noise_offset + 1) % 100
        self.update()
    
    def paintEvent(self, event):
        """Custom paint for enhanced glass effect with multi-layer rendering"""
        from PyQt6.QtCore import QRectF
        
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        painter.setRenderHint(QPainter.RenderHint.SmoothPixmapTransform)
        
        # Create rounded rectangle path
        path = QPainterPath()
        path.addRoundedRect(QRectF(self.rect()), self.corner_radius, self.corner_radius)
        
        # Clip to rounded rectangle
        painter.save()
        painter.setClipPath(path)
        
        # Layer 1: Base gradient with dynamic lighting
        gradient = QLinearGradient(0, 0, self.width(), self.height())
        
        # Calculate lighting colors based on mouse position
        light_factor = 0.0
        if self.mouse_pos.x() >= 0:
            light_factor = (self.mouse_pos.x() / self.width()) * 0.1
        
        base_alpha = int(255 * self.intensity)
        highlight_alpha = int(255 * (self.intensity + 0.05))
        
        # Dynamic gradient with subtle lighting shift
        gradient.setColorAt(0, QColor(40 + int(light_factor * 50), 40, 70, base_alpha + 20))
        gradient.setColorAt(0.5, QColor(30 + int(light_factor * 30), 30, 55, base_alpha))
        gradient.setColorAt(1, QColor(20, 20, 45, base_alpha - 10))
        
        painter.fillPath(path, gradient)
        
        # Layer 2: Subtle inner highlight
        highlight_gradient = QLinearGradient(0, 0, 0, self.height())
        highlight_gradient.setColorAt(0, QColor(255, 255, 255, 15))
        highlight_gradient.setColorAt(1, QColor(255, 255, 255, 0))
        painter.fillPath(path, highlight_gradient)
        
        # Layer 3: Noise texture overlay
        painter.save()
        painter.setOpacity(0.5)
        noise_pattern = QBrush(self.noise_pixmap)
        painter.fillPath(path, noise_pattern)
        painter.restore()
        
        painter.restore()  # Remove clip
        
        # Layer 4: Enhanced border with gradient highlight
        border_path = QPainterPath()
        border_path.addRoundedRect(QRectF(self.rect()), self.corner_radius, self.corner_radius)
        
        # Inner border path for stroke
        inner_path = QPainterPath()
        inner_path.addRoundedRect(QRectF(self.rect()).adjusted(0.5, 0.5, -0.5, -0.5), 
                                   self.corner_radius - 0.5, self.corner_radius - 0.5)
        
        # Draw border with gradient
        border_pen = QPen()
        border_pen.setWidth(1)
        
        # Create gradient border
        border_gradient = QLinearGradient(0, 0, self.width(), self.height())
        
        # Light reflection based on mouse position
        border_light = 60 + int(light_factor * 80)
        border_alpha = 30 + int(light_factor * 50)
        
        border_gradient.setColorAt(0, QColor(255, 255, 255, border_alpha))
        border_gradient.setColorAt(0.5, QColor(255, 255, 255, border_alpha // 2))
        border_gradient.setColorAt(1, QColor(255, 255, 255, border_alpha // 4))
        
        border_pen.setBrush(border_gradient)
        border_pen.setStyle(Qt.PenStyle.SolidLine)
        painter.strokePath(border_path, border_pen)
        
        # Layer 5: Subtle top highlight
        top_highlight = QPen(QColor(255, 255, 255, 40), 1)
        painter.setPen(top_highlight)
        painter.drawPath(border_path)


class GlassButton(QPushButton):
    """Enhanced glassmorphic button with ripple effect and professional animations"""
    
    def __init__(self, text="", parent=None, button_type="primary"):
        super().__init__(text, parent)
        self.button_type = button_type
        self.ripple_color = QColor(255, 255, 255, 100)
        self.ripple_pos = QPoint()
        self.ripple_radius = 0
        self.ripple_opacity = 0.0
        self.hover_animation = None
        self.press_animation = None
        
        self.setup_enhanced_style()
        self.setup_animations()
        self.setCursor(Qt.CursorShape.PointingHandCursor)
    
    def setup_enhanced_style(self):
        """Setup enhanced button styling with glass effect"""
        colors = COLORS
        
        if self.button_type == "primary":
            base_color = colors['accent_primary']
            hover_color = colors['accent_hover']
            pressed_color = "#2563eb"
        elif self.button_type == "success":
            base_color = colors['success']
            hover_color = "#34d399"
            pressed_color = "#059669"
        elif self.button_type == "danger":
            base_color = colors['error']
            hover_color = "#f87171"
            pressed_color = "#dc2626"
        else:
            base_color = colors['bg_tertiary']
            hover_color = colors['bg_tertiary']
            pressed_color = colors['bg_secondary']
        
        # Remove all transition properties from stylesheet
        self.setStyleSheet(f"""
            QPushButton {{
                background: {base_color};
                color: white;
                border: none;
                border-radius: 10px;
                font-size: 14px;
                font-weight: 600;
                padding: 12px 24px;
                min-height: 20px;
            }}
            QPushButton:hover {{
                background: {hover_color};
            }}
            QPushButton:pressed {{
                background: {pressed_color};
            }}
            QPushButton:disabled {{
                background: {colors['border']};
                color: {colors['text_tertiary']};
            }}
        """)
        
        # Add shadow effect
        shadow = QGraphicsDropShadowEffect()
        shadow.setBlurRadius(15)
        shadow.setColor(QColor(0, 0, 0, 40))
        shadow.setOffset(0, 4)
        self.setGraphicsEffect(shadow)
    
    def setup_animations(self):
        """Setup professional animations for button"""
        # Scale effect for hover
        self.scale_animation = QPropertyAnimation(self, b"scale")
        self.scale_animation.setDuration(ANIMATION['medium'])
        self.scale_animation.setEasingCurve(EasingCurves.elastic_out())
        
        # Brightness animation
        self.brightness_animation = QPropertyAnimation(self, b"brightness")
        self.brightness_animation.setDuration(ANIMATION['medium'])
        self.brightness_animation.setEasingCurve(EasingCurves.smooth_stop())
    
    def get_scale(self):
        return self._scale if hasattr(self, '_scale') else 1.0
    
    def set_scale(self, value):
        self._scale = value
        # Apply transform using geometry scaling
        if hasattr(self, 'original_geometry'):
            original = self.original_geometry
            scale_factor = value
            new_width = int(original.width() * scale_factor)
            new_height = int(original.height() * scale_factor)
            new_x = original.x() + (original.width() - new_width) // 2
            new_y = original.y() + (original.height() - new_height) // 2
            self.setGeometry(new_x, new_y, new_width, new_height)
    
    scale = pyqtProperty(float, get_scale, set_scale)
    
    def get_brightness(self):
        return self._brightness if hasattr(self, '_brightness') else 1.0
    
    def set_brightness(self, value):
        self._brightness = value
        # Apply brightness filter via style sheet adjustment
        opacity = int(value * 100)
        self.setStyleSheet(self.styleSheet().replace(
            f"opacity: {100}%;" if "opacity:" in self.styleSheet() else "",
            f"opacity: {opacity}%;"
        ))
    
    brightness = pyqtProperty(float, get_brightness, set_brightness)
    
    def enterEvent(self, event):
        """Enhanced hover animation with scale up"""
        self.scale_animation.stop()
        self.scale_animation.setDuration(ANIMATION['medium'])
        self.scale_animation.setStartValue(self.scale)
        self.scale_animation.setEndValue(1.05)
        self.scale_animation.setEasingCurve(EasingCurves.elastic_out())
        self.scale_animation.start()
        super().enterEvent(event)
    
    def leaveEvent(self, event):
        """Enhanced leave animation with scale down"""
        self.scale_animation.stop()
        self.scale_animation.setDuration(ANIMATION['short'])
        self.scale_animation.setStartValue(self.scale)
        self.scale_animation.setEndValue(1.0)
        self.scale_animation.setEasingCurve(EasingCurves.smooth_stop())
        self.scale_animation.start()
        super().leaveEvent(event)
    
    def mousePressEvent(self, event):
        """Record position for ripple effect"""
        self.ripple_pos = event.pos()
        self.ripple_radius = 0
        self.ripple_opacity = 0.6
        super().mousePressEvent(event)
    
    def mouseReleaseEvent(self, event):
        """Trigger ripple animation on release"""
        self.start_ripple_animation()
        super().mouseReleaseEvent(event)
    
    def start_ripple_animation(self):
        """Start ripple animation from click position"""
        # Simple ripple without animation to avoid errors
        self.ripple_radius = max(self.width(), self.height()) * 1.5
        self.ripple_opacity = 0.0
        self.update()
    
    def get_ripple_radius(self):
        return self._ripple_radius if hasattr(self, '_ripple_radius') else 0
    
    def set_ripple_radius(self, value):
        self._ripple_radius = value
        self.update()
    
    rippleRadius = pyqtProperty(float, get_ripple_radius, set_ripple_radius)
    
    def get_ripple_opacity(self):
        return self._ripple_opacity if hasattr(self, '_ripple_opacity') else 0.0
    
    def set_ripple_opacity(self, value):
        self._ripple_opacity = value
        self.update()
    
    rippleOpacity = pyqtProperty(float, get_ripple_opacity, set_ripple_opacity)
    
    def paintEvent(self, event):
        """Custom paint for ripple effect"""
        super().paintEvent(event)
        
        if self.ripple_opacity > 0:
            painter = QPainter(self)
            painter.setRenderHint(QPainter.RenderHint.Antialiasing)
            
            # Create ripple gradient
            ripple_gradient = QRadialGradient(self.ripple_pos.x(), self.ripple_pos.y(), self.ripple_radius)
            ripple_gradient.setColorAt(0, QColor(255, 255, 255, int(80 * self.ripple_opacity)))
            ripple_gradient.setColorAt(0.7, QColor(255, 255, 255, int(40 * self.ripple_opacity)))
            ripple_gradient.setColorAt(1, QColor(255, 255, 255, 0))
            
            painter.setBrush(ripple_gradient)
            painter.setPen(Qt.PenStyle.NoPen)
            painter.drawEllipse(self.ripple_pos, self.ripple_radius, self.ripple_radius)


class GlassLineEdit(QLineEdit):
    """Enhanced glassmorphic input field with focus animations"""
    
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setup_enhanced_style()
        self.setup_animations()
    
    def setup_enhanced_style(self):
        """Setup enhanced input field styling"""
        self.setStyleSheet(f"""
            QLineEdit {{
                background: {COLORS['bg_tertiary']};
                color: {COLORS['text_primary']};
                border: 2px solid {COLORS['border']};
                border-radius: 10px;
                padding: 12px 16px;
                font-size: 14px;
                selection-background-color: {COLORS['accent_primary']};
                selection-color: white;
            }}
            QLineEdit:focus {{
                border-color: {COLORS['accent_primary']};
            }}
            QLineEdit:disabled {{
                background: {COLORS['bg_secondary']};
                color: {COLORS['text_tertiary']};
                border-color: {COLORS['border']};
            }}
        """)
        
        # Shadow effect
        shadow = QGraphicsDropShadowEffect()
        shadow.setBlurRadius(10)
        shadow.setColor(QColor(0, 0, 0, 30))
        shadow.setOffset(0, 2)
        self.setGraphicsEffect(shadow)
    
    def setup_animations(self):
        """Setup focus animations"""
        self.glow_animation = QPropertyAnimation(self, b"glowIntensity")
        self.glow_animation.setDuration(ANIMATION['medium'])
        self.glow_animation.setEasingCurve(EasingCurves.smooth_stop())
    
    def get_glow_intensity(self):
        return self._glow_intensity if hasattr(self, '_glow_intensity') else 0.0
    
    def set_glow_intensity(self, value):
        self._glow_intensity = value
        if value > 0:
            # Apply glow effect
            glow = QGraphicsDropShadowEffect()
            glow.setBlurRadius(15 + value * 10)
            glow.setColor(QColor(59, 130, 246, int(100 * value)))
            glow.setOffset(0, 0)
            self.setGraphicsEffect(glow)
        else:
            shadow = QGraphicsDropShadowEffect()
            shadow.setBlurRadius(10)
            shadow.setColor(QColor(0, 0, 0, 30))
            shadow.setOffset(0, 2)
            self.setGraphicsEffect(shadow)
    
    glowIntensity = pyqtProperty(float, get_glow_intensity, set_glow_intensity)
    
    def focusInEvent(self, event):
        """Animate glow on focus"""
        self.glow_animation.stop()
        self.glow_animation.setStartValue(self.glowIntensity)
        self.glow_animation.setEndValue(1.0)
        self.glow_animation.setDuration(ANIMATION['medium'])
        self.glow_animation.start()
        super().focusInEvent(event)
    
    def focusOutEvent(self, event):
        """Remove glow on focus out"""
        self.glow_animation.stop()
        self.glow_animation.setStartValue(self.glowIntensity)
        self.glow_animation.setEndValue(0.0)
        self.glow_animation.setDuration(ANIMATION['short'])
        self.glow_animation.start()
        super().focusOutEvent(event)


class AnimatedStackedWidget(QStackedWidget):
    """Stacked widget with smooth transitions between pages"""
    
    def __init__(self, parent=None):
        super().__init__(parent)
        self.current_animation = None
        self.direction = 'right'  # 'left', 'right', 'up', 'down', 'fade'
    
    def setCurrentIndex(self, index):
        """Animate transition to new index"""
        if self.currentIndex() == index:
            return
        
        old_widget = self.currentWidget()
        new_widget = self.widget(index)
        
        if old_widget and new_widget:
            self.animate_transition(old_widget, new_widget, index)
        else:
            super().setCurrentIndex(index)
    
    def setCurrentWidget(self, widget):
        """Animate transition to specific widget"""
        index = self.indexOf(widget)
        if index >= 0:
            self.setCurrentIndex(index)
    
    def animate_transition(self, old_widget, new_widget, new_index):
        """Create smooth transition animation"""
        # Ensure new widget is visible but transparent
        new_widget.setWindowOpacity(0.0)
        if self.indexOf(new_widget) < 0:
            self.addWidget(new_widget)
        
        # Create fade animation
        fade_out = QPropertyAnimation(old_widget, b"windowOpacity")
        fade_out.setDuration(ANIMATION['medium'])
        fade_out.setStartValue(1.0)
        fade_out.setEndValue(0.0)
        fade_out.setEasingCurve(EasingCurves.smooth_stop())
        
        fade_in = QPropertyAnimation(new_widget, b"windowOpacity")
        fade_in.setDuration(ANIMATION['medium'])
        fade_in.setStartValue(0.0)
        fade_in.setEndValue(1.0)
        fade_in.setEasingCurve(EasingCurves.smooth_start())
        
        # Sequential animation
        self.current_animation = QSequentialAnimationGroup()
        self.current_animation.addAnimation(fade_out)
        self.current_animation.addAnimation(fade_in)
        
        self.current_animation.finished.connect(lambda: self.finish_transition(new_widget, new_index))
        self.current_animation.start()
    
    def finish_transition(self, widget, index):
        """Complete transition and remove old widget"""
        if self.currentIndex() >= 0:
            old_widget = self.currentWidget()
            if old_widget and old_widget != widget:
                self.removeWidget(old_widget)
        super().setCurrentIndex(self.indexOf(widget))


class SkeletonLoader(QWidget):
    """Professional skeleton loader with shimmer effect"""
    
    def __init__(self, parent=None, width=200, height=20, corner_radius=4):
        super().__init__(parent)
        self.width = width
        self.height = height
        self.corner_radius = corner_radius
        self.setFixedSize(width, height)
        self.setup_skeleton()
        self.start_shimmer()
    
    def setup_skeleton(self):
        """Setup skeleton loader styling"""
        self.setStyleSheet(f"""
            background: {COLORS['bg_tertiary']};
            border-radius: {self.corner_radius}px;
        """)
    
    def start_shimmer(self):
        """Start shimmer animation"""
        self.shimmer_animation = QPropertyAnimation(self, b"shimmerOffset")
        self.shimmer_animation.setDuration(1500)
        self.shimmer_animation.setStartValue(-self.width)
        self.shimmer_animation.setEndValue(self.width * 2)
        self.shimmer_animation.setLoopCount(-1)  # Infinite loop
        self.shimmer_animation.start()
    
    def get_shimmer_offset(self):
        return self._shimmer_offset if hasattr(self, '_shimmer_offset') else -self.width
    
    def set_shimmer_offset(self, value):
        self._shimmer_offset = value
        self.update()
    
    shimmerOffset = pyqtProperty(float, get_shimmer_offset, set_shimmer_offset)
    
    def paintEvent(self, event):
        """Custom paint for shimmer effect"""
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        
        # Draw base background
        painter.fillRect(self.rect(), QColor(37, 37, 56))
        
        # Draw shimmer gradient
        if hasattr(self, '_shimmer_offset'):
            gradient = QLinearGradient(self._shimmer_offset, 0, self._shimmer_offset + 200, 0)
            gradient.setColorAt(0, QColor(255, 255, 255, 0))
            gradient.setColorAt(0.5, QColor(255, 255, 255, 40))
            gradient.setColorAt(1, QColor(255, 255, 255, 0))
            
            painter.setBrush(gradient)
            painter.setPen(Qt.PenStyle.NoPen)
            painter.drawRoundedRect(self.rect(), self.corner_radius, self.corner_radius)


class ParallaxBackground(QWidget):
    """Parallax background with subtle movement based on mouse position"""
    
    def __init__(self, parent=None):
        super().__init__(parent)
        self.mouse_pos = QPoint(-1, -1)
        self.parallax_factor = 0.1
        self.gradient_offset = 0
        self.setMouseTracking(True)
        self.start_parallax_animation()
    
    def start_parallax_animation(self):
        """Start smooth parallax animation"""
        self.parallax_timer = QTimer()
        self.parallax_timer.timeout.connect(self.update_parallax)
        self.parallax_timer.start(16)  # ~60fps
    
    def update_parallax(self):
        """Update parallax offset based on mouse position"""
        if self.mouse_pos.x() >= 0:
            target_offset = (self.mouse_pos.x() - self.width() // 2) * self.parallax_factor
            self.gradient_offset += (target_offset - self.gradient_offset) * 0.1
            self.update()
    
    def enterEvent(self, event):
        self.mouse_pos = event.pos()
        super().enterEvent(event)
    
    def leaveEvent(self, event):
        self.mouse_pos = QPoint(-1, -1)
        super().leaveEvent(event)
    
    def mouseMoveEvent(self, event):
        self.mouse_pos = event.pos()
        super().mouseMoveEvent(event)
    
    def paintEvent(self, event):
        """Paint parallax background"""
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        
        # Create dynamic gradient
        gradient = QLinearGradient(0, 0, self.width(), self.height())
        
        # Subtle parallax movement in gradient stops
        offset1 = max(0, min(0.5, 0.3 + self.gradient_offset / 1000))
        offset2 = max(0.5, min(1.0, 0.7 + self.gradient_offset / 1000))
        
        gradient.setColorAt(0, QColor('#0f0f1e'))
        gradient.setColorAt(offset1, QColor('#1a1a3e'))
        gradient.setColorAt(offset2, QColor('#15153a'))
        gradient.setColorAt(1, QColor('#0f0f1e'))
        
        painter.fillRect(self.rect(), gradient)
        
        # Add subtle glow spots
        self.draw_glow_spots(painter)
    
    def draw_glow_spots(self, painter):
        """Draw subtle glow spots for visual interest"""
        spots = [
            (self.width() * 0.2, self.height() * 0.3, 200),
            (self.width() * 0.8, self.height() * 0.7, 300),
            (self.width() * 0.5, self.height() * 0.5, 250),
        ]
        
        for x, y, radius in spots:
            # Calculate parallax offset
            px = x + self.gradient_offset * 2
            py = y
            
            gradient = QRadialGradient(px, py, radius)
            gradient.setColorAt(0, QColor(59, 130, 246, 30))
            gradient.setColorAt(1, QColor(59, 130, 246, 0))
            
            painter.setBrush(gradient)
            painter.setPen(Qt.PenStyle.NoPen)
            painter.drawEllipse(px, py, radius, radius)


class PulseEffect:
    """Pulse animation mixin for widgets"""
    
    def __init__(self):
        self._pulse_scale = 1.0
        self._pulse_opacity = 1.0
        self.pulse_timer = None
    
    def start_pulse(self, scale_range=(1.0, 1.03), duration=1000):
        """Start pulsing animation"""
        self.stop_pulse()
        
        scale_anim = QPropertyAnimation(self, b"pulseScale")
        scale_anim.setDuration(duration)
        scale_anim.setStartValue(scale_range[0])
        scale_anim.setEndValue(scale_range[1])
        scale_anim.setEasingCurve(QEasingCurve.Type.InOutSine)
        scale_anim.setLoopCount(-1)
        
        opacity_anim = QPropertyAnimation(self, b"pulseOpacity")
        opacity_anim.setDuration(duration)
        opacity_anim.setStartValue(1.0)
        opacity_anim.setEndValue(0.95)
        opacity_anim.setEasingCurve(QEasingCurve.Type.InOutSine)
        opacity_anim.setLoopCount(-1)
        
        self._pulse_anim_group = QParallelAnimationGroup()
        self._pulse_anim_group.addAnimation(scale_anim)
        self._pulse_anim_group.addAnimation(opacity_anim)
        self._pulse_anim_group.start()
    
    def stop_pulse(self):
        """Stop pulsing animation"""
        if hasattr(self, '_pulse_anim_group'):
            self._pulse_anim_group.stop()
        self._pulse_scale = 1.0
        self._pulse_opacity = 1.0
        self.update()
    
    def get_pulse_scale(self):
        return self._pulse_scale
    
    def set_pulse_scale(self, value):
        self._pulse_scale = value
        self.update()
    
    pulseScale = pyqtProperty(float, get_pulse_scale, set_pulse_scale)
    
    def get_pulse_opacity(self):
        return self._pulse_opacity
    
    def set_pulse_opacity(self, value):
        self._pulse_opacity = value
    
    pulseOpacity = pyqtProperty(float, get_pulse_opacity, set_pulse_opacity)


class LoadingCircle(QWidget):
    """Enhanced Windows 11-style loading circle with improved aesthetics"""
    
    def __init__(self, parent=None, size=60):
        super().__init__(parent)
        self.setFixedSize(size, size)
        self.angle = 0
        self.dot_count = 8
        self.base_radius = size * 0.35
        self.dot_size = size * 0.1
        
        # Animation timer
        self.timer = QTimer()
        self.timer.timeout.connect(self.rotate)
        self.timer.start(50)
    
    def paintEvent(self, event):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        
        # Center point
        center = self.rect().center()
        
        # Draw dots in circle with enhanced styling
        for i in range(self.dot_count):
            angle = (self.angle + i * 360 / self.dot_count) % 360
            opacity = max(0.2, 1.0 - (i * 0.1))
            
            # Calculate position
            x = center.x() + self.base_radius * math.cos(math.radians(angle))
            y = center.y() + self.base_radius * math.sin(math.radians(angle))
            
            # Draw dot with glow effect
            # Outer glow
            glow_color = QColor(59, 130, 246, int(50 * opacity))
            painter.setBrush(glow_color)
            painter.setPen(Qt.PenStyle.NoPen)
            painter.drawEllipse(int(x - self.dot_size), int(y - self.dot_size), 
                               int(self.dot_size * 2.5), int(self.dot_size * 2.5))
            
            # Main dot
            dot_color = QColor(59, 130, 246, int(255 * opacity))
            painter.setBrush(dot_color)
            painter.drawEllipse(int(x - self.dot_size / 2), int(y - self.dot_size / 2), 
                               int(self.dot_size), int(self.dot_size))
    
    def rotate(self):
        self.angle = (self.angle + 15) % 360
        self.update()


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
                if 'profile_picture' not in user_data:
                    user_data['profile_picture'] = ''
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
            'profile_picture': '',
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


class BootScreen(QWidget):
    """Enhanced boot screen with professional animations"""
    
    boot_complete = pyqtSignal()
    
    def __init__(self, parent=None):
        super().__init__(parent)
        self.dependency_error = None
        self.setup_ui()
    
    def setup_ui(self):
        """Setup boot screen UI with enhanced styling"""
        self.layout = QVBoxLayout(self)
        self.layout.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.layout.setSpacing(30)
        
        # Enhanced loading bar with glow effect
        self.loading_bar = QWidget()
        self.loading_bar.setFixedSize(400, 6)
        self.loading_bar.setStyleSheet(f"""
            background: {COLORS['bg_tertiary']};
            border-radius: 3px;
        """)
        
        # Add glow shadow
        shadow = QGraphicsDropShadowEffect()
        shadow.setBlurRadius(10)
        shadow.setColor(QColor(59, 130, 246, 80))
        shadow.setOffset(0, 0)
        self.loading_bar.setGraphicsEffect(shadow)
        
        self.layout.addWidget(self.loading_bar, alignment=Qt.AlignmentFlag.AlignCenter)
        
        # Moving indicator with enhanced styling
        self.indicator = QWidget(self.loading_bar)
        self.indicator.setFixedSize(80, 6)
        self.indicator.setStyleSheet(f"""
            background: {COLORS['accent_primary']};
            border-radius: 3px;
        """)
        self.indicator.move(-80, 0)
        
        # Add glow to indicator
        indicator_shadow = QGraphicsDropShadowEffect()
        indicator_shadow.setBlurRadius(15)
        indicator_shadow.setColor(QColor(59, 130, 246, 150))
        indicator_shadow.setOffset(0, 0)
        self.indicator.setGraphicsEffect(indicator_shadow)
        
        # Loading text
        self.loading_text = QLabel("Starting YouOS 10...")
        self.loading_text.setStyleSheet(f"""
            color: {COLORS['text_secondary']};
            font-size: 14px;
            font-weight: 500;
        """)
        self.loading_text.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.layout.addWidget(self.loading_text)

        # Dependency progress bar (hidden until check starts)
        # Dependency progress bar — replaces loading_bar when checking/installing
        self.dep_progress = QProgressBar()
        self.dep_progress.setFixedSize(400, 6)
        self.dep_progress.setRange(0, 100)
        self.dep_progress.setValue(0)
        self.dep_progress.setTextVisible(False)
        self.dep_progress.setStyleSheet(f"""
            QProgressBar {{
                background: {COLORS['bg_tertiary']};
                border-radius: 3px;
                border: none;
            }}
            QProgressBar::chunk {{
                background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
                    stop:0 #10b981, stop:1 #34d399);
                border-radius: 3px;
            }}
        """)
        dep_shadow = QGraphicsDropShadowEffect()
        dep_shadow.setBlurRadius(10)
        dep_shadow.setColor(QColor(16, 185, 129, 120))
        dep_shadow.setOffset(0, 0)
        self.dep_progress.setGraphicsEffect(dep_shadow)
        self.dep_progress.hide()
        self.layout.addWidget(self.dep_progress, alignment=Qt.AlignmentFlag.AlignCenter)
        
        # Error display (hidden initially)
        self.error_icon = QLabel("✕")
        self.error_icon.setStyleSheet("""
            font-size: 72px;
            color: #ef4444;
        """)
        self.error_icon.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.error_icon.hide()
        self.layout.addWidget(self.error_icon)
        
        self.error_message = QLabel()
        self.error_message.setStyleSheet(f"""
            color: {COLORS['error']};
            font-size: 16px;
            font-weight: bold;
        """)
        self.error_message.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.error_message.setWordWrap(True)
        self.error_message.hide()
        self.layout.addWidget(self.error_message)
        
        self.shutdown_prompt = QLabel("Press any key to shutdown")
        self.shutdown_prompt.setStyleSheet(f"""
            color: {COLORS['text_secondary']};
            font-size: 14px;
        """)
        self.shutdown_prompt.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.shutdown_prompt.hide()
        self.layout.addWidget(self.shutdown_prompt)
        
        # Logo (hidden initially)
        self.logo_label = QLabel()
        pixmap = QPixmap("/home/yousuf-yasser-elshaer/codes/os/assets/start.png")
        if not pixmap.isNull():
            scaled_pixmap = pixmap.scaled(180, 180, Qt.AspectRatioMode.KeepAspectRatio, Qt.TransformationMode.SmoothTransformation)
            self.logo_label.setPixmap(scaled_pixmap)
        else:
            self.logo_label.setText("YouOS")
            self.logo_label.setStyleSheet(f"""
                color: {COLORS['text_primary']};
                font-size: 56px;
                font-weight: bold;
            """)
        self.logo_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.logo_label.setWindowOpacity(0.0)
        self.logo_label.hide()
        self.layout.addWidget(self.logo_label)
    
    def show_logo_with_enhanced_fade(self):
        """Show logo with enhanced fade-in and scale animation"""
        self.loading_bar.hide()
        self.loading_text.hide()
        self.logo_label.show()
        
        # Create parallel animation group
        self.anim_group = QParallelAnimationGroup()
        
        # Fade-in animation
        fade_anim = QPropertyAnimation(self.logo_label, b"windowOpacity")
        fade_anim.setDuration(ANIMATION['entrance'])
        fade_anim.setStartValue(0.0)
        fade_anim.setEndValue(1.0)
        fade_anim.setEasingCurve(EasingCurves.smooth_stop())
        
        # Scale animation - using transform instead of scale property
        scale_anim = QPropertyAnimation(self.logo_label, b"geometry")
        scale_anim.setDuration(ANIMATION['entrance'])
        original_rect = self.logo_label.geometry()
        smaller_rect = QRect(original_rect.x() + 20, original_rect.y() + 20, 
                           original_rect.width() - 40, original_rect.height() - 40)
        scale_anim.setStartValue(smaller_rect)
        scale_anim.setEndValue(original_rect)
        scale_anim.setEasingCurve(EasingCurves.elastic_out())
        
        self.anim_group.addAnimation(fade_anim)
        self.anim_group.addAnimation(scale_anim)
        
        self.anim_group.finished.connect(self.play_startup_sound)
        self.anim_group.start()
    
    def start_boot(self):
        """Start boot sequence with enhanced animations"""
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
        
        # Check dependencies after short delay
        QTimer.singleShot(800, self.check_dependencies)
    
    def update_loading_text(self):
        """Update loading text progressively"""
        if self.current_step < len(self.loading_steps):
            self.loading_text.setText(self.loading_steps[self.current_step])
            self.current_step += 1
    
    def move_indicator(self):
        """Move the loading indicator with smooth motion"""
        current_x = self.indicator.x()
        if current_x >= 400:
            current_x = -80
        else:
            current_x += 4
        self.indicator.move(current_x, 0)
    
    def check_dependencies(self):
        """Check and install all required dependencies in background"""
        # Fast synchronous check first - no thread needed if all present
        missing = []
        for import_name, pip_spec in REQUIRED_PACKAGES:
            try:
                __import__(import_name)
            except ImportError:
                missing.append((import_name, pip_spec))

        if not missing:
            # Everything is fine, boot immediately
            self._verify_and_boot()
            return

        # Something missing - show bar and fix in background
        self.loading_bar.hide()
        self.loading_text.setText('Installing missing packages...')
        self.dep_progress.setValue(0)
        self.dep_progress.show()

        self.dep_worker = DependencyWorker()
        self.dep_worker.progress.connect(self._on_dep_progress)
        self.dep_worker.finished.connect(self._on_dep_finished)
        self.dep_worker.start()

    def _on_dep_progress(self, percent, message):
        self.dep_progress.setValue(percent)
        self.loading_text.setText(message)

    def _on_dep_finished(self):
        self.dep_progress.setValue(100)
        self.loading_text.setText('All dependencies ready ✓')
        QTimer.singleShot(600, self._verify_and_boot)

    def _verify_and_boot(self):
        try:
            from profile_widget import ProfilePictureWidget
            from desktop import DesktopManager
            self.dep_progress.hide()
            self.show_logo()
        except ImportError as e:
            self.show_error(f"Missing dependency: {str(e)}")
        except Exception as e:
            self.show_error(f"System error: {str(e)}")
    
    def show_error(self, error_msg):
        """Show error screen with enhanced styling"""
        self.move_timer.stop()
        self.text_timer.stop()
        self.dep_progress.hide()
        self.loading_bar.hide()
        self.loading_text.hide()
        
        self.error_icon.show()
        self.error_message.setText(error_msg)
        self.error_message.show()
        self.shutdown_prompt.show()
        
        self.dependency_error = True
        self.setFocusPolicy(Qt.FocusPolicy.StrongFocus)
        self.setFocus()
    
    def show_logo(self):
        """Show logo with enhanced animation"""
        self.move_timer.stop()
        self.text_timer.stop()
        self.show_logo_with_enhanced_fade()
    
    def play_startup_sound(self):
        """Play startup sound after fade-in completes"""
        try:
            from utils import play_sound
            play_sound('startup.wav')
            # Wait for sound to finish then continue
            QTimer.singleShot(3000, self.boot_complete.emit)
        except:
            # No sound, continue immediately
            QTimer.singleShot(1000, self.boot_complete.emit)
    
    def keyPressEvent(self, event):
        """Handle key press for shutdown"""
        if self.dependency_error:
            QApplication.quit()


class UserSelectionWidget(EnhancedGlassWidget):
    """Enhanced user selection screen with professional animations"""
    
    user_selected = pyqtSignal(str)
    
    def __init__(self, auth_manager, parent=None):
        super().__init__(parent, intensity=0.12, corner_radius=20)
        self.auth = auth_manager
        self.setup_ui()
    
    def setup_ui(self):
        layout = QVBoxLayout(self)
        layout.setContentsMargins(50, 50, 50, 50)
        layout.setSpacing(30)
        
        # Title with enhanced styling
        title = QLabel("Select User")
        title.setStyleSheet(f"""
            color: {COLORS['text_primary']};
            font-size: 32px;
            font-weight: bold;
            letter-spacing: 1px;
        """)
        title.setAlignment(Qt.AlignmentFlag.AlignCenter)
        layout.addWidget(title)
        
        # User grid container
        users_container = QWidget()
        users_layout = QGridLayout(users_container)
        users_layout.setSpacing(25)
        
        usernames = self.auth.get_all_usernames()
        for i, username in enumerate(usernames):
            user_data = self.auth.get_user_data(username)
            
            # Enhanced user frame with glass effect
            user_frame = EnhancedGlassWidget(corner_radius=16)
            user_frame.setFixedSize(140, 170)
            user_frame.setCursor(Qt.CursorShape.PointingHandCursor)
            user_frame.setMouseTracking(True)
            
            # Add pulse effect on hover
            user_frame.setProperty("hovered", False)
            
            frame_layout = QVBoxLayout(user_frame)
            frame_layout.setContentsMargins(15, 20, 15, 15)
            frame_layout.setSpacing(12)
            frame_layout.setAlignment(Qt.AlignmentFlag.AlignCenter)
            
            # Profile picture with animation
            profile_pic = ProfilePictureWidget(
                username=username,
                profile_picture_path=user_data.get('profile_picture', ''),
                size=90
            )
            frame_layout.addWidget(profile_pic, alignment=Qt.AlignmentFlag.AlignCenter)
            
            # Username with enhanced styling
            username_label = QLabel(username)
            username_label.setStyleSheet(f"""
                color: {COLORS['text_primary']};
                font-size: 14px;
                font-weight: 600;
            """)
            username_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
            username_label.setWordWrap(True)
            frame_layout.addWidget(username_label)
            
            # Store original geometry after layout
            user_frame.original_geometry = QRect(0, 0, 140, 170)
            
            # Make clickable with ripple effect
            def click_handler(event, u=username, f=user_frame):
                self.user_selected.emit(u)
            
            user_frame.mousePressEvent = click_handler
            
            # Add hover effects
            user_frame.enterEvent = lambda event, f=user_frame: self.on_user_hover(f, True)
            user_frame.leaveEvent = lambda event, f=user_frame: self.on_user_hover(f, False)
            
            # Add to grid (3 columns)
            row = i // 3
            col = i % 3
            users_layout.addWidget(user_frame, row, col)
        
        layout.addWidget(users_container, alignment=Qt.AlignmentFlag.AlignCenter)
    

    def on_user_hover(self, widget, hovered):
        """Handle hover animation for user cards"""
        if hovered:
            # Scale up using geometry animation instead of scale property
            current_geo = widget.geometry()
            if not hasattr(widget, 'original_geometry'):
                widget.original_geometry = current_geo
            
            scale_factor = 1.03
            new_width = int(current_geo.width() * scale_factor)
            new_height = int(current_geo.height() * scale_factor)
            new_x = current_geo.x() - (new_width - current_geo.width()) // 2
            new_y = current_geo.y() - (new_height - current_geo.height()) // 2
            
            scale_anim = QPropertyAnimation(widget, b"geometry")
            scale_anim.setDuration(ANIMATION['medium'])
            scale_anim.setStartValue(current_geo)
            scale_anim.setEndValue(QRect(new_x, new_y, new_width, new_height))
            scale_anim.setEasingCurve(EasingCurves.elastic_out())
            scale_anim.start()
            
            # Add shadow
            shadow = QGraphicsDropShadowEffect()
            shadow.setBlurRadius(25)
            shadow.setColor(QColor(59, 130, 246, 80))
            shadow.setOffset(0, 4)
            widget.setGraphicsEffect(shadow)
        else:
            # Scale back to original
            if hasattr(widget, 'original_geometry'):
                scale_anim = QPropertyAnimation(widget, b"geometry")
                scale_anim.setDuration(ANIMATION['short'])
                scale_anim.setStartValue(widget.geometry())
                scale_anim.setEndValue(widget.original_geometry)
                scale_anim.setEasingCurve(EasingCurves.smooth_stop())
                scale_anim.start()
            
            # Reset shadow
            shadow = QGraphicsDropShadowEffect()
            shadow.setBlurRadius(40)
            shadow.setColor(QColor(0, 0, 0, 80))
            shadow.setOffset(0, 8)
            widget.setGraphicsEffect(shadow)
    
    def animate_user_selection(self, widget, username):
        """Animate user selection with scale and fade"""
        current_geo = widget.geometry()
        
        # Scale down animation using geometry
        scale_factor = 0.95
        new_width = int(current_geo.width() * scale_factor)
        new_height = int(current_geo.height() * scale_factor)
        new_x = current_geo.x() + (current_geo.width() - new_width) // 2
        new_y = current_geo.y() + (current_geo.height() - new_height) // 2
        smaller_geo = QRect(new_x, new_y, new_width, new_height)
        
        scale_down = QPropertyAnimation(widget, b"geometry")
        scale_down.setDuration(150)
        scale_down.setStartValue(current_geo)
        scale_down.setEndValue(smaller_geo)
        scale_down.setEasingCurve(QEasingCurve.Type.OutQuad)
        
        # Scale up animation
        scale_up = QPropertyAnimation(widget, b"geometry")
        scale_up.setDuration(200)
        scale_up.setStartValue(smaller_geo)
        scale_up.setEndValue(current_geo)
        scale_up.setEasingCurve(EasingCurves.elastic_out())
        
        # Fade out animation
        fade_out = QPropertyAnimation(widget, b"windowOpacity")
        fade_out.setDuration(200)
        fade_out.setStartValue(1.0)
        fade_out.setEndValue(0.0)
        
        # Sequential animation
        selection_group = QSequentialAnimationGroup()
        selection_group.addAnimation(scale_down)
        selection_group.addAnimation(scale_up)
        selection_group.addAnimation(fade_out)
        
        selection_group.finished.connect(lambda: self.user_selected.emit(username))
        selection_group.start()


class UserLoginWidget(EnhancedGlassWidget):
    """Enhanced individual user login form with professional animations"""
    
    login_success = pyqtSignal(str)
    back_requested = pyqtSignal()
    
    def __init__(self, auth_manager, username, parent=None):
        super().__init__(parent, intensity=0.15, corner_radius=20)
        self.auth = auth_manager
        self.username = username
        self.user_data = auth_manager.get_user_data(username)
        self.password_visible = False
        self.setup_ui()
        # Remove entrance animation that causes positioning issues
    
    def has_biometric_enrolled(self):
        """Check if user has enrolled biometric"""
        try:
            from face_recognition_module import FaceRecognitionEngine
            engine = FaceRecognitionEngine()
            return engine.has_enrolled_face(self.username)
        except:
            return False

    def attempt_biometric_login(self):
        """Attempt login using face recognition"""
        try:
            from face_recognition_module import FaceAuthenticationDialog
            
            dialog = FaceAuthenticationDialog(self.username, self)
            dialog.authentication_complete.connect(self.on_biometric_auth_complete)
            dialog.exec()
        except Exception as e:
            from PyQt6.QtWidgets import QMessageBox
            QMessageBox.critical(self, "Error", f"Biometric authentication failed: {str(e)}\n\nPlease use password login.")

    def on_biometric_auth_complete(self, success):
        """Handle biometric authentication result"""
        if success:
            self.login_success.emit(self.username)
    
    def setup_ui(self):
        # Adjust height based on whether biometric is enrolled
        has_biometric = self.has_biometric_enrolled()
        height = 420 if has_biometric else 350
        self.setFixedSize(420, height)
        
        layout = QVBoxLayout(self)
        layout.setContentsMargins(40, 40, 40, 40)
        layout.setSpacing(20 if has_biometric else 25)
        
        # User info header with enhanced layout
        header_layout = QHBoxLayout()
        
        # Profile picture with scale effect
        self.profile_pic = ProfilePictureWidget(
            username=self.username,
            profile_picture_path=self.user_data.get('profile_picture', ''),
            size=70
        )
        header_layout.addWidget(self.profile_pic)
        
        # Username with enhanced styling
        username_label = QLabel(self.username)
        username_label.setStyleSheet(f"""
            color: {COLORS['text_primary']};
            font-size: 24px;
            font-weight: bold;
            letter-spacing: 0.5px;
        """)
        username_label.setAlignment(Qt.AlignmentFlag.AlignLeft | Qt.AlignmentFlag.AlignVCenter)
        header_layout.addWidget(username_label)
        
        header_layout.addStretch()
        layout.addLayout(header_layout)
        
        # Password field with enhanced styling
        password_container = QWidget()
        password_layout = QHBoxLayout(password_container)
        password_layout.setContentsMargins(0, 0, 0, 0)
        password_layout.setSpacing(0)
        
        self.password_entry = GlassLineEdit()
        self.password_entry.setPlaceholderText("Enter password")
        self.password_entry.setEchoMode(QLineEdit.EchoMode.Password)
        self.password_entry.setFixedHeight(50)
        self.password_entry.returnPressed.connect(self.attempt_login)
        password_layout.addWidget(self.password_entry)
        
        # Enhanced eye button with icon
        self.eye_btn = QPushButton("○")
        self.eye_btn.setFixedSize(45, 45)
        self.eye_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        self.eye_btn.setStyleSheet("""
            QPushButton {
                background: transparent;
                border: none;
                font-size: 18px;
                color: #9ca3af;
            }
            QPushButton:hover {
                background: rgba(255, 255, 255, 0.1);
                color: #60a5fa;
                border-radius: 8px;
            }
        """)
        self.eye_btn.clicked.connect(self.toggle_password_visibility)
        
        # Position eye button over password field
        password_layout.addWidget(self.eye_btn)
        password_layout.setAlignment(self.eye_btn, Qt.AlignmentFlag.AlignRight | Qt.AlignmentFlag.AlignVCenter)
        
        layout.addWidget(password_container)
        
        # Enhanced login button with ripple effect
        self.login_btn = GlassButton("Sign In", button_type="primary")
        self.login_btn.setFixedHeight(50)
        self.login_btn.setStyleSheet(self.login_btn.styleSheet() + """
            QPushButton {
                font-size: 16px;
                font-weight: 600;
                letter-spacing: 0.5px;
            }
        """)
        self.login_btn.clicked.connect(self.attempt_login)
        layout.addWidget(self.login_btn)
        
        # Biometric unlock button (only show if user has enrolled face)
        if has_biometric:
            biometric_layout = QHBoxLayout()
            biometric_layout.addStretch()
            
            self.biometric_btn = QPushButton("🔓 Unlock with Face")
            self.biometric_btn.setFixedHeight(45)
            self.biometric_btn.setFixedWidth(200)
            self.biometric_btn.setCursor(Qt.CursorShape.PointingHandCursor)
            self.biometric_btn.setStyleSheet(f"""
                QPushButton {{
                    background: rgba(16, 185, 129, 0.2);
                    color: {COLORS['success']};
                    border: 2px solid {COLORS['success']};
                    border-radius: 8px;
                    font-size: 13px;
                    font-weight: bold;
                }}
                QPushButton:hover {{
                    background: rgba(16, 185, 129, 0.3);
                }}
            """)
            self.biometric_btn.clicked.connect(self.attempt_biometric_login)
            biometric_layout.addWidget(self.biometric_btn)
            biometric_layout.addStretch()
            
            layout.addLayout(biometric_layout)
        
        # Enhanced back button
        back_btn = GlassButton("← Back", button_type="glass")
        back_btn.setFixedHeight(45)
        back_btn.setStyleSheet(back_btn.styleSheet() + """
            QPushButton {
                font-size: 14px;
            }
        """)
        back_btn.clicked.connect(self.back_requested.emit)
        layout.addWidget(back_btn)
        
        # Focus password field
        self.password_entry.setFocus()
    

    def toggle_password_visibility(self):
        """Toggle password visibility with animation"""
        if self.password_visible:
            self.password_entry.setEchoMode(QLineEdit.EchoMode.Password)
            self.eye_btn.setText("○")
            self.password_visible = False
        else:
            self.password_entry.setEchoMode(QLineEdit.EchoMode.Normal)
            self.eye_btn.setText("◉")
            self.password_visible = True
    
    def attempt_login(self):
        """Attempt login with shake animation for wrong password"""
        password = self.password_entry.text()
        if not password:
            self.shake_animation()
            return
        
        if self.auth.authenticate(self.username, password):
            self.animate_login_success()
        else:
            self.shake_animation()
    
    def shake_animation(self):
        """Enhanced shake animation for wrong password"""
        self.password_entry.clear()
        self.password_entry.setStyleSheet(self.password_entry.styleSheet().replace(
            f"border: 2px solid {COLORS['border']};",
            f"border: 2px solid {COLORS['error']};"
        ))
        
        # Shake animation
        original_pos = self.pos()
        shake_anim = QPropertyAnimation(self, b"pos")
        shake_anim.setDuration(50)
        shake_anim.setLoopCount(4)
        
        shake_anim.setKeyValueAt(0, original_pos)
        shake_anim.setKeyValueAt(0.25, QPoint(original_pos.x() - 10, original_pos.y()))
        shake_anim.setKeyValueAt(0.5, QPoint(original_pos.x() + 10, original_pos.y()))
        shake_anim.setKeyValueAt(0.75, QPoint(original_pos.x() - 10, original_pos.y()))
        shake_anim.setKeyValueAt(1, original_pos)
        
        shake_anim.finished.connect(lambda: self.reset_password_field())
        shake_anim.start()
        
        self.password_entry.setFocus()
    
    def reset_password_field(self):
        """Reset password field styling"""
        self.password_entry.setStyleSheet(self.password_entry.styleSheet().replace(
            f"border: 2px solid {COLORS['error']};",
            f"border: 2px solid {COLORS['border']};"
        ))
    
    def animate_login_success(self):
        """Animate successful login"""
        # Skip animation and directly emit success
        self.login_success.emit(self.username)


class ShutdownScreen(QWidget):
    """Enhanced shutdown screen with professional animations"""
    
    shutdown_complete = pyqtSignal()
    
    def __init__(self, action="shutdown", parent=None):
        super().__init__(parent)
        self.action = action
        self.setup_ui()
    
    def setup_ui(self):
        """Setup shutdown screen UI with enhanced styling"""
        layout = QVBoxLayout(self)
        layout.setAlignment(Qt.AlignmentFlag.AlignCenter)
        layout.setSpacing(30)
        
        # Logo image with animation
        logo_label = QLabel()
        logo_pixmap = QPixmap("/home/yousuf-yasser-elshaer/codes/os/assets/start.png")
        if not logo_pixmap.isNull():
            scaled_pixmap = logo_pixmap.scaled(150, 150, Qt.AspectRatioMode.KeepAspectRatio, Qt.TransformationMode.SmoothTransformation)
            logo_label.setPixmap(scaled_pixmap)
        else:
            icons = {'shutdown': '⏻', 'restart': '🔄', 'logout': '🔓'}
            logo_label.setText(icons.get(self.action, '⏻'))
            logo_label.setStyleSheet(f"""
                color: {COLORS['text_primary']};
                font-size: 72px;
            """)
        logo_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        logo_label.setWindowOpacity(0.0)
        layout.addWidget(logo_label)
        
        # Add logo fade-in animation
        logo_anim = QPropertyAnimation(logo_label, b"windowOpacity")
        logo_anim.setDuration(ANIMATION['entrance'])
        logo_anim.setStartValue(0.0)
        logo_anim.setEndValue(1.0)
        logo_anim.setEasingCurve(EasingCurves.smooth_stop())
        QTimer.singleShot(500, logo_anim.start)
        
        # Message with enhanced styling
        messages = {'shutdown': 'Shutting down...', 'restart': 'Restarting...', 'logout': 'Logging out...'}
        self.message_label = QLabel(messages.get(self.action, 'Shutting down...'))
        self.message_label.setStyleSheet(f"""
            color: {COLORS['text_primary']};
            font-size: 28px;
            font-weight: 500;
            letter-spacing: 1px;
        """)
        self.message_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.message_label.setWindowOpacity(0.0)
        layout.addWidget(self.message_label)
        
        # Message fade-in
        msg_anim = QPropertyAnimation(self.message_label, b"windowOpacity")
        msg_anim.setDuration(ANIMATION['medium'])
        msg_anim.setStartValue(0.0)
        msg_anim.setEndValue(1.0)
        msg_anim.setEasingCurve(EasingCurves.smooth_stop())
        QTimer.singleShot(800, msg_anim.start)
        
        # Loading circle
        circle_container = QHBoxLayout()
        circle_container.addStretch()
        
        self.loading_circle = LoadingCircle(size=50)
        circle_container.addWidget(self.loading_circle)
        circle_container.addStretch()
        layout.addLayout(circle_container)
        
        # Status label with enhanced styling
        self.status_label = QLabel("Please wait...")
        self.status_label.setStyleSheet(f"""
            color: {COLORS['text_secondary']};
            font-size: 14px;
            font-weight: 500;
        """)
        self.status_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.status_label.setWindowOpacity(0.0)
        layout.addWidget(self.status_label)
        
        # Status fade-in
        status_anim = QPropertyAnimation(self.status_label, b"windowOpacity")
        status_anim.setDuration(ANIMATION['medium'])
        status_anim.setStartValue(0.0)
        status_anim.setEndValue(1.0)
        status_anim.setEasingCurve(EasingCurves.smooth_stop())
        QTimer.singleShot(1200, status_anim.start)
    
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


class LoginScreen(QWidget):
    """Enhanced login screen with parallax background and professional animations"""
    
    login_success = pyqtSignal(str)
    shutdown_requested = pyqtSignal()
    restart_requested = pyqtSignal()
    
    def __init__(self, auth_manager, parent=None):
        super().__init__(parent)
        self.auth = auth_manager
        self.current_widget = None
        self.background_pixmap = None
        self.setup_ui()
        self.start_clock()
    
    def paintEvent(self, event):
        """Custom paint event for background with parallax"""
        painter = QPainter(self)
        
        if self.background_pixmap and not self.background_pixmap.isNull():
            scaled_pixmap = self.background_pixmap.scaled(
                self.size(), 
                Qt.AspectRatioMode.KeepAspectRatioByExpanding, 
                Qt.TransformationMode.SmoothTransformation
            )
            x = (self.width() - scaled_pixmap.width()) // 2
            y = (self.height() - scaled_pixmap.height()) // 2
            painter.drawPixmap(x, y, scaled_pixmap)
        else:
            # Enhanced gradient background
            gradient = QLinearGradient(0, 0, self.width(), self.height())
            gradient.setColorAt(0, QColor('#0f0f1e'))
            gradient.setColorAt(0.4, QColor('#1a1a3e'))
            gradient.setColorAt(0.6, QColor('#15153a'))
            gradient.setColorAt(1, QColor('#0f0f1e'))
            painter.fillRect(self.rect(), gradient)
        
        # Add subtle overlay glow
        self.draw_overlay_glow(painter)
    
    def draw_overlay_glow(self, painter):
        """Draw subtle overlay glow effects"""
        # Center glow
        center_gradient = QRadialGradient(self.width() // 2, self.height() // 2, 
                                          max(self.width(), self.height()) * 0.6)
        center_gradient.setColorAt(0, QColor(59, 130, 246, 20))
        center_gradient.setColorAt(1, QColor(59, 130, 246, 0))
        
        painter.fillRect(self.rect(), center_gradient)
    
    def setup_ui(self):
        self.set_login_background()
        
        main_layout = QVBoxLayout(self)
        main_layout.setContentsMargins(0, 0, 0, 0)
        main_layout.setSpacing(0)
        
        main_layout.addStretch(2)
        
        # Center area for login widgets
        self.center_container = QWidget()
        self.center_container.setStyleSheet("background: transparent;")
        self.center_container.setFixedSize(800, 600)
        main_layout.addWidget(self.center_container, alignment=Qt.AlignmentFlag.AlignCenter)
        main_layout.addStretch(2)
        
        # Bottom bar with enhanced styling
        bottom_bar = QWidget()
        bottom_bar.setStyleSheet("background: transparent;")
        bottom_bar.setFixedHeight(160)
        bottom_layout = QHBoxLayout(bottom_bar)
        bottom_layout.setContentsMargins(40, 25, 40, 35)
        
        # Power buttons container
        power_container = QWidget()
        power_container.setFixedWidth(80)
        power_layout = QVBoxLayout(power_container)
        power_layout.setSpacing(12)
        
        # Enhanced shutdown button
        self.shutdown_btn = QPushButton("⏻")
        self.shutdown_btn.setFixedSize(55, 55)
        self.shutdown_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        self.shutdown_btn.setStyleSheet(f"""
            QPushButton {{ 
                background: rgba(0,0,0,0.6); 
                color: white; 
                border: 2px solid rgba(255,255,255,0.3);
                border-radius: 28px; 
                font-size: 22px;
                transition: all 0.3s ease;
            }} 
            QPushButton:hover {{ 
                background: {COLORS['error']}; 
                border-color: {COLORS['error']};
            }}
            QPushButton:pressed {{
                background: {COLORS['error']};
            }}
        """)
        self.shutdown_btn.clicked.connect(self.shutdown_requested.emit)
        
        # Enhanced restart button
        self.restart_btn = QPushButton("🔄")
        self.restart_btn.setFixedSize(55, 55)
        self.restart_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        self.restart_btn.setStyleSheet(f"""
            QPushButton {{ 
                background: rgba(0,0,0,0.6); 
                color: white; 
                border: 2px solid rgba(255,255,255,0.3);
                border-radius: 28px; 
                font-size: 20px;
                transition: all 0.3s ease;
            }} 
            QPushButton:hover {{ 
                background: {COLORS['warning']}; 
                border-color: {COLORS['warning']};
            }}
            QPushButton:pressed {{
                background: {COLORS['warning']};
            }}
        """)
        self.restart_btn.clicked.connect(self.restart_requested.emit)
        
        power_layout.addWidget(self.shutdown_btn)
        power_layout.addWidget(self.restart_btn)
        power_layout.addStretch()
        
        bottom_layout.addWidget(power_container)
        bottom_layout.addStretch()
        
        # Enhanced clock widget
        self.clock_widget = QWidget()
        clock_layout = QVBoxLayout(self.clock_widget)
        clock_layout.setContentsMargins(0, 0, 0, 0)
        clock_layout.setSpacing(5)
        
        self.time_label = QLabel()
        self.time_label.setStyleSheet("""
            color: white; 
            font-size: 72px; 
            font-weight: bold;
            letter-spacing: -2px;
        """)
        self.time_label.setAlignment(Qt.AlignmentFlag.AlignRight)
        clock_layout.addWidget(self.time_label)
        
        self.date_label = QLabel()
        self.date_label.setStyleSheet("""
            color: rgba(255,255,255,0.8); 
            font-size: 26px;
            font-weight: 500;
        """)
        self.date_label.setAlignment(Qt.AlignmentFlag.AlignRight)
        clock_layout.addWidget(self.date_label)
        
        bottom_layout.addWidget(self.clock_widget)
        main_layout.addWidget(bottom_bar)
        
        self.setMouseTracking(True)
    
    def set_login_background(self):
        login_bg_path = Path('/home/yousuf-yasser-elshaer/codes/os/assets/login.png')
        if login_bg_path.exists():
            self.background_pixmap = QPixmap(str(login_bg_path))
        else:
            self.background_pixmap = None
        self.update()
    
    def mousePressEvent(self, event):
        if event.button() == Qt.MouseButton.LeftButton and not self.current_widget:
            self.show_user_selection()
    
    def show_user_selection(self):
        """Show user selection screen with animation"""
        if self.current_widget:
            self.current_widget.hide()
            self.current_widget.deleteLater()
        
        self.user_selection = UserSelectionWidget(self.auth, self)
        self.user_selection.user_selected.connect(self.show_user_login)
        self.user_selection.setFixedSize(650, 450)
        
        # Center the widget properly
        self.user_selection.move(
            (self.width() - self.user_selection.width()) // 2,
            (self.height() - self.user_selection.height()) // 2
        )
        self.user_selection.show()
        self.current_widget = self.user_selection
    
    def show_user_login(self, username):
        """Show login form for specific user with animation"""
        if self.current_widget:
            self.current_widget.hide()
            self.current_widget.deleteLater()
        
        self.user_login = UserLoginWidget(self.auth, username, self)
        self.user_login.login_success.connect(self.handle_login_success)
        self.user_login.back_requested.connect(self.show_user_selection)
        
        # Center the widget properly
        self.user_login.move(
            (self.width() - self.user_login.width()) // 2,
            (self.height() - self.user_login.height()) // 2
        )
        self.user_login.show()
        self.current_widget = self.user_login
    
    def position_widget(self, widget):
        """Position widget in center"""
        widget.move((self.width() - widget.width()) // 2, (self.height() - widget.height()) // 2)
    
    def resizeEvent(self, event):
        super().resizeEvent(event)
        if self.current_widget:
            self.position_widget(self.current_widget)
    
    def handle_login_success(self, username):
        """Handle successful login with animation"""
        self.show_loading()
        QTimer.singleShot(500, lambda: self.login_success.emit(username))
    
    def show_loading(self):
        """Show loading screen with enhanced animations"""
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
        loading_circle = LoadingCircle(size=60)
        welcome_container.addWidget(loading_circle)
        
        # Welcome label with animation
        welcome_label = QLabel("Welcome")
        welcome_label.setStyleSheet("""
            color: white; 
            font-size: 64px; 
            font-weight: bold;
            letter-spacing: 2px;
        """)
        welcome_label.setWindowOpacity(0.0)
        welcome_container.addWidget(welcome_label)
        
        loading_layout.addLayout(welcome_container)
        
        # Animate welcome text
        fade_anim = QPropertyAnimation(welcome_label, b"windowOpacity")
        fade_anim.setDuration(ANIMATION['entrance'])
        fade_anim.setStartValue(0.0)
        fade_anim.setEndValue(1.0)
        fade_anim.setEasingCurve(EasingCurves.smooth_stop())
        QTimer.singleShot(200, fade_anim.start)
        
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
        if self.current_user:  # already logged in, ignore duplicate signals
            return
        self.current_user = username
        
        # Save current user to config for settings app
        try:
            config_path = BASE_DIR / 'config.json'
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
        
        # Show desktop immediately after welcome screen
        QTimer.singleShot(500, self.show_desktop_immediately)
        
        print(f"Logged in as: {username}")
    
    def show_desktop_immediately(self):
        """Show desktop immediately after welcome screen"""
        if self.desktop_widget in [self.stacked_widget.widget(i) for i in range(self.stacked_widget.count())]:
            return
        self.stacked_widget.addWidget(self.desktop_widget)
        self.stacked_widget.setCurrentWidget(self.desktop_widget)
        
        # Play logon sound
        try:
            from utils import play_sound
            play_sound('logon.wav')
        except:
            pass
    
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
    # Don't call show() - showFullScreen() is already called in __init__
    
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
