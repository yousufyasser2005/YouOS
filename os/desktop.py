"""
YouOS 10 PyQt6 - Desktop Module with Window Management
desktop.py - Desktop manager with integrated window system
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


class ProgramWindow(GlassFrame):
    """Window container for running programs"""
    
    closed = pyqtSignal(str)  # Emits window ID
    minimized = pyqtSignal(str)
    focused = pyqtSignal(str)
    
    def __init__(self, window_id, title, icon, content_widget, parent=None):
        super().__init__(parent, opacity=0.20)
        self.window_id = window_id
        self.title = title
        self.icon = icon
        self.content_widget = content_widget
        self.is_minimized = False
        self.is_maximized = False
        self.normal_geometry = None
        
        self.setMinimumSize(400, 300)
        
        # Set better default size based on content widget
        if hasattr(content_widget, 'sizeHint') and content_widget.sizeHint().isValid():
            hint = content_widget.sizeHint()
            self.resize(max(800, hint.width() + 50), max(600, hint.height() + 90))
        else:
            self.resize(900, 650)
        
        self.setup_ui()
        self.setup_dragging()
        
    def setup_ui(self):
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(0)
        
        # Title bar
        self.title_bar = QWidget()
        self.title_bar.setFixedHeight(40)
        self.title_bar.setStyleSheet(f"""
            QWidget {{
                background: rgba(26, 26, 46, 0.95);
                border-top-left-radius: 16px;
                border-top-right-radius: 16px;
            }}
        """)
        
        title_layout = QHBoxLayout(self.title_bar)
        title_layout.setContentsMargins(15, 5, 10, 5)
        title_layout.setSpacing(10)
        
        # Icon and title
        icon_label = QLabel(self.icon)
        icon_label.setStyleSheet("font-size: 18px; background: transparent;")
        title_layout.addWidget(icon_label)
        
        self.title_label = QLabel(self.title)
        self.title_label.setStyleSheet(f"color: {COLORS['text_primary']}; font-size: 13px; font-weight: bold; background: transparent;")
        title_layout.addWidget(self.title_label)
        
        title_layout.addStretch()
        
        # Window controls
        btn_style = """
            QPushButton {{
                background: rgba(255, 255, 255, 0.1);
                border: none;
                border-radius: 6px;
                color: white;
                font-size: 14px;
                font-weight: bold;
            }}
            QPushButton:hover {{
                background: rgba(255, 255, 255, 0.2);
            }}
        """
        
        minimize_btn = QPushButton("−")
        minimize_btn.setFixedSize(30, 30)
        minimize_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        minimize_btn.setStyleSheet(btn_style)
        minimize_btn.clicked.connect(self.minimize)
        title_layout.addWidget(minimize_btn)
        
        self.maximize_btn = QPushButton("□")
        self.maximize_btn.setFixedSize(30, 30)
        self.maximize_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        self.maximize_btn.setStyleSheet(btn_style)
        self.maximize_btn.clicked.connect(self.toggle_maximize)
        title_layout.addWidget(self.maximize_btn)
        
        close_btn = QPushButton("×")
        close_btn.setFixedSize(30, 30)
        close_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        close_btn.setStyleSheet("""
            QPushButton {{
                background: rgba(239, 68, 68, 0.3);
                border: none;
                border-radius: 6px;
                color: white;
                font-size: 18px;
                font-weight: bold;
            }}
            QPushButton:hover {{
                background: rgba(239, 68, 68, 0.8);
            }}
        """)
        close_btn.clicked.connect(self.close_window)
        title_layout.addWidget(close_btn)
        
        layout.addWidget(self.title_bar)
        
        # Content area
        content_container = QWidget()
        content_container.setStyleSheet("background: transparent;")
        content_layout = QVBoxLayout(content_container)
        content_layout.setContentsMargins(5, 5, 5, 5)
        content_layout.addWidget(self.content_widget)
        
        layout.addWidget(content_container)
    
    def setup_dragging(self):
        self.dragging = False
        self.drag_position = QPoint()
        self.title_bar.mousePressEvent = self.start_drag
        self.title_bar.mouseMoveEvent = self.do_drag
        self.title_bar.mouseReleaseEvent = self.end_drag
        self.title_bar.mouseDoubleClickEvent = lambda e: self.toggle_maximize()
    
    def start_drag(self, event):
        if event.button() == Qt.MouseButton.LeftButton:
            self.dragging = True
            self.drag_position = event.globalPosition().toPoint() - self.frameGeometry().topLeft()
            self.focused.emit(self.window_id)
    
    def do_drag(self, event):
        if self.dragging and not self.is_maximized:
            self.move(event.globalPosition().toPoint() - self.drag_position)
    
    def end_drag(self, event):
        self.dragging = False
    
    def minimize(self):
        self.is_minimized = True
        self.hide()
        self.minimized.emit(self.window_id)
        play_sound("minimize.wav")
    
    def restore(self):
        self.is_minimized = False
        self.show()
        self.raise_()
        self.activateWindow()
        self.focused.emit(self.window_id)
    
    def toggle_maximize(self):
        if self.is_maximized:
            # Restore
            if self.normal_geometry:
                self.setGeometry(self.normal_geometry)
            self.is_maximized = False
            self.maximize_btn.setText("□")
        else:
            # Maximize
            self.normal_geometry = self.geometry()
            desktop_rect = self.parent().rect()
            # Leave space for taskbar (70px) and some margin
            self.setGeometry(10, 10, desktop_rect.width() - 20, desktop_rect.height() - 90)
            self.is_maximized = True
            self.maximize_btn.setText("❐")
    
    def close_window(self):
        play_sound("close.wav")
        self.closed.emit(self.window_id)
        self.deleteLater()
    
    def mousePressEvent(self, event):
        super().mousePressEvent(event)
        self.focused.emit(self.window_id)


class ProgramThread(QThread):
    """Thread for running program logic without blocking UI"""
    
    finished = pyqtSignal(object)  # Emits result
    error = pyqtSignal(str)  # Emits error message
    
    def __init__(self, target_func, *args, **kwargs):
        super().__init__()
        self.target_func = target_func
        self.args = args
        self.kwargs = kwargs
    
    def run(self):
        try:
            result = self.target_func(*self.args, **self.kwargs)
            self.finished.emit(result)
        except Exception as e:
            self.error.emit(str(e))


class WindowManager:
    """Manages all program windows"""
    
    def __init__(self, parent):
        self.parent = parent
        self.windows = {}  # window_id -> ProgramWindow
        self.window_counter = 0
        self.z_order = []  # List of window_ids in z-order
    
    def create_window(self, program_name, icon, content_widget):
        """Create a new program window"""
        self.window_counter += 1
        window_id = f"{program_name}_{self.window_counter}"
        
        window = ProgramWindow(window_id, program_name, icon, content_widget, self.parent)
        window.closed.connect(self.close_window)
        window.minimized.connect(self.on_window_minimized)
        window.focused.connect(self.bring_to_front)
        
        # Position new window with cascade effect
        offset = (len(self.windows) % 10) * 30
        window.move(100 + offset, 80 + offset)
        
        self.windows[window_id] = {
            'window': window,
            'program_name': program_name,
            'icon': icon
        }
        
        self.z_order.append(window_id)
        window.show()
        window.raise_()
        
        return window_id
    
    def close_window(self, window_id):
        """Close a window"""
        if window_id in self.windows:
            del self.windows[window_id]
            if window_id in self.z_order:
                self.z_order.remove(window_id)
            
            # Notify parent to update taskbar
            if hasattr(self.parent, 'update_taskbar'):
                self.parent.update_taskbar()
    
    def on_window_minimized(self, window_id):
        """Handle window minimization"""
        pass  # Parent will handle taskbar update
    
    def bring_to_front(self, window_id):
        """Bring window to front"""
        if window_id in self.windows and window_id in self.z_order:
            self.z_order.remove(window_id)
            self.z_order.append(window_id)
            
            # Update z-order of all windows
            for i, wid in enumerate(self.z_order):
                if wid in self.windows:
                    self.windows[wid]['window'].raise_()
    
    def get_windows_for_program(self, program_name):
        """Get all windows for a specific program"""
        return [wid for wid, data in self.windows.items() 
                if data['program_name'] == program_name]
    
    def get_running_programs(self):
        """Get set of running program names"""
        return set(data['program_name'] for data in self.windows.values())
    
    def focus_or_restore_program(self, program_name):
        """Focus or restore the first window of a program"""
        windows = self.get_windows_for_program(program_name)
        if windows:
            window_id = windows[0]
            window = self.windows[window_id]['window']
            if window.is_minimized:
                window.restore()
            else:
                if window.isVisible():
                    # If visible, minimize it
                    window.minimize()
                else:
                    window.restore()


class ErrorDialog(GlassFrame):
    """Glassmorphic error dialog"""
    
    closed = pyqtSignal()
    
    def __init__(self, title, message, parent=None):
        super().__init__(parent, opacity=0.20)
        self.setFixedSize(450, 300)
        self.setup_ui(title, message)
    
    def setup_ui(self, title, message):
        layout = QVBoxLayout(self)
        layout.setContentsMargins(30, 30, 30, 30)
        layout.setSpacing(20)
        
        icon_label = QLabel("❌")
        icon_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        icon_label.setStyleSheet("font-size: 48px;")
        layout.addWidget(icon_label)
        
        title_label = QLabel(title)
        title_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        title_label.setStyleSheet(f"color: {COLORS['text_primary']}; font-size: 18px; font-weight: bold;")
        title_label.setWordWrap(True)
        layout.addWidget(title_label)
        
        message_label = QLabel(message)
        message_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        message_label.setStyleSheet(f"color: {COLORS['text_secondary']}; font-size: 13px;")
        message_label.setWordWrap(True)
        layout.addWidget(message_label)
        
        layout.addSpacing(10)
        
        ok_btn = QPushButton("OK")
        ok_btn.setFixedHeight(40)
        ok_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        ok_btn.setStyleSheet(f"""
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
        ok_btn.clicked.connect(self.closed.emit)
        layout.addWidget(ok_btn)


class WallpaperDialog(GlassFrame):
    """Wallpaper selection dialog"""
    
    wallpaper_selected = pyqtSignal(str)
    closed = pyqtSignal()
    
    def __init__(self, parent=None):
        super().__init__(parent, opacity=0.20)
        self.setFixedSize(600, 500)
        self.setup_ui()
    
    def setup_ui(self):
        layout = QVBoxLayout(self)
        layout.setContentsMargins(30, 30, 30, 30)
        layout.setSpacing(20)
        
        title = QLabel("Choose Wallpaper")
        title.setStyleSheet(f"color: {COLORS['text_primary']}; font-size: 20px; font-weight: bold;")
        layout.addWidget(title)
        
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setStyleSheet("""
            QScrollArea { background: transparent; border: none; }
            QScrollBar:vertical { background: rgba(255, 255, 255, 0.1); width: 8px; border-radius: 4px; }
            QScrollBar::handle:vertical { background: rgba(59, 130, 246, 0.5); border-radius: 4px; }
        """)
        
        content = QWidget()
        content_layout = QGridLayout(content)
        content_layout.setSpacing(15)
        
        if WALLPAPERS_DIR.exists():
            wallpapers = list(WALLPAPERS_DIR.glob("*.jpg")) + list(WALLPAPERS_DIR.glob("*.png")) + list(WALLPAPERS_DIR.glob("*.jpeg"))
            for i, wallpaper_path in enumerate(wallpapers):
                btn = QPushButton()
                btn.setFixedSize(150, 100)
                btn.setCursor(Qt.CursorShape.PointingHandCursor)
                pixmap = QPixmap(str(wallpaper_path))
                if not pixmap.isNull():
                    pixmap = pixmap.scaled(150, 100, Qt.AspectRatioMode.KeepAspectRatioByExpanding, Qt.TransformationMode.SmoothTransformation)
                    btn.setIcon(QIcon(pixmap))
                    btn.setIconSize(pixmap.size())
                btn.setStyleSheet("QPushButton { background: rgba(255, 255, 255, 0.1); border: 2px solid rgba(255, 255, 255, 0.2); border-radius: 8px; } QPushButton:hover { border: 2px solid rgba(59, 130, 246, 0.8); }")
                btn.clicked.connect(lambda checked, p=wallpaper_path: self.select_wallpaper(str(p)))
                content_layout.addWidget(btn, i // 3, i % 3)
        else:
            no_wall_label = QLabel("No wallpapers found")
            no_wall_label.setStyleSheet(f"color: {COLORS['text_secondary']};")
            content_layout.addWidget(no_wall_label)
        
        scroll.setWidget(content)
        layout.addWidget(scroll)
        
        cancel_btn = QPushButton("Cancel")
        cancel_btn.setFixedHeight(40)
        cancel_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        cancel_btn.setStyleSheet("QPushButton { background: rgba(255, 255, 255, 0.1); color: white; border: 1px solid rgba(255, 255, 255, 0.2); border-radius: 8px; font-size: 14px; } QPushButton:hover { background: rgba(255, 255, 255, 0.2); }")
        cancel_btn.clicked.connect(self.close)
        layout.addWidget(cancel_btn)
    
    def select_wallpaper(self, path):
        self.wallpaper_selected.emit(path)
        self.close()


class BatteryIndicator(QWidget):
    """Custom battery indicator widget"""
    
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setFixedSize(80, 50)
        self.setup_ui()
        
        self.timer = QTimer(self)
        self.timer.timeout.connect(self.update_battery)
        self.timer.start(5000)
        
        self.update_battery()
    
    def setup_ui(self):
        layout = QHBoxLayout(self)
        layout.setContentsMargins(5, 5, 5, 5)
        layout.setSpacing(5)
        
        self.icon_label = QLabel()
        self.icon_label.setStyleSheet("font-size: 20px;")
        layout.addWidget(self.icon_label)
        
        self.percent_label = QLabel()
        self.percent_label.setStyleSheet("color: white; font-size: 12px; font-weight: 500;")
        layout.addWidget(self.percent_label)
    
    def update_battery(self):
        battery_info = get_battery_info()
        percent = int(battery_info.get('percent', 0))
        plugged = battery_info.get('plugged', False)
        
        if plugged:
            self.icon_label.setText("⚡")
        else:
            if percent > 80:
                self.icon_label.setText("🔋")
            elif percent > 50:
                self.icon_label.setText("🔋")
            elif percent > 20:
                self.icon_label.setText("🪫")
            else:
                self.icon_label.setText("🪫")
        
        self.percent_label.setText(f"{percent}%")


class NotificationPopup(GlassFrame):
    """Individual notification popup"""
    
    closed = pyqtSignal()
    clicked = pyqtSignal(str)  # Emits notification type when clicked
    
    def __init__(self, notification, parent=None):
        super().__init__(parent, opacity=0.25)
        self.setFixedSize(350, 100)
        self.notification = notification
        self.setup_ui()
        
        # Auto-hide timer
        self.hide_timer = QTimer()
        self.hide_timer.setSingleShot(True)
        self.hide_timer.timeout.connect(self.fade_out)
        self.hide_timer.start(5000)  # Hide after 5 seconds
        
        # Make it stay on top
        self.setWindowFlags(Qt.WindowType.FramelessWindowHint | Qt.WindowType.WindowStaysOnTopHint)
        self.setAttribute(Qt.WidgetAttribute.WA_TranslucentBackground)
    
    def setup_ui(self):
        layout = QHBoxLayout(self)
        layout.setContentsMargins(15, 10, 15, 10)
        layout.setSpacing(10)
        
        # Icon
        icon_label = QLabel(self.notification.get('icon', '📢'))
        icon_label.setStyleSheet("font-size: 24px;")
        icon_label.setFixedSize(40, 40)
        icon_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        layout.addWidget(icon_label)
        
        # Content
        content_layout = QVBoxLayout()
        content_layout.setSpacing(2)
        
        title_label = QLabel(self.notification.get('title', 'Notification'))
        title_label.setStyleSheet("color: white; font-size: 13px; font-weight: bold;")
        title_label.setWordWrap(True)
        content_layout.addWidget(title_label)
        
        message_label = QLabel(self.notification.get('message', ''))
        message_label.setStyleSheet(f"color: {COLORS['text_secondary']}; font-size: 11px;")
        message_label.setWordWrap(True)
        content_layout.addWidget(message_label)
        
        layout.addLayout(content_layout)
        
        # Close button
        close_btn = QPushButton("×")
        close_btn.setFixedSize(20, 20)
        close_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        close_btn.setStyleSheet("""
            QPushButton {
                background: rgba(255, 255, 255, 0.2);
                border: none;
                border-radius: 10px;
                color: white;
                font-size: 12px;
                font-weight: bold;
            }
            QPushButton:hover {
                background: rgba(239, 68, 68, 0.8);
            }
        """)
        close_btn.clicked.connect(self.fade_out)
        layout.addWidget(close_btn)
    
    def fade_out(self):
        """Fade out and close the notification"""
        self.hide()
        self.closed.emit()
        self.deleteLater()
    
    def mousePressEvent(self, event):
        """Handle click to dismiss or trigger action"""
        if event.button() == Qt.MouseButton.LeftButton:
            # Check if this is an update notification
            if self.notification.get('title') == 'System Updates Available':
                self.clicked.emit('update')
            self.fade_out()
        super().mousePressEvent(event)


class NotificationCenter(GlassFrame):
    """Notification center panel"""
    
    closed = pyqtSignal()
    
    def __init__(self, notifications, parent=None):
        super().__init__(parent, opacity=0.20)
        self.setFixedSize(380, 600)
        self.notifications = notifications
        self.setup_ui()
    
    def setup_ui(self):
        layout = QVBoxLayout(self)
        layout.setContentsMargins(20, 20, 20, 20)
        layout.setSpacing(15)
        
        header_layout = QHBoxLayout()
        title = QLabel("Notifications")
        title.setStyleSheet("color: white; font-size: 20px; font-weight: bold;")
        header_layout.addWidget(title)
        header_layout.addStretch()
        
        clear_btn = QPushButton("Clear All")
        clear_btn.setFixedHeight(30)
        clear_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        clear_btn.setStyleSheet("""
            QPushButton {
                background: rgba(239, 68, 68, 0.3);
                border: 1px solid rgba(239, 68, 68, 0.5);
                border-radius: 6px;
                color: white;
                font-size: 11px;
                padding: 5px 15px;
            }
            QPushButton:hover {
                background: rgba(239, 68, 68, 0.5);
            }
        """)
        clear_btn.clicked.connect(self.clear_all)
        header_layout.addWidget(clear_btn)
        layout.addLayout(header_layout)
        
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setStyleSheet("""
            QScrollArea { background: transparent; border: none; }
            QScrollBar:vertical { background: rgba(255, 255, 255, 0.1); width: 8px; border-radius: 4px; }
            QScrollBar::handle:vertical { background: rgba(59, 130, 246, 0.5); border-radius: 4px; }
        """)
        
        self.notifications_widget = QWidget()
        self.notifications_layout = QVBoxLayout(self.notifications_widget)
        self.notifications_layout.setSpacing(10)
        self.notifications_layout.setAlignment(Qt.AlignmentFlag.AlignTop)
        
        self.refresh_notifications()
        
        scroll.setWidget(self.notifications_widget)
        layout.addWidget(scroll)
    
    def refresh_notifications(self):
        for i in reversed(range(self.notifications_layout.count())):
            widget = self.notifications_layout.itemAt(i).widget()
            if widget:
                widget.deleteLater()
        
        if not self.notifications:
            no_notif = QLabel("No notifications")
            no_notif.setAlignment(Qt.AlignmentFlag.AlignCenter)
            no_notif.setStyleSheet(f"color: {COLORS['text_secondary']}; font-size: 14px; padding: 40px;")
            self.notifications_layout.addWidget(no_notif)
        else:
            for notif in reversed(self.notifications):
                notif_widget = self.create_notification_widget(notif)
                self.notifications_layout.addWidget(notif_widget)
    
    def create_notification_widget(self, notification):
        widget = QFrame()
        widget.setStyleSheet("""
            QFrame {
                background: rgba(255, 255, 255, 0.05);
                border: 1px solid rgba(255, 255, 255, 0.1);
                border-radius: 8px;
                padding: 12px;
            }
        """)
        
        layout = QVBoxLayout(widget)
        layout.setSpacing(5)
        
        title_layout = QHBoxLayout()
        icon_label = QLabel(notification.get('icon', '📢'))
        icon_label.setStyleSheet("font-size: 20px;")
        title_layout.addWidget(icon_label)
        
        title_label = QLabel(notification.get('title', 'Notification'))
        title_label.setStyleSheet("color: white; font-size: 13px; font-weight: bold;")
        title_layout.addWidget(title_label)
        title_layout.addStretch()
        
        time_label = QLabel(notification.get('time', ''))
        time_label.setStyleSheet(f"color: {COLORS['text_secondary']}; font-size: 10px;")
        title_layout.addWidget(time_label)
        layout.addLayout(title_layout)
        
        message_label = QLabel(notification.get('message', ''))
        message_label.setWordWrap(True)
        message_label.setStyleSheet(f"color: {COLORS['text_secondary']}; font-size: 12px;")
        layout.addWidget(message_label)
        
        return widget
    
    def clear_all(self):
        self.notifications.clear()
        self.refresh_notifications()
        play_sound("click.wav")


class QuickSettingsMenu(GlassFrame):
    
    def setup_ui(self):
        layout = QVBoxLayout(self)
        layout.setContentsMargins(20, 20, 20, 20)
        layout.setSpacing(15)
        
        title = QLabel("System Add-ons")
        title.setStyleSheet("color: white; font-size: 20px; font-weight: bold;")
        layout.addWidget(title)
        
        # Tab widget for different categories
        self.tabs = QTabWidget()
        self.tabs.setStyleSheet(f"""
            QTabWidget::pane {{
                border: 1px solid {COLORS['border']};
                border-radius: 8px;
                background: rgba(255, 255, 255, 0.05);
            }}
            QTabBar::tab {{
                background: rgba(255, 255, 255, 0.1);
                color: {COLORS['text_primary']};
                padding: 8px 16px;
                margin-right: 2px;
                border-top-left-radius: 6px;
                border-top-right-radius: 6px;
            }}
            QTabBar::tab:selected {{
                background: {COLORS['accent_primary']};
            }}
        """)
        
        # News Tab
        news_tab = self.create_news_tab()
        self.tabs.addTab(news_tab, "📰 News")
        
        # Sports Tab
        sports_tab = self.create_sports_tab()
        self.tabs.addTab(sports_tab, "⚽ Sports")
        
        # AI Assistant Tab
        ai_tab = self.create_ai_tab()
        self.tabs.addTab(ai_tab, "🤖 AI")
        
        layout.addWidget(self.tabs)
    
    def create_news_tab(self):
        widget = QWidget()
        layout = QVBoxLayout(widget)
        layout.setSpacing(10)
        
        # News source selector
        source_layout = QHBoxLayout()
        source_label = QLabel("Source:")
        source_label.setStyleSheet("color: white; font-size: 12px;")
        source_layout.addWidget(source_label)
        
        self.news_source = QComboBox()
        self.news_source.addItems(["Google News", "BBC News", "Reuters", "CNN"])
        self.news_source.setStyleSheet(f"""
            QComboBox {{
                background: rgba(255, 255, 255, 0.1);
                color: white;
                border: 1px solid {COLORS['border']};
                border-radius: 4px;
                padding: 4px;
            }}
        """)
        source_layout.addWidget(self.news_source)
        source_layout.addStretch()
        layout.addLayout(source_layout)
        
        # Fetch news button
        fetch_btn = QPushButton("📰 Fetch Latest News")
        fetch_btn.setFixedHeight(35)
        fetch_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        fetch_btn.setStyleSheet(f"""
            QPushButton {{
                background: {COLORS['accent_primary']};
                color: white;
                border: none;
                border-radius: 6px;
                font-size: 12px;
                font-weight: bold;
            }}
            QPushButton:hover {{
                background: {COLORS['accent_hover']};
            }}
        """)
        fetch_btn.clicked.connect(self.fetch_news)
        layout.addWidget(fetch_btn)
        
        # News display area
        self.news_display = QTextEdit()
        self.news_display.setReadOnly(True)
        self.news_display.setStyleSheet(f"""
            QTextEdit {{
                background: rgba(255, 255, 255, 0.05);
                color: {COLORS['text_primary']};
                border: 1px solid {COLORS['border']};
                border-radius: 6px;
                padding: 10px;
                font-size: 11px;
            }}
        """)
        self.news_display.setPlainText("Click 'Fetch Latest News' to load news articles...")
        layout.addWidget(self.news_display)
        
        return widget
    
    def create_sports_tab(self):
        widget = QWidget()
        layout = QVBoxLayout(widget)
        layout.setSpacing(10)
        
        # Sports selector
        sport_layout = QHBoxLayout()
        sport_label = QLabel("Sport:")
        sport_label.setStyleSheet("color: white; font-size: 12px;")
        sport_layout.addWidget(sport_label)
        
        self.sport_type = QComboBox()
        self.sport_type.addItems(["Football", "Basketball", "Soccer", "Tennis", "Baseball"])
        self.sport_type.setStyleSheet(f"""
            QComboBox {{
                background: rgba(255, 255, 255, 0.1);
                color: white;
                border: 1px solid {COLORS['border']};
                border-radius: 4px;
                padding: 4px;
            }}
        """)
        sport_layout.addWidget(self.sport_type)
        sport_layout.addStretch()
        layout.addLayout(sport_layout)
        
        # Team following section
        follow_label = QLabel("Follow Teams:")
        follow_label.setStyleSheet("color: white; font-size: 12px; font-weight: bold;")
        layout.addWidget(follow_label)
        
        # Team input with search
        team_input_layout = QHBoxLayout()
        self.team_input = QLineEdit()
        self.team_input.setPlaceholderText("Search for teams...")
        self.team_input.setStyleSheet(f"""
            QLineEdit {{
                background: rgba(255, 255, 255, 0.1);
                color: white;
                border: 1px solid {COLORS['border']};
                border-radius: 4px;
                padding: 4px;
                font-size: 11px;
            }}
        """)
        self.team_input.textChanged.connect(self.search_teams)
        team_input_layout.addWidget(self.team_input)
        layout.addLayout(team_input_layout)
        
        # Team search results
        self.team_search_results = QListWidget()
        self.team_search_results.setMaximumHeight(120)
        self.team_search_results.setStyleSheet(f"""
            QListWidget {{
                background: rgba(255, 255, 255, 0.05);
                color: white;
                border: 1px solid {COLORS['border']};
                border-radius: 4px;
                font-size: 10px;
            }}
            QListWidget::item {{
                padding: 5px;
                border-bottom: 1px solid rgba(255, 255, 255, 0.1);
            }}
            QListWidget::item:hover {{
                background: rgba(59, 130, 246, 0.3);
            }}
        """)
        self.team_search_results.itemClicked.connect(self.select_team_from_search)
        self.team_search_results.hide()  # Initially hidden
        layout.addWidget(self.team_search_results)
        
        # Followed teams list
        self.followed_teams_list = QListWidget()
        self.followed_teams_list.setMaximumHeight(80)
        self.followed_teams_list.setStyleSheet(f"""
            QListWidget {{
                background: rgba(255, 255, 255, 0.05);
                color: white;
                border: 1px solid {COLORS['border']};
                border-radius: 4px;
                font-size: 10px;
            }}
        """)
        layout.addWidget(self.followed_teams_list)
        
        # Fetch scores button
        scores_btn = QPushButton("⚽ Get Live Scores")
        scores_btn.setFixedHeight(35)
        scores_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        scores_btn.setStyleSheet(f"""
            QPushButton {{
                background: {COLORS['accent_primary']};
                color: white;
                border: none;
                border-radius: 6px;
                font-size: 12px;
                font-weight: bold;
            }}
            QPushButton:hover {{
                background: {COLORS['accent_hover']};
            }}
        """)
        scores_btn.clicked.connect(self.fetch_scores)
        layout.addWidget(scores_btn)
        
        # Fixtures button
        fixtures_btn = QPushButton("📅 Upcoming Matches")
        fixtures_btn.setFixedHeight(35)
        fixtures_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        fixtures_btn.setStyleSheet(f"""
            QPushButton {{
                background: {COLORS['warning']};
                color: white;
                border: none;
                border-radius: 6px;
                font-size: 12px;
                font-weight: bold;
            }}
            QPushButton:hover {{
                background: #d97706;
            }}
        """)
        fixtures_btn.clicked.connect(self.fetch_fixtures)
        layout.addWidget(fixtures_btn)
        
        # Scores display area
        self.scores_display = QTextEdit()
        self.scores_display.setReadOnly(True)
        self.scores_display.setStyleSheet(f"""
            QTextEdit {{
                background: rgba(255, 255, 255, 0.05);
                color: {COLORS['text_primary']};
                border: 1px solid {COLORS['border']};
                border-radius: 6px;
                padding: 10px;
                font-size: 11px;
            }}
        """)
        self.scores_display.setPlainText("Click 'Get Live Scores' to load sports scores...")
        layout.addWidget(self.scores_display)
        
        return widget
    
    def create_ai_tab(self):
        widget = QWidget()
        layout = QVBoxLayout(widget)
        layout.setSpacing(10)
        
        # AI service selector
        ai_layout = QHBoxLayout()
        ai_label = QLabel("AI Service:")
        ai_label.setStyleSheet("color: white; font-size: 12px;")
        ai_layout.addWidget(ai_label)
        
        self.ai_service_combo = QComboBox()
        self.ai_service_combo.addItems(["Amazon Q", "Claude", "Gemini", "ChatGPT"])
        self.ai_service_combo.setStyleSheet(f"""
            QComboBox {{
                background: rgba(255, 255, 255, 0.1);
                color: white;
                border: 1px solid {COLORS['border']};
                border-radius: 4px;
                padding: 4px;
            }}
        """)
        ai_layout.addWidget(self.ai_service_combo)
        ai_layout.addStretch()
        layout.addLayout(ai_layout)
        
        # Chat history
        self.chat_history = QTextEdit()
        self.chat_history.setReadOnly(True)
        self.chat_history.setStyleSheet(f"""
            QTextEdit {{
                background: rgba(255, 255, 255, 0.05);
                color: {COLORS['text_primary']};
                border: 1px solid {COLORS['border']};
                border-radius: 6px;
                padding: 10px;
                font-size: 11px;
            }}
        """)
        self.chat_history.setPlainText("AI Chat - Start a conversation...")
        layout.addWidget(self.chat_history)
        
        # Query input
        self.ai_query = QLineEdit()
        self.ai_query.setPlaceholderText("Ask AI a question...")
        self.ai_query.setStyleSheet(f"""
            QLineEdit {{
                background: rgba(255, 255, 255, 0.1);
                color: white;
                border: 1px solid {COLORS['border']};
                border-radius: 6px;
                padding: 8px;
                font-size: 12px;
            }}
        """)
        self.ai_query.returnPressed.connect(self.ask_ai)
        layout.addWidget(self.ai_query)
        
        # Ask AI button
        ask_btn = QPushButton("🤖 Ask AI")
        ask_btn.setFixedHeight(35)
        ask_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        ask_btn.setStyleSheet(f"""
            QPushButton {{
                background: {COLORS['accent_primary']};
                color: white;
                border: none;
                border-radius: 6px;
                font-size: 12px;
                font-weight: bold;
            }}
            QPushButton:hover {{
                background: {COLORS['accent_hover']};
            }}
        """)
        ask_btn.clicked.connect(self.ask_ai)
        layout.addWidget(ask_btn)
        
        return widget
    
    def fetch_news(self):
        """Fetch real news from API"""
        self.news_display.setPlainText("Fetching news...")
        
        try:
            source = self.news_source.currentText().lower().replace(' ', '-')
            articles = self.news_service.get_news(source)
            
            if articles:
                news_text = f"Latest from {self.news_source.currentText()}:\n\n"
                for i, article in enumerate(articles, 1):
                    title = article.get('title', 'No title')
                    description = article.get('description', 'No description')
                    news_text += f"{i}. {title}\n{description}\n\n"
                
                self.news_display.setPlainText(news_text)
            else:
                self.news_display.setPlainText("No news articles found.")
                
        except Exception as e:
            self.news_display.setPlainText(f"Error fetching news: {str(e)}")
    
    def search_teams(self, query):
        """Search for teams as user types"""
        if len(query) < 2:
            self.team_search_results.hide()
            return
        
        try:
            teams = self.sports_service.search_teams(query)
            self.team_search_results.clear()
            
            if teams:
                for team in teams:
                    # Create item with logo/flag and team name
                    logo = team.get('logo', '⚽')
                    name = team.get('name', '')
                    country = team.get('country', '')
                    is_national = team.get('national', False)
                    
                    if is_national:
                        display_text = f"🏴 {name} (National Team)"
                    else:
                        display_text = f"🏆 {name} ({country})"
                    
                    # Try to download team logo
                    logo_url = team.get('logo', '')
                    if logo_url:
                        try:
                            logo_response = requests.get(logo_url, timeout=5)
                            if logo_response.status_code == 200:
                                # For now, use emoji - could implement actual image loading
                                display_text = f"🏆 {name} ({country})"
                        except:
                            pass
                    
                    item = QListWidgetItem(display_text)
                    item.setData(Qt.ItemDataRole.UserRole, team)  # Store team data
                    self.team_search_results.addItem(item)
                
                self.team_search_results.show()
            else:
                self.team_search_results.hide()
        except Exception as e:
            # Show error message in search results
            self.team_search_results.clear()
            error_item = QListWidgetItem(f"❌ {str(e)}")
            error_item.setData(Qt.ItemDataRole.UserRole, None)
            self.team_search_results.addItem(error_item)
            self.team_search_results.show()
    
    def select_team_from_search(self, item):
        """Select team from search results"""
        team_data = item.data(Qt.ItemDataRole.UserRole)
        if not team_data:  # Error item or invalid data
            return
            
        team_name = team_data.get('name', '')
        
        # Check if team already exists
        existing_teams = [self.followed_teams_list.item(i).text().split(' ', 1)[1] if self.followed_teams_list.item(i) else ''
                        for i in range(self.followed_teams_list.count())]
        
        if team_name not in existing_teams:
            self.sports_service.add_followed_team(team_name)
            
            # Add to followed teams list with logo
            logo = '🏴' if team_data.get('national', False) else '🏆'
            self.add_team_to_list(team_name, logo)
            
            print(f"Added team: {team_name}")
        else:
            print(f"Team {team_name} already followed")
        
        # Clear search
        self.team_input.clear()
        self.team_search_results.hide()
    
    def add_team_to_list(self, team_name, logo):
        """Add team to followed teams list with logo"""
        item_widget = QWidget()
        item_layout = QHBoxLayout(item_widget)
        item_layout.setContentsMargins(2, 2, 2, 2)
        
        team_label = QLabel(f"{logo} {team_name}")
        team_label.setStyleSheet("color: white; font-size: 10px;")
        item_layout.addWidget(team_label)
        
        remove_btn = QPushButton("×")
        remove_btn.setFixedSize(15, 15)
        remove_btn.setStyleSheet(f"""
            QPushButton {{
                background: {COLORS['error']};
                color: white;
                border: none;
                border-radius: 7px;
                font-size: 8px;
            }}
        """)
        remove_btn.clicked.connect(lambda: self.remove_team(team_name, item))
        item_layout.addWidget(remove_btn)
        
        item = QListWidgetItem()
        item.setText(f"{logo} {team_name}")  # Store for duplicate checking
        item.setSizeHint(item_widget.sizeHint())
        self.followed_teams_list.addItem(item)
        self.followed_teams_list.setItemWidget(item, item_widget)
    
    def remove_team(self, team_name, item):
        """Remove team from follow list"""
        self.sports_service.remove_followed_team(team_name)
        row = self.followed_teams_list.row(item)
        self.followed_teams_list.takeItem(row)
        print(f"Removed team: {team_name}")
    
    def fetch_scores(self):
        """Fetch real sports scores"""
        self.scores_display.setPlainText("Fetching scores...")
        
        try:
            sport = self.sport_type.currentText().lower()
            scores = self.sports_service.get_live_scores(sport)
            
            if scores:
                scores_text = f"Live {self.sport_type.currentText()} Scores:\n\n"
                for match in scores:
                    home = match['home_team']
                    away = match['away_team']
                    home_score = match['home_score']
                    away_score = match['away_score']
                    status = match['status']
                    
                    if status == 'LIVE':
                        minute = match.get('minute', '')
                        scores_text += f"🔴 {home} {home_score} - {away_score} {away} ({minute}')\n\n"
                    else:
                        scores_text += f"{home} {home_score} - {away_score} {away} ({status})\n\n"
                
                self.scores_display.setPlainText(scores_text)
            else:
                self.scores_display.setPlainText("No scores available.")
                
        except Exception as e:
            self.scores_display.setPlainText(f"Error fetching scores: {str(e)}")
    
    def ask_ai(self):
        """Send message to AI service"""
        query = self.ai_query.text().strip()
        if not query:
            return
        
        service = self.ai_service_combo.currentText()
        
        # Add user message to chat
        current_text = self.chat_history.toPlainText()
        if current_text == "AI Chat - Start a conversation...":
            current_text = ""
        
        current_text += f"\nYou: {query}\n"
        self.chat_history.setPlainText(current_text)
        
        # Show thinking
        self.chat_history.append(f"{service}: Thinking...")
        
        try:
            response = self.ai_service.chat(service, query)
            
            # Replace thinking with actual response
            current_text = self.chat_history.toPlainText()
            current_text = current_text.replace(f"{service}: Thinking...", response)
            self.chat_history.setPlainText(current_text)
            
            # Scroll to bottom
            scrollbar = self.chat_history.verticalScrollBar()
            scrollbar.setValue(scrollbar.maximum())
            
        except Exception as e:
            current_text = self.chat_history.toPlainText()
            current_text = current_text.replace(f"{service}: Thinking...", f"{service}: Error - {str(e)}")
            self.chat_history.setPlainText(current_text)
        
    def fetch_fixtures(self):
        """Fetch upcoming fixtures for followed teams"""
        self.scores_display.setPlainText("Fetching upcoming matches...")
        
        try:
            if not self.sports_service.followed_teams:
                self.scores_display.setPlainText("No teams followed. Add teams to see their upcoming matches.")
                return
            
            fixtures_text = "Upcoming Matches for Followed Teams:\n\n"
            
            for team_name in self.sports_service.followed_teams:
                fixtures = self.sports_service.get_team_fixtures(team_name)
                
                if fixtures:
                    fixtures_text += f"\n{team_name}:\n"
                    for fixture in fixtures:
                        home = fixture['home_team']
                        away = fixture['away_team']
                        date = fixture['date']
                        time = fixture['time']
                        comp = fixture['competition']
                        
                        fixtures_text += f"  📅 {home} vs {away}\n"
                        fixtures_text += f"     {date} {time} - {comp}\n\n"
                else:
                    fixtures_text += f"\n{team_name}: No upcoming matches found\n\n"
            
            self.scores_display.setPlainText(fixtures_text)
            
        except Exception as e:
            self.scores_display.setPlainText(f"Error fetching fixtures: {str(e)}")


class QuickSettingsMenu(GlassFrame):
    """Quick settings menu"""
    
    def __init__(self, parent=None):
        super().__init__(parent, opacity=0.20)
        self.setFixedSize(350, 420)
        self.last_ding_time = 0
        self.setup_ui()
    
    def setup_ui(self):
        layout = QVBoxLayout(self)
        layout.setContentsMargins(20, 20, 20, 20)
        layout.setSpacing(20)
        
        title = QLabel("Quick Settings")
        title.setStyleSheet("color: white; font-size: 18px; font-weight: bold;")
        layout.addWidget(title)
        
        battery_info = get_battery_info()
        battery_layout = QHBoxLayout()
        battery_icon = QLabel("⚡" if battery_info.get('plugged') else "🔋")
        battery_icon.setStyleSheet("font-size: 32px;")
        battery_layout.addWidget(battery_icon)
        
        battery_text_layout = QVBoxLayout()
        battery_percent = QLabel(f"{int(battery_info.get('percent', 0))}%")
        battery_percent.setStyleSheet("color: white; font-size: 20px; font-weight: bold;")
        battery_text_layout.addWidget(battery_percent)
        
        battery_status = QLabel("Charging" if battery_info.get('plugged') else "On Battery")
        battery_status.setStyleSheet("color: #9ca3af; font-size: 12px;")
        battery_text_layout.addWidget(battery_status)
        battery_layout.addLayout(battery_text_layout)
        battery_layout.addStretch()
        layout.addLayout(battery_layout)
        
        divider = QFrame()
        divider.setFrameShape(QFrame.Shape.HLine)
        divider.setStyleSheet("background: rgba(255, 255, 255, 0.2); max-height: 1px;")
        layout.addWidget(divider)
        
        vol_label = QLabel("🔊 Volume")
        vol_label.setStyleSheet("color: white; font-size: 14px; font-weight: bold;")
        layout.addWidget(vol_label)
        
        vol_container = QHBoxLayout()
        self.vol_slider = QSlider(Qt.Orientation.Horizontal)
        self.vol_slider.setRange(0, 100)
        self.vol_slider.setValue(int(get_volume()))
        self.vol_slider.setStyleSheet("QSlider::groove:horizontal { background: rgba(255, 255, 255, 0.2); height: 8px; border-radius: 4px; } QSlider::handle:horizontal { background: rgba(59, 130, 246, 0.9); width: 20px; margin: -6px 0; border-radius: 10px; }")
        self.vol_slider.valueChanged.connect(self.on_volume_change)
        self.vol_slider.sliderPressed.connect(lambda: play_sound('ding.wav'))
        vol_container.addWidget(self.vol_slider)
        self.vol_label = QLabel(f"{self.vol_slider.value()}%")
        self.vol_label.setStyleSheet("color: white; font-size: 12px; min-width: 40px;")
        vol_container.addWidget(self.vol_label)
        layout.addLayout(vol_container)
        
        layout.addSpacing(10)
        
        bright_label = QLabel("🔆 Brightness")
        bright_label.setStyleSheet("color: white; font-size: 14px; font-weight: bold;")
        layout.addWidget(bright_label)
        
        bright_container = QHBoxLayout()
        self.bright_slider = QSlider(Qt.Orientation.Horizontal)
        self.bright_slider.setRange(0, 100)
        self.bright_slider.setValue(get_brightness())
        self.bright_slider.setStyleSheet("QSlider::groove:horizontal { background: rgba(255, 255, 255, 0.2); height: 8px; border-radius: 4px; } QSlider::handle:horizontal { background: rgba(59, 130, 246, 0.9); width: 20px; margin: -6px 0; border-radius: 10px; }")
        self.bright_slider.valueChanged.connect(self.on_brightness_change)
        bright_container.addWidget(self.bright_slider)
        self.bright_label = QLabel(f"{self.bright_slider.value()}%")
        self.bright_label.setStyleSheet("color: white; font-size: 12px; min-width: 40px;")
        bright_container.addWidget(self.bright_label)
        layout.addLayout(bright_container)
        
        divider2 = QFrame()
        divider2.setFrameShape(QFrame.Shape.HLine)
        divider2.setStyleSheet("background: rgba(255, 255, 255, 0.2); max-height: 1px;")
        layout.addWidget(divider2)
        
        actions_grid = QGridLayout()
        actions_grid.setSpacing(10)
        for i, (icon, name) in enumerate([("📡", "WiFi"), ("🔵", "Bluetooth"), ("✈️", "Airplane"), ("🌙", "Night Light")]):
            btn = QPushButton(f"{icon}\n{name}")
            btn.setFixedSize(75, 75)
            btn.setCursor(Qt.CursorShape.PointingHandCursor)
            btn.setStyleSheet("QPushButton { background: rgba(255, 255, 255, 0.1); border: 1px solid rgba(255, 255, 255, 0.2); border-radius: 12px; color: white; font-size: 10px; } QPushButton:hover { background: rgba(59, 130, 246, 0.3); }")
            actions_grid.addWidget(btn, i // 4, i % 4)
        layout.addLayout(actions_grid)
        layout.addStretch()
    
    def on_volume_change(self, value):
        import time
        current_time = time.time()
        set_volume(value)
        self.vol_label.setText(f"{value}%")
        if current_time - self.last_ding_time > 0.3:
            play_sound('ding.wav')
            self.last_ding_time = current_time
    
    def on_brightness_change(self, value):
        set_brightness(value)
        self.bright_label.setText(f"{value}%")


class WidgetPanel(GlassFrame):
    """Widget panel with all widgets"""
    
    def __init__(self, parent=None):
        super().__init__(parent, opacity=0.20)
        self.setFixedWidth(380)
        self.setup_ui()
    
    def setup_ui(self):
        layout = QVBoxLayout(self)
        layout.setContentsMargins(15, 15, 15, 15)
        layout.setSpacing(10)
        
        clock = AnalogClock()
        layout.addWidget(clock, alignment=Qt.AlignmentFlag.AlignCenter)
        
        self.time_label = QLabel()
        self.time_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.time_label.setStyleSheet("color: white; font-size: 24px; font-weight: bold;")
        layout.addWidget(self.time_label)
        
        self.date_label = QLabel()
        self.date_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.date_label.setStyleSheet("color: #9ca3af; font-size: 14px;")
        layout.addWidget(self.date_label)
        
        layout.addWidget(SystemMonitorWidget())
        layout.addWidget(WeatherWidget())
        layout.addWidget(CalendarWidget())
        layout.addStretch()
        
        self.update_time()
        timer = QTimer(self)
        timer.timeout.connect(self.update_time)
        timer.start(1000)
    
    def update_time(self):
        self.time_label.setText(QTime.currentTime().toString('hh:mm:ss AP'))
        self.date_label.setText(QDate.currentDate().toString('dddd, MMMM d, yyyy'))


class DesktopIcon(QWidget):
    """Desktop icon with double-click, drag and context menu"""
    
    clicked = pyqtSignal(str)
    pin_toggled = pyqtSignal(str, bool)
    remove_requested = pyqtSignal(str)
    position_changed = pyqtSignal(str, int, int)
    
    def __init__(self, name, icon, desktop_manager, parent=None):
        super().__init__(parent)
        self.name = name
        self.icon = icon
        self.desktop_manager = desktop_manager
        self.last_click = 0
        self.dragging = False
        self.drag_start_position = QPoint()
        self.setup_ui()
    
    def setup_ui(self):
        layout = QVBoxLayout(self)
        layout.setAlignment(Qt.AlignmentFlag.AlignCenter)
        layout.setSpacing(5)
        
        self.icon_btn = QPushButton(self.icon)
        self.icon_btn.setFixedSize(64, 64)
        self.icon_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        self.icon_btn.setStyleSheet("QPushButton { background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 rgba(59, 130, 246, 0.3), stop:1 rgba(147, 51, 234, 0.3)); border: 1px solid rgba(255, 255, 255, 0.2); border-radius: 16px; font-size: 32px; } QPushButton:hover { background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 rgba(59, 130, 246, 0.5), stop:1 rgba(147, 51, 234, 0.5)); }")
        self.icon_btn.clicked.connect(self.on_click)
        layout.addWidget(self.icon_btn)
        
        label = QLabel(self.name)
        label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        label.setWordWrap(True)
        label.setFixedWidth(80)
        label.setStyleSheet("color: white; font-size: 11px; background: rgba(0, 0, 0, 0.5); padding: 4px; border-radius: 4px;")
        layout.addWidget(label)
    
    def mousePressEvent(self, event):
        if event.button() == Qt.MouseButton.LeftButton:
            self.drag_start_position = event.position().toPoint()
        elif event.button() == Qt.MouseButton.RightButton:
            self.show_context_menu(event.globalPosition().toPoint())
        super().mousePressEvent(event)
    
    def mouseMoveEvent(self, event):
        if not (event.buttons() & Qt.MouseButton.LeftButton):
            return
        
        if ((event.position().toPoint() - self.drag_start_position).manhattanLength() < 
            QApplication.startDragDistance()):
            return
        
        if not self.dragging:
            self.dragging = True
            self.setCursor(Qt.CursorShape.ClosedHandCursor)
            self.raise_()
        
        # Move the icon with absolute positioning
        global_pos = event.globalPosition().toPoint()
        parent_pos = self.parent().mapFromGlobal(global_pos - self.drag_start_position)
        
        # Keep within parent bounds
        parent_rect = self.parent().rect()
        x = max(0, min(parent_pos.x(), parent_rect.width() - self.width()))
        y = max(0, min(parent_pos.y(), parent_rect.height() - self.height()))
        
        self.move(x, y)
    
    def mouseReleaseEvent(self, event):
        if self.dragging:
            self.dragging = False
            self.setCursor(Qt.CursorShape.PointingHandCursor)
            # Save position
            pos = self.pos()
            self.position_changed.emit(self.name, pos.x(), pos.y())
        super().mouseReleaseEvent(event)
    
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
        
        menu.addSeparator()
        
        remove_action = QAction("Remove Desktop Icon", self)
        remove_action.triggered.connect(lambda: self.remove_requested.emit(self.name))
        menu.addAction(remove_action)
        
        menu.exec(pos)
    
    def on_click(self):
        import time
        current = time.time() * 1000
        if current - self.last_click < 500:
            self.clicked.emit(self.name)
        self.last_click = current


class AppPreview(GlassFrame):
    """Small popup that shows a preview of the program on hover"""
    
    def __init__(self, title, icon_text, parent=None):
        super().__init__(parent, opacity=0.4)
        self.setFixedSize(180, 120)
        layout = QVBoxLayout(self)
        layout.setContentsMargins(10, 10, 10, 10)
        
        title_label = QLabel(title)
        title_label.setStyleSheet("color: white; font-size: 11px; font-weight: bold;")
        title_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        layout.addWidget(title_label)
        
        content = QFrame()
        content.setStyleSheet("background: rgba(255, 255, 255, 0.05); border-radius: 4px;")
        content_layout = QVBoxLayout(content)
        icon_label = QLabel(icon_text)
        icon_label.setStyleSheet("font-size: 32px;")
        icon_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        content_layout.addWidget(icon_label)
        layout.addWidget(content)


class SystemPropertiesDialog(GlassFrame):
    """System properties dialog"""
    
    def __init__(self, parent=None):
        super().__init__(parent, opacity=0.20)
        self.setFixedSize(500, 400)
        self.setWindowTitle("System Properties")
        self.setup_ui()
    
    def setup_ui(self):
        layout = QVBoxLayout(self)
        layout.setContentsMargins(30, 30, 30, 30)
        layout.setSpacing(20)
        
        # Title
        title = QLabel("System Properties")
        title.setStyleSheet("color: white; font-size: 20px; font-weight: bold;")
        title.setAlignment(Qt.AlignmentFlag.AlignCenter)
        layout.addWidget(title)
        
        # System info
        info_layout = QVBoxLayout()
        info_layout.setSpacing(15)
        
        # OS Version
        os_label = QLabel("Operating System: YouOS 10 Build 26M1.5")
        os_label.setStyleSheet("color: white; font-size: 14px;")
        info_layout.addWidget(os_label)
        
        # CPU
        cpu_info = self.get_cpu_info()
        cpu_label = QLabel(f"Processor: {cpu_info}")
        cpu_label.setStyleSheet("color: white; font-size: 14px;")
        cpu_label.setWordWrap(True)
        info_layout.addWidget(cpu_label)
        
        # GPU
        gpu_info = self.get_gpu_info()
        gpu_label = QLabel(f"Graphics: {gpu_info}")
        gpu_label.setStyleSheet("color: white; font-size: 14px;")
        gpu_label.setWordWrap(True)
        info_layout.addWidget(gpu_label)
        
        # RAM
        ram_info = self.get_ram_info()
        ram_label = QLabel(f"Memory: {ram_info}")
        ram_label.setStyleSheet("color: white; font-size: 14px;")
        info_layout.addWidget(ram_label)
        
        # Disk
        disk_info = self.get_disk_info()
        disk_label = QLabel(f"Storage: {disk_info}")
        disk_label.setStyleSheet("color: white; font-size: 14px;")
        info_layout.addWidget(disk_label)
        
        layout.addLayout(info_layout)
        layout.addStretch()
        
        # Close button
        close_btn = QPushButton("Close")
        close_btn.setFixedHeight(40)
        close_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        close_btn.setStyleSheet(f"""
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
        close_btn.clicked.connect(self.close)
        layout.addWidget(close_btn)
    
    def get_cpu_info(self):
        try:
            with open('/proc/cpuinfo', 'r') as f:
                for line in f:
                    if line.startswith('model name'):
                        return line.split(':')[1].strip()
        except:
            pass
        return "Unknown CPU"
    
    def get_gpu_info(self):
        try:
            import subprocess
            result = subprocess.run(['lspci'], capture_output=True, text=True, timeout=3)
            for line in result.stdout.split('\n'):
                if 'VGA' in line or 'Display' in line:
                    return line.split(':')[-1].strip()
        except:
            pass
        return "Unknown GPU"
    
    def get_ram_info(self):
        try:
            with open('/proc/meminfo', 'r') as f:
                for line in f:
                    if line.startswith('MemTotal'):
                        kb = int(line.split()[1])
                        gb = round(kb / 1024 / 1024, 1)
                        return f"{gb} GB"
        except:
            pass
        return "Unknown"
    
    def get_disk_info(self):
        try:
            import shutil
            total, used, free = shutil.disk_usage('/')
            total_gb = round(total / (1024**3), 1)
            free_gb = round(free / (1024**3), 1)
            return f"{total_gb} GB ({free_gb} GB free)"
        except:
            pass
        return "Unknown"


class TaskbarIcon(QWidget):
    """Dynamic taskbar icon"""
    
    clicked = pyqtSignal(str)
    double_clicked = pyqtSignal(str)
    pin_toggled = pyqtSignal(str, bool)
    close_requested = pyqtSignal(str)
    
    def __init__(self, name, icon_text, is_running=False, is_pinned=False, parent=None):
        super().__init__(parent)
        self.name = name
        self.icon_text = icon_text
        self.is_running = is_running
        self.is_pinned = is_pinned
        self.preview = None
        self.last_click_time = 0
        self.click_timer = QTimer()
        self.click_timer.setSingleShot(True)
        self.click_timer.timeout.connect(self.handle_single_click)
        self.setFixedSize(60, 60)
        self.setCursor(Qt.CursorShape.PointingHandCursor)
        self.setMouseTracking(True)
        self.setup_ui()
    
    def setup_ui(self):
        self.layout = QVBoxLayout(self)
        self.layout.setContentsMargins(5, 5, 5, 5)
        
        self.icon_btn = QLabel(self.icon_text)
        self.icon_btn.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.icon_btn.setStyleSheet("font-size: 24px; background: transparent;")
        self.layout.addWidget(self.icon_btn)
        
        self.update_style()
    
    def update_style(self):
        if self.is_running:
            self.setStyleSheet("""
                TaskbarIcon {
                    background: rgba(255, 255, 255, 0.15);
                    border: 1px solid rgba(255, 255, 255, 0.3);
                    border-radius: 12px;
                }
                TaskbarIcon:hover {
                    background: rgba(59, 130, 246, 0.3);
                }
            """)
        else:
            self.setStyleSheet("""
                TaskbarIcon {
                    background: transparent;
                    border: none;
                }
                TaskbarIcon:hover {
                    background: rgba(255, 255, 255, 0.05);
                    border-radius: 12px;
                }
            """)
    
    def set_running(self, running):
        self.is_running = running
        self.update_style()
    
    def set_pinned(self, pinned):
        self.is_pinned = pinned
    
    def mousePressEvent(self, event):
        if event.button() == Qt.MouseButton.LeftButton:
            import time
            current_time = time.time() * 1000
            
            if current_time - self.last_click_time < 500:
                self.click_timer.stop()
                self.double_clicked.emit(self.name)
                self.last_click_time = 0
            else:
                self.last_click_time = current_time
                self.click_timer.start(300)
                
        elif event.button() == Qt.MouseButton.RightButton:
            self.show_context_menu(event.globalPosition().toPoint())
    
    def handle_single_click(self):
        self.clicked.emit(self.name)
    
    def show_context_menu(self, pos):
        try:
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
            
            if self.is_running:
                close_act = QAction("Close All Windows", self)
                close_act.triggered.connect(lambda: self.close_requested.emit(self.name))
                menu.addAction(close_act)
                menu.addSeparator()
            
            new_act = QAction(f"New Window", self)
            new_act.triggered.connect(lambda: self.double_clicked.emit(self.name))
            menu.addAction(new_act)
            
            menu.addSeparator()
            
            pin_text = "Unpin from Taskbar" if self.is_pinned else "Pin to Taskbar"
            pin_act = QAction(pin_text, self)
            pin_act.triggered.connect(lambda: self.pin_toggled.emit(self.name, not self.is_pinned))
            menu.addAction(pin_act)
            
            menu.exec(pos)
        except Exception as e:
            print(f"Error showing context menu: {e}")
    
    def enterEvent(self, event):
        if self.is_running:
            if self.preview:
                self.preview.deleteLater()
            self.preview = AppPreview(self.name, self.icon_text, self.window())
            global_pos = self.mapToGlobal(QPoint(0, 0))
            local_pos = self.window().mapFromGlobal(global_pos)
            self.preview.move(local_pos.x() - (180-60)//2, local_pos.y() - 130)
            self.preview.show()
    
    def leaveEvent(self, event):
        if self.preview:
            QTimer.singleShot(500, self.check_hide_preview)
    
    def check_hide_preview(self):
        if self.preview and not self.preview.underMouse() and not self.underMouse():
            self.preview.hide()
            self.preview.deleteLater()
            self.preview = None


class Taskbar(GlassFrame):
    """Taskbar"""
    
    start_clicked = pyqtSignal()
    battery_clicked = pyqtSignal()
    program_clicked = pyqtSignal(str)
    program_double_clicked = pyqtSignal(str)
    pin_requested = pyqtSignal(str, bool)
    close_requested = pyqtSignal(str)
    task_manager_requested = pyqtSignal()
    
    def __init__(self, parent=None):
        super().__init__(parent, opacity=0.20)
        self.setFixedHeight(70)
        self.icons = {}
        self.setup_ui()
        self.setup_context_menu()
    
    def setup_ui(self):
        self.main_layout = QHBoxLayout(self)
        self.main_layout.setContentsMargins(15, 5, 15, 5)
        self.main_layout.setSpacing(5)
        
        start_btn = QPushButton()
        start_btn.setFixedSize(50, 50)
        start_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        
        if START_ICON_PATH.exists():
            pixmap = QPixmap(str(START_ICON_PATH))
            if not pixmap.isNull():
                scaled_pixmap = pixmap.scaled(32, 32, Qt.AspectRatioMode.KeepAspectRatio, Qt.TransformationMode.SmoothTransformation)
                start_btn.setIcon(QIcon(scaled_pixmap))
                start_btn.setIconSize(scaled_pixmap.size())
        else:
            start_btn.setText("⊞")
            start_btn.setStyleSheet("font-size: 24px;")
        
        start_btn.setStyleSheet("""
            QPushButton { 
                background: rgba(59, 130, 246, 0.3); 
                border: 1px solid rgba(59, 130, 246, 0.5); 
                border-radius: 12px; 
                color: white; 
                font-weight: bold; 
            } 
            QPushButton:hover { 
                background: rgba(59, 130, 246, 0.5); 
            }
        """)
        start_btn.clicked.connect(self.start_clicked.emit)
        start_btn.setContextMenuPolicy(Qt.ContextMenuPolicy.CustomContextMenu)
        start_btn.customContextMenuRequested.connect(self.show_start_context_menu)
        self.main_layout.addWidget(start_btn)
        
        self.icons_container = QWidget()
        self.icons_layout = QHBoxLayout(self.icons_container)
        self.icons_layout.setContentsMargins(0, 0, 0, 0)
        self.icons_layout.setSpacing(5)
        self.main_layout.addWidget(self.icons_container)
        
        self.main_layout.addStretch()
        
        self.battery_widget = QWidget()
        self.battery_widget.setCursor(Qt.CursorShape.PointingHandCursor)
        bat_layout = QHBoxLayout(self.battery_widget)
        bat_layout.setContentsMargins(0, 0, 0, 0)
        self.battery_indicator = BatteryIndicator()
        bat_layout.addWidget(self.battery_indicator)
        self.battery_widget.mousePressEvent = lambda e: self.battery_clicked.emit()
        self.main_layout.addWidget(self.battery_widget)
        
        # WiFi indicator
        self.wifi_widget = QWidget()
        self.wifi_widget.setCursor(Qt.CursorShape.PointingHandCursor)
        wifi_layout = QHBoxLayout(self.wifi_widget)
        wifi_layout.setContentsMargins(5, 0, 5, 0)
        
        self.wifi_icon = QLabel()
        self.wifi_icon.setStyleSheet("color: white; font-size: 16px;")
        self.update_wifi_status()
        wifi_layout.addWidget(self.wifi_icon)
        
        self.main_layout.addWidget(self.wifi_widget)
        
        self.time_label = QLabel()
        self.time_label.setStyleSheet("color: white; font-size: 14px; font-weight: 500;")
        self.main_layout.addWidget(self.time_label)
        self.update_time()
        timer = QTimer(self)
        timer.timeout.connect(self.update_time)
        timer.start(1000)
    
    def update_time(self):
        self.time_label.setText(QTime.currentTime().toString('hh:mm AP'))
    
    def update_wifi_status(self):
        """Update WiFi icon based on signal strength"""
        try:
            import subprocess
            # Get WiFi signal strength on Linux
            result = subprocess.run(['iwconfig'], capture_output=True, text=True, timeout=2)
            output = result.stdout
            
            if 'Link Quality' in output:
                # Extract signal quality
                for line in output.split('\n'):
                    if 'Link Quality' in line:
                        quality_part = line.split('Link Quality=')[1].split(' ')[0]
                        if '/' in quality_part:
                            current, max_val = quality_part.split('/')
                            signal_percent = (int(current) / int(max_val)) * 100
                            
                            if signal_percent > 75:
                                self.wifi_icon.setText("📶")  # Strong signal
                            elif signal_percent > 50:
                                self.wifi_icon.setText("📵")  # Good signal  
                            elif signal_percent > 25:
                                self.wifi_icon.setText("📴")  # Weak signal
                            else:
                                self.wifi_icon.setText("📳")  # Very weak
                            return
            
            # Fallback - check if connected
            ping_result = subprocess.run(['ping', '-c', '1', '8.8.8.8'], 
                                       capture_output=True, timeout=3)
            if ping_result.returncode == 0:
                self.wifi_icon.setText("📶")  # Connected
            else:
                self.wifi_icon.setText("❌")  # Disconnected
                
        except Exception:
            # Default to connected icon if can't determine
            self.wifi_icon.setText("📶")
        
        # Update every 1 second
        QTimer.singleShot(1000, self.update_wifi_status)
    
    def setup_context_menu(self):
        self.setContextMenuPolicy(Qt.ContextMenuPolicy.CustomContextMenu)
        self.customContextMenuRequested.connect(self.show_taskbar_context_menu)
    
    def show_taskbar_context_menu(self, pos):
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
        
        task_manager_action = QAction("📊 Task Manager", self)
        task_manager_action.triggered.connect(self.task_manager_requested.emit)
        menu.addAction(task_manager_action)
        
        menu.exec(self.mapToGlobal(pos))
    
    def show_start_context_menu(self, pos):
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
        
        properties_action = QAction("💻 System Properties", self)
        properties_action.triggered.connect(self.show_system_properties)
        menu.addAction(properties_action)
        
        menu.exec(self.mapToGlobal(pos))
    
    def show_system_properties(self):
        """Show system properties dialog"""
        try:
            dialog = SystemPropertiesDialog(self.parent())
            dialog.move((self.parent().width() - 500) // 2, (self.parent().height() - 400) // 2)
            dialog.show()
        except Exception as e:
            print(f"Error showing system properties: {e}")
    
    def update_taskbar(self, running_apps, pinned_apps, app_metadata):
        all_apps = running_apps.union(pinned_apps)
        
        for name in list(self.icons.keys()):
            if name not in all_apps:
                self.icons[name].deleteLater()
                del self.icons[name]
        
        for name in all_apps:
            is_running = name in running_apps
            is_pinned = name in pinned_apps
            icon_text = app_metadata.get(name, "📦")
            
            if name not in self.icons:
                icon = TaskbarIcon(name, icon_text, is_running, is_pinned, self)
                icon.clicked.connect(self.program_clicked.emit)
                icon.double_clicked.connect(self.program_double_clicked.emit)
                icon.pin_toggled.connect(self.pin_requested.emit)
                icon.close_requested.connect(self.close_requested.emit)
                self.icons[name] = icon
                self.icons_layout.addWidget(icon)
            else:
                self.icons[name].set_running(is_running)
                self.icons[name].set_pinned(is_pinned)


class DesktopManager(QWidget):
    """Desktop manager - main desktop functionality with window management"""
    
    logout_requested = pyqtSignal()
    restart_requested = pyqtSignal()
    shutdown_requested = pyqtSignal()
    
    def __init__(self, auth_manager, username, parent=None):
        super().__init__(parent)
        self.auth = auth_manager
        self.username = username
        self.window_manager = WindowManager(self)
        self.pinned_apps = set()
        self.app_metadata = {}
        self.installed_programs = []
        self.desktop_icons = {}  # Track desktop icons
        self.desktop_icon_positions = {}  # Track icon positions
        self.start_menu = None
        self.confirm_dialog = None
        self.quick_settings = None
        self.system_addons = None
        self.notification_center = None
        self.error_dialog = None
        self.wallpaper_dialog = None
        self.app_store_window = None
        self.current_wallpaper = None
        self.notifications = []
        self.live_score_widgets = []  # Track live score widgets
        self.sports_notification_service = None
        
        self.scan_installed_programs()
        self.load_user_state()  # Load user state FIRST (includes file shortcuts)
        self.setup_ui()
        self.setup_context_menu()
        self.load_wallpaper()
        self.update_taskbar()
        
        # Start WhatsApp notification service if not running
        self.start_whatsapp_service()
        
        # Initialize update manager
        self.update_manager = None
        QTimer.singleShot(5000, self.initialize_updates)  # Start after 5 seconds
        
        print(f"✅ Desktop initialized for user: {username}")
    
    def scan_installed_programs(self):
        self.installed_programs = []
        self.app_metadata = {}
        
        builtin_programs = [
            ("💻", "My Computer", "file_manager.py"),
            ("📁", "File Manager", "file_manager.py"),
            ("🖼️", "Media Viewer", "media_viewer.py"),
            ("🌐", "Browser", "browser.py"),
            ("⚙️", "Settings", "settings.py"),
            ("💻", "Terminal", "terminal.py"),
            ("🔢", "Calculator", "calculator.py"),
            ("📝", "Text Editor", "text_editor.py"),
            ("🗑️", "Recycle Bin", "recycle_bin.py"),
            ("📊", "Task Manager", None),
            ("🏪", "App Store", None),
        ]
        
        for icon, name, script in builtin_programs:
            self.installed_programs.append((icon, name))
            self.app_metadata[name] = icon
        
        if PROGRAMS_DIR.exists():
            for item in PROGRAMS_DIR.iterdir():
                if item.is_dir():
                    metadata_file = item / "metadata.json"
                    if metadata_file.exists():
                        try:
                            with open(metadata_file, 'r') as f:
                                metadata = json.load(f)
                                icon = metadata.get('icon', '📦')
                                name = metadata.get('name', item.name)
                                self.installed_programs.append((icon, name))
                                self.app_metadata[name] = icon
                        except:
                            name = item.name.replace('_', ' ').title()
                            self.installed_programs.append(('📦', name))
                            self.app_metadata[name] = '📦'
                    else:
                        name = item.name.replace('_', ' ').title()
                        self.installed_programs.append(('📦', name))
                        self.app_metadata[name] = '📦'
    
    def load_desktop_icons(self):
        """Load desktop icons from saved positions or create default layout"""
        print(f"Loading desktop icons. Saved positions: {self.desktop_icon_positions}")
        
        if self.desktop_icon_positions:
            # Load from saved positions
            for name, pos_data in self.desktop_icon_positions.items():
                # Check if this is a file shortcut
                if name in self.file_shortcuts:
                    # Get icon from file shortcuts
                    file_path = self.file_shortcuts[name]
                    if os.path.isdir(file_path):
                        icon = "📁"  # Folder icon
                    else:
                        ext = os.path.splitext(file_path)[1].lower()
                        if ext in ['.jpg', '.jpeg', '.png', '.gif', '.bmp', '.webp']:
                            icon = "🖼"
                        elif ext in ['.mp4', '.avi', '.mkv', '.mov']:
                            icon = "🎦"
                        elif ext in ['.mp3', '.wav', '.ogg', '.flac']:
                            icon = "🎵"
                        else:
                            icon = "📄"
                else:
                    # Regular program icon
                    icon = self.app_metadata.get(name)
                
                if icon:
                    icon_widget = DesktopIcon(name, icon, self)
                    icon_widget.clicked.connect(self.launch_program)
                    icon_widget.pin_toggled.connect(self.on_pin_toggled)
                    icon_widget.remove_requested.connect(self.remove_desktop_icon)
                    icon_widget.position_changed.connect(self.save_icon_position)
                    icon_widget.setParent(self.desktop_widget)
                    icon_widget.move(pos_data['x'], pos_data['y'])
                    icon_widget.show()
                    self.desktop_icons[name] = icon_widget
                    print(f"Loaded icon {name} at position ({pos_data['x']}, {pos_data['y']})")
        else:
            # Create default layout only if no saved positions exist
            print("No saved positions found, creating default layout")
            default_icons = ['My Computer', 'File Manager', 'Browser', 'Calculator', 'Text Editor', 'Settings']
            for i, name in enumerate(default_icons[:6]):
                if name in self.app_metadata:
                    icon = self.app_metadata[name]
                    icon_widget = DesktopIcon(name, icon, self)
                    icon_widget.clicked.connect(self.launch_program)
                    icon_widget.pin_toggled.connect(self.on_pin_toggled)
                    icon_widget.remove_requested.connect(self.remove_desktop_icon)
                    icon_widget.position_changed.connect(self.save_icon_position)
                    icon_widget.setParent(self.desktop_widget)
                    
                    # Position in grid
                    x = (i % 6) * 100 + 20
                    y = (i // 6) * 120 + 20
                    icon_widget.move(x, y)
                    icon_widget.show()
                    
                    self.desktop_icons[name] = icon_widget
                    self.desktop_icon_positions[name] = {'x': x, 'y': y}
            
            self.save_user_state()
    
    def save_icon_position(self, name, x, y):
        """Save icon position when moved"""
        self.desktop_icon_positions[name] = {'x': x, 'y': y}
        self.save_user_state()
    
    def load_wallpaper(self):
        try:
            user_data = self.auth.users.get(self.username, {})
            wallpaper_path = user_data.get('wallpaper')
            if wallpaper_path and Path(wallpaper_path).exists():
                self.apply_wallpaper(wallpaper_path)
        except Exception as e:
            print(f"⚠️  Failed to load wallpaper: {e}")
    
    def setup_context_menu(self):
        self.setContextMenuPolicy(Qt.ContextMenuPolicy.CustomContextMenu)
        self.customContextMenuRequested.connect(self.show_context_menu)
    
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
        
        wallpaper_action = QAction("🖼️  Change Wallpaper", self)
        wallpaper_action.triggered.connect(self.change_wallpaper)
        menu.addAction(wallpaper_action)
        
        menu.addSeparator()
        
        settings_action = QAction("⚙️  Settings", self)
        settings_action.triggered.connect(self.open_settings)
        menu.addAction(settings_action)
        
        menu.addSeparator()
        
        refresh_action = QAction("🔄  Refresh", self)
        refresh_action.triggered.connect(self.refresh_desktop)
        menu.addAction(refresh_action)
        
        menu.exec(self.mapToGlobal(pos))
    
    def apply_wallpaper(self, wallpaper_path):
        self.current_wallpaper = wallpaper_path
        pixmap = QPixmap(wallpaper_path)
        if not pixmap.isNull():
            scaled = pixmap.scaled(self.size(), Qt.AspectRatioMode.KeepAspectRatioByExpanding, Qt.TransformationMode.SmoothTransformation)
            palette = self.palette()
            palette.setBrush(QPalette.ColorRole.Window, QBrush(scaled))
            self.setPalette(palette)
            self.setAutoFillBackground(True)
            try:
                config = self.auth.users.get(self.username, {})
                config['wallpaper'] = wallpaper_path
                self.auth.users[self.username] = config
                self.auth.save_users()
            except:
                pass
            play_sound("success.wav")
    
    def resizeEvent(self, event):
        super().resizeEvent(event)
        if self.current_wallpaper and Path(self.current_wallpaper).exists():
            self.apply_wallpaper(self.current_wallpaper)
    
    def change_wallpaper(self):
        play_sound("click.wav")
        if self.wallpaper_dialog:
            self.wallpaper_dialog.close()
            self.wallpaper_dialog = None
        
        self.wallpaper_dialog = WallpaperDialog(self)
        self.wallpaper_dialog.wallpaper_selected.connect(self.apply_wallpaper)
        self.wallpaper_dialog.closed.connect(self.cleanup_wallpaper_dialog)
        self.wallpaper_dialog.move((self.width() - 600) // 2, (self.height() - 500) // 2)
        self.wallpaper_dialog.show()
    
    def cleanup_wallpaper_dialog(self):
        if self.wallpaper_dialog:
            self.wallpaper_dialog.deleteLater()
            self.wallpaper_dialog = None
    
    def open_settings(self):
        play_sound("click.wav")
        self.launch_program("Settings")
    
    def refresh_desktop(self):
        play_sound("click.wav")
        self.scan_installed_programs()
        # Clear existing icons
        for icon_widget in self.desktop_icons.values():
            icon_widget.deleteLater()
        self.desktop_icons.clear()
        # Reload icons
        self.load_desktop_icons()
    
    def show_error(self, title, message):
        play_sound("error.wav")
        if self.error_dialog:
            self.error_dialog.deleteLater()
        self.error_dialog = ErrorDialog(title, message, self)
        self.error_dialog.closed.connect(lambda: self.error_dialog.deleteLater() if self.error_dialog else None)
        self.error_dialog.move((self.width() - 450) // 2, (self.height() - 300) // 2)
        self.error_dialog.show()
    
    def load_user_state(self):
        try:
            user_data = self.auth.users.get(self.username, {})
            self.pinned_apps = set(user_data.get('pinned_apps', []))
            self.desktop_icon_positions = user_data.get('desktop_icons', {})
            self.file_shortcuts = user_data.get('file_shortcuts', {})
            print(f"Loaded user state: pinned={len(self.pinned_apps)}, desktop_icons={len(self.desktop_icon_positions)}, shortcuts={len(self.file_shortcuts)}")
        except Exception as e:
            print(f"Error loading user state: {e}")
            self.pinned_apps = set()
            self.desktop_icon_positions = {}
            self.file_shortcuts = {}
    

    def save_user_state(self):
        try:
            # Ensure user exists in auth.users
            if self.username not in self.auth.users:
                self.auth.users[self.username] = {
                    'password': '',
                    'theme': 'dark',
                    'wallpaper': 'default.jpg'
                }
            
            # Update only the specific fields we want to save
            user_data = self.auth.users[self.username]
            user_data['pinned_apps'] = list(self.pinned_apps)
            user_data['desktop_icons'] = self.desktop_icon_positions
            user_data['file_shortcuts'] = getattr(self, 'file_shortcuts', {})
            
            # Save to file
            self.auth.save_users()
            print(f"Saved user state: pinned={len(self.pinned_apps)}, desktop_icons={len(self.desktop_icon_positions)}, shortcuts={len(getattr(self, 'file_shortcuts', {}))}")
        except Exception as e:
            print(f"Error saving user state: {e}")
    
    def update_taskbar(self):
        running_names = self.window_manager.get_running_programs()
        self.taskbar.update_taskbar(running_names, self.pinned_apps, self.app_metadata)
    
    def handle_taskbar_single_click(self, name):
        """Handle single click on taskbar icon - focus/restore or minimize"""
        print(f"Single click on: {name}")
        
        windows = self.window_manager.get_windows_for_program(name)
        if not windows:
            # Not running, launch it
            self.launch_program(name)
            return
        
        # Toggle minimize/restore
        self.window_manager.focus_or_restore_program(name)
        play_sound("click.wav")
    
    def handle_taskbar_double_click(self, name):
        """Handle double click on taskbar icon - open new instance"""
        print(f"Double click on: {name} - opening new instance")
        play_sound("click.wav")
        self.launch_program(name)
    
    def on_pin_toggled(self, name, pinned):
        if pinned:
            self.pinned_apps.add(name)
        else:
            self.pinned_apps.discard(name)
        self.save_user_state()
        self.update_taskbar()
    
    def close_app_instances(self, name):
        """Close all windows of a program"""
        windows = self.window_manager.get_windows_for_program(name)
        for window_id in windows[:]:
            if window_id in self.window_manager.windows:
                self.window_manager.windows[window_id]['window'].close_window()
        self.update_taskbar()
    
    def remove_desktop_icon(self, name):
        """Remove desktop icon (but keep in start menu and taskbar if pinned)"""
        if name in self.desktop_icons:
            icon_widget = self.desktop_icons[name]
            icon_widget.deleteLater()
            del self.desktop_icons[name]
            if name in self.desktop_icon_positions:
                del self.desktop_icon_positions[name]
            # Also remove from file shortcuts if it's a shortcut
            if name in self.file_shortcuts:
                del self.file_shortcuts[name]
            self.save_user_state()
            play_sound("click.wav")
    
    def add_icon_to_desktop(self, name):
        """Add program icon to desktop"""
        if name not in self.desktop_icons:
            # Find the program in installed_programs
            icon = None
            for prog_icon, prog_name in self.installed_programs:
                if prog_name == name:
                    icon = prog_icon
                    break
            
            if icon:
                # Find next available position
                x, y = 20, 20
                while any(pos['x'] == x and pos['y'] == y for pos in self.desktop_icon_positions.values()):
                    x += 100
                    if x > 500:
                        x = 20
                        y += 120
                
                # Create and add icon
                icon_widget = DesktopIcon(name, icon, self)
                icon_widget.clicked.connect(self.launch_program)
                icon_widget.pin_toggled.connect(self.on_pin_toggled)
                icon_widget.remove_requested.connect(self.remove_desktop_icon)
                icon_widget.position_changed.connect(self.save_icon_position)
                icon_widget.setParent(self.desktop_widget)
                icon_widget.move(x, y)
                icon_widget.show()
                
                self.desktop_icons[name] = icon_widget
                self.desktop_icon_positions[name] = {'x': x, 'y': y}
                self.save_user_state()
                play_sound("click.wav")
    def setup_ui(self):
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(0)
        
        content_layout = QHBoxLayout()
        content_layout.setContentsMargins(10, 10, 10, 0)
        content_layout.setSpacing(10)
        
        desktop = QWidget()
        desktop.setStyleSheet("background: transparent;")
        
        # Use absolute positioning instead of grid layout for free positioning
        self.desktop_widget = desktop
        
        # Load saved desktop icons
        self.load_desktop_icons()
        
        content_layout.addWidget(desktop, stretch=1)
        self.widget_panel = WidgetPanel()
        content_layout.addWidget(self.widget_panel)
        layout.addLayout(content_layout, stretch=1)
        
        self.taskbar = Taskbar()
        self.taskbar.start_clicked.connect(self.toggle_start_menu)
        self.taskbar.battery_clicked.connect(self.toggle_quick_settings)
        self.taskbar.program_clicked.connect(self.handle_taskbar_single_click)
        self.taskbar.program_double_clicked.connect(self.handle_taskbar_double_click)
        self.taskbar.pin_requested.connect(self.on_pin_toggled)
        self.taskbar.close_requested.connect(self.close_app_instances)
        self.taskbar.task_manager_requested.connect(self.open_task_manager)
        
        self.notif_button = GlassFrame(self, opacity=0.20)
        self.notif_button.setFixedSize(60, 60)
        notif_layout = QVBoxLayout(self.notif_button)
        notif_layout.setContentsMargins(0, 0, 0, 0)
        notif_btn = QPushButton("🔔")
        notif_btn.setFixedSize(60, 60)
        notif_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        notif_btn.setStyleSheet("""
            QPushButton {
                background: transparent;
                border: none;
                color: white;
                font-size: 28px;
            }
            QPushButton:hover {
                background: rgba(59, 130, 246, 0.2);
                border-radius: 12px;
            }
        """)
        notif_btn.clicked.connect(self.toggle_notification_center)
        notif_layout.addWidget(notif_btn)
        
        # System Add-ons button (separate from taskbar) - REMOVED
        # self.addons_button = GlassFrame(self, opacity=0.20)
        # self.addons_button.setFixedSize(60, 60)
        # addons_layout = QVBoxLayout(self.addons_button)
        # addons_layout.setContentsMargins(0, 0, 0, 0)
        # addons_btn = QPushButton("🔧")
        # addons_btn.setFixedSize(60, 60)
        # addons_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        # addons_btn.setStyleSheet("""
        #     QPushButton {
        #         background: transparent;
        #         border: none;
        #         color: white;
        #         font-size: 28px;
        #     }
        #     QPushButton:hover {
        #         background: rgba(147, 51, 234, 0.2);
        #         border-radius: 12px;
        #     }
        # """)
        # addons_btn.clicked.connect(self.toggle_system_addons)
        # addons_layout.addWidget(addons_btn)
        
        taskbar_layout = QHBoxLayout()
        taskbar_layout.setContentsMargins(0, 0, 0, 10)
        taskbar_layout.addStretch()
        taskbar_layout.addWidget(self.notif_button)
        taskbar_layout.addSpacing(10)
        taskbar_layout.addWidget(self.taskbar)
        taskbar_layout.addStretch()
        layout.addLayout(taskbar_layout)
    
    def toggle_notification_center(self):
        if self.notification_center and self.notification_center.isVisible():
            self.notification_center.hide()
            self.notification_center.deleteLater()
            self.notification_center = None
        else:
            self.close_start_menu()
            if self.quick_settings:
                self.quick_settings.hide()
                self.quick_settings.deleteLater()
                self.quick_settings = None
            if self.system_addons:
                self.system_addons.hide()
                self.system_addons.deleteLater()
                self.system_addons = None
            
            self.notification_center = NotificationCenter(self.notifications, self)
            self.notification_center.closed.connect(lambda: self.notification_center.deleteLater() if self.notification_center else None)
            notif_btn_pos = self.notif_button.mapTo(self, QPoint(0, 0))
            x = notif_btn_pos.x()
            y = self.height() - 70 - 600 - 20
            self.notification_center.move(x, y)
            self.notification_center.show()
            play_sound("click.wav")
    
    def add_notification(self, title, message, icon="📢", time="Now"):
        """Add a notification to the list and show popup"""
        notification = {
            'title': title,
            'message': message,
            'icon': icon,
            'time': time
        }
        self.notifications.append(notification)
        if len(self.notifications) > 50:
            self.notifications.pop(0)
        
        # Show notification popup
        self.show_notification_popup(notification)
        
        # Play notification sound
        try:
            import pygame
            pygame.mixer.init()
            sound_path = "/home/yousuf-yasser-elshaer/codes/os/assets/sounds/notification.wav"
            if Path(sound_path).exists():
                pygame.mixer.music.load(sound_path)
                pygame.mixer.music.play()
        except Exception as e:
            print(f"Could not play notification sound: {e}")
            # Fallback to system beep
            try:
                import subprocess
                subprocess.run(['paplay', '/usr/share/sounds/alsa/Front_Left.wav'], 
                             capture_output=True, timeout=1)
            except:
                pass
    
    def show_notification_popup(self, notification):
        """Show a notification popup"""
        popup = NotificationPopup(notification, self)
        
        # Connect click handler for update notifications
        popup.clicked.connect(self.handle_notification_click)
        
        # Position popup in top-right corner
        screen_geometry = self.screen().geometry()
        popup_x = screen_geometry.width() - popup.width() - 20
        popup_y = 80 + (len([w for w in self.findChildren(NotificationPopup) if w.isVisible()]) * 110)
        
        popup.move(popup_x, popup_y)
        popup.show()
        
        # Clean up when closed
        popup.closed.connect(lambda: self.cleanup_popups())
    
    def cleanup_popups(self):
        """Clean up closed popups and reposition remaining ones"""
        QTimer.singleShot(100, self.reposition_popups)
    
    def reposition_popups(self):
        """Reposition visible notification popups"""
        visible_popups = [w for w in self.findChildren(NotificationPopup) if w.isVisible()]
        screen_geometry = self.screen().geometry()
        
        for i, popup in enumerate(visible_popups):
            popup_x = screen_geometry.width() - popup.width() - 20
            popup_y = 80 + (i * 110)
            popup.move(popup_x, popup_y)
    
    def toggle_system_addons(self):
        """System Add-ons removed - no longer available"""
        pass
    
    def toggle_quick_settings(self):
        if self.quick_settings and self.quick_settings.isVisible():
            self.quick_settings.hide()
            self.quick_settings.deleteLater()
            self.quick_settings = None
        else:
            self.close_start_menu()
            if self.system_addons:
                self.system_addons.hide()
                self.system_addons.deleteLater()
                self.system_addons = None
            if self.notification_center:
                self.notification_center.hide()
                self.notification_center.deleteLater()
                self.notification_center = None
            
            self.quick_settings = QuickSettingsMenu(self)
            x = self.width() - self.widget_panel.width() - 350 - 30
            y = self.height() - 70 - 420 - 20
            self.quick_settings.move(x, y)
            self.quick_settings.show()
            play_sound("click.wav")
    
    def toggle_start_menu(self):
        if self.start_menu and self.start_menu.isVisible():
            self.close_start_menu()
        else:
            if self.quick_settings:
                self.quick_settings.hide()
                self.quick_settings.deleteLater()
                self.quick_settings = None
            if self.system_addons:
                self.system_addons.hide()
                self.system_addons.deleteLater()
                self.system_addons = None
            if self.notification_center:
                self.notification_center.hide()
                self.notification_center.deleteLater()
                self.notification_center = None
            
            self.start_menu = StartMenu(self.installed_programs, self, self)
            self.start_menu.program_clicked.connect(self.launch_program)
            self.start_menu.logout_clicked.connect(self.request_logout)
            self.start_menu.restart_clicked.connect(self.request_restart)
            self.start_menu.shutdown_clicked.connect(self.request_shutdown)
            self.start_menu.pin_toggled.connect(self.on_pin_toggled)
            self.start_menu.add_to_desktop.connect(self.add_icon_to_desktop)
            self.start_menu.move((self.width() - 600) // 2, self.height() - 70 - 700 - 20)
            self.start_menu.show()
            play_sound("click.wav")
    
    def close_start_menu(self):
        if self.start_menu:
            self.start_menu.hide()
            self.start_menu.deleteLater()
            self.start_menu = None
    
    def request_logout(self):
        self.close_start_menu()
        self.show_confirm_dialog("Logout", "Logout?", "🔓", self.logout_requested.emit)
    
    def request_restart(self):
        self.close_start_menu()
        self.show_confirm_dialog("Restart", "Restart?", "🔄", self.restart_requested.emit)
    
    def request_shutdown(self):
        self.close_start_menu()
        self.show_confirm_dialog("Shutdown", "Shutdown?", "⏻", self.shutdown_requested.emit)
    
    def show_confirm_dialog(self, title, message, icon, callback):
        # Import from the correct main.py in os directory
        try:
            import importlib.util
            import sys
            main_path = BASE_DIR / 'main.py'
            spec = importlib.util.spec_from_file_location("youos_main", main_path)
            main_module = importlib.util.module_from_spec(spec)
            
            original_path = sys.path.copy()
            try:
                spec.loader.exec_module(main_module)
            finally:
                sys.path = original_path
            
            ConfirmDialog = main_module.ConfirmDialog
            
            # Clean up existing dialog safely
            if self.confirm_dialog:
                try:
                    if not self.confirm_dialog.isHidden():
                        self.confirm_dialog.hide()
                    self.confirm_dialog.deleteLater()
                except RuntimeError:
                    pass
                self.confirm_dialog = None
            
            self.confirm_dialog = ConfirmDialog(title, message, icon, self)
            self.confirm_dialog.confirmed.connect(callback)
            self.confirm_dialog.confirmed.connect(self.on_confirm_dialog_closed)
            self.confirm_dialog.cancelled.connect(self.on_confirm_dialog_closed)
            self.confirm_dialog.move((self.width() - 400) // 2, (self.height() - 250) // 2)
            self.confirm_dialog.show()
            
        except Exception as e:
            print(f"✗ Error showing confirm dialog: {e}")
            import traceback
            traceback.print_exc()
    
    def on_confirm_dialog_closed(self):
        """Safely clean up confirm dialog"""
        if self.confirm_dialog:
            try:
                self.confirm_dialog.deleteLater()
            except RuntimeError:
                pass
            self.confirm_dialog = None
    
    def open_task_manager(self):
        print("🚀 Opening Task Manager")
        play_sound("click.wav")
        self.launch_program("Task Manager")
    
    def open_app_store(self):
        print("🚀 Opening App Store")
        play_sound("click.wav")
        
        try:
            # Try to import the app store module from the correct path
            app_store_path = BASE_DIR / 'app_store.py'
            if not app_store_path.exists():
                raise ImportError("app_store.py not found")
            
            import importlib.util
            spec = importlib.util.spec_from_file_location("app_store_module", app_store_path)
            app_store_module = importlib.util.module_from_spec(spec)
            
            # Store original sys.path
            original_path = sys.path.copy()
            try:
                spec.loader.exec_module(app_store_module)
            finally:
                sys.path = original_path
            
            # Try different function/class names
            app_store_widget = None
            
            # Try function first (open_app_store)
            if hasattr(app_store_module, 'open_app_store'):
                print("✓ Found open_app_store function")
                result = app_store_module.open_app_store(self)
                if result and hasattr(result, 'content_widget'):
                    app_store_widget = result.content_widget
                elif result:
                    app_store_widget = result
            
            # Try class names
            if not app_store_widget:
                for class_name in ['AppStore', 'AppStoreWindow', 'AppStoreWidget', 'Store']:
                    if hasattr(app_store_module, class_name):
                        print(f"✓ Found class: {class_name}")
                        cls = getattr(app_store_module, class_name)
                        try:
                            app_store_widget = cls(desktop_manager=self, is_standalone=False)
                        except TypeError:
                            try:
                                app_store_widget = cls(desktop_manager=self)
                            except TypeError:
                                try:
                                    app_store_widget = cls(is_standalone=False)
                                except TypeError:
                                    app_store_widget = cls()
                        break
            
            if app_store_widget:
                # Connect notification signal if available
                if hasattr(app_store_widget, 'notification_requested'):
                    app_store_widget.notification_requested.connect(
                        lambda title, message, icon: self.add_notification(title, message, icon, "Now")
                    )
                
                # Create window
                window_id = self.window_manager.create_window("App Store", "🏪", app_store_widget)
                self.update_taskbar()
                print("✓ App Store opened in window")
            else:
                raise ImportError("No suitable App Store class or function found")
            
        except Exception as e:
            print(f"✗ Failed to import App Store: {e}")
            import traceback
            traceback.print_exc()
            
            # Create simple placeholder app store widget
            app_store_widget = QWidget()
            app_store_widget.setStyleSheet(f"background: {COLORS['bg_secondary']};")
            layout = QVBoxLayout(app_store_widget)
            layout.setContentsMargins(40, 40, 40, 40)
            layout.setAlignment(Qt.AlignmentFlag.AlignCenter)
            
            icon_label = QLabel("🏪")
            icon_label.setStyleSheet("font-size: 72px;")
            icon_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
            layout.addWidget(icon_label)
            
            title = QLabel("App Store")
            title.setStyleSheet("color: white; font-size: 24px; font-weight: bold;")
            title.setAlignment(Qt.AlignmentFlag.AlignCenter)
            layout.addWidget(title)
            
            layout.addSpacing(20)
            
            desc = QLabel("The App Store module is not properly configured.\nPlease check app_store.py")
            desc.setStyleSheet("color: #9ca3af; font-size: 14px;")
            desc.setAlignment(Qt.AlignmentFlag.AlignCenter)
            desc.setWordWrap(True)
            layout.addWidget(desc)
            
            window_id = self.window_manager.create_window("App Store", "🏪", app_store_widget)
            self.update_taskbar()
    
    def launch_program(self, name):
        """Launch a program in a new window"""
        print(f"🚀 Launching: {name}")
        play_sound("click.wav")
        self.close_start_menu()
        
        # Check if this is a file shortcut
        if hasattr(self, 'file_shortcuts') and name in self.file_shortcuts:
            self.launch_file_shortcut(name)
            return
        
        if name == "App Store":
            self.open_app_store()
            return
        
        if name == "Task Manager":
            self.launch_task_manager()
            return
        
        # Try to launch custom program from programs directory
        builtin_map = {
            "My Computer": ("file_manager", "FileManager"),
            "File Manager": ("file_manager", "FileManager"),
            "Media Viewer": ("media_viewer", "MediaViewer"),
            "Browser": ("browser", "Browser"),
            "Settings": ("settings", ["SettingsApp", "Settings", "SettingsWindow"]),
            "Terminal": ("terminal", ["TerminalApp", "Terminal", "TerminalEmulator"]),
            "Calculator": ("calculator", ["CalculatorApp", "Calculator"]),
            "Text Editor": ("text_editor", "TextEditor"),
            "Recycle Bin": ("recycle_bin", ["RecycleBinApp", "RecycleBin", "RecycleBinWindow"]),
        }
        
        if name in builtin_map:
            module_name, class_names = builtin_map[name]
            
            # Special handling for Browser - launch as separate process
            if name == "Browser":
                browser_path = BASE_DIR / "browser.py"
                if browser_path.exists():
                    self.launch_as_separate_process(browser_path, name)
                else:
                    self.show_placeholder_window(name)
                return
            
            # Normalize class_names to always be a list
            if isinstance(class_names, str):
                class_names = [class_names]
            
            try:
                # Import from the OS directory, not from sys.path
                module_path = BASE_DIR / f"{module_name}.py"
                if not module_path.exists():
                    print(f"✗ Module file not found: {module_path}")
                    self.show_placeholder_window(name)
                    return
                
                import importlib.util
                spec = importlib.util.spec_from_file_location(module_name, module_path)
                module = importlib.util.module_from_spec(spec)
                
                # Store original sys.path
                original_path = sys.path.copy()
                try:
                    spec.loader.exec_module(module)
                finally:
                    # Restore original sys.path to prevent pollution
                    sys.path = original_path
                
                # Try to find the class
                program_class = None
                found_class_name = None
                
                for class_name in class_names:
                    if hasattr(module, class_name):
                        program_class = getattr(module, class_name)
                        found_class_name = class_name
                        print(f"✓ Found class: {class_name}")
                        break
                
                if not program_class:
                    print(f"✗ None of these classes found: {class_names}")
                    # Try alternative class names from module
                    available_classes = [n for n in dir(module) if not n.startswith('_') and isinstance(getattr(module, n), type)]
                    print(f"   Available classes: {available_classes}")
                    
                    for alt_name in available_classes:
                        if 'Window' in alt_name or 'Widget' in alt_name or 'App' in alt_name:
                            program_class = getattr(module, alt_name)
                            found_class_name = alt_name
                            print(f"✓ Using alternative class: {alt_name}")
                            break
                    
                    if not program_class:
                        self.show_placeholder_window(name)
                        return
                
                # Create instance with different parameter combinations
                content_widget = None
                try:
                    content_widget = program_class(is_standalone=False)
                except TypeError:
                    try:
                        content_widget = program_class()
                    except TypeError as e:
                        if 'root' in str(e):
                            # Calculator needs root parameter
                            print(f"✗ {found_class_name} requires 'root' parameter - creating wrapper")
                            # Create a wrapper widget
                            wrapper = QWidget()
                            wrapper.setStyleSheet(f"background: {COLORS['bg_secondary']};")
                            layout = QVBoxLayout(wrapper)
                            label = QLabel(f"⚠️ {name} needs to be updated to work in window mode")
                            label.setStyleSheet("color: white; font-size: 14px;")
                            label.setAlignment(Qt.AlignmentFlag.AlignCenter)
                            layout.addWidget(label)
                            content_widget = wrapper
                        else:
                            print(f"✗ Failed to instantiate {found_class_name}: {e}")
                            self.show_placeholder_window(name)
                            return
                
                # Create window
                icon = self.app_metadata.get(name, "📦")
                window_id = self.window_manager.create_window(name, icon, content_widget)
                self.update_taskbar()
                
                print(f"✓ Launched {name} in window")
                
            except Exception as e:
                print(f"✗ Failed to import {name}: {e}")
                import traceback
                traceback.print_exc()
                self.show_placeholder_window(name)
        else:
            # Try to launch custom program from programs directory
            self.launch_custom_program(name)
    
    def launch_custom_program(self, name):
        """Launch a custom program from the programs directory"""
        print(f"🔍 Looking for custom program: {name}")
        
        # Special handling for WhatsApp - launch as separate process
        if name.lower() == 'whatsapp':
            whatsapp_path = PROGRAMS_DIR / 'whatsapp' / 'main.py'
            if whatsapp_path.exists():
                self.launch_as_separate_process(whatsapp_path, name)
            else:
                self.show_placeholder_window(name)
            return
        
        # Find the program folder
        app_folder = name.lower().replace(' ', '_')
        app_path = PROGRAMS_DIR / app_folder
        
        if not app_path.exists():
            print(f"✗ Program folder not found: {app_path}")
            self.show_placeholder_window(name)
            return
        
        # Look for main file
        main_file = None
        for filename in ['main.py', 'app.py', f"{app_folder}.py"]:
            potential_file = app_path / filename
            if potential_file.exists():
                main_file = potential_file
                break
        
        if not main_file:
            print(f"✗ No main file found in {app_path}")
            self.show_placeholder_window(name)
            return
        
        print(f"✓ Found program at: {main_file}")
        
        try:
            # Import using importlib to avoid sys.path pollution
            import importlib.util
            spec = importlib.util.spec_from_file_location(app_folder, main_file)
            module = importlib.util.module_from_spec(spec)
            
            # Store original sys.path and add program directory temporarily
            original_path = sys.path.copy()
            if str(app_path) not in sys.path:
                sys.path.insert(0, str(app_path))
            
            try:
                spec.loader.exec_module(module)
            finally:
                # Restore original sys.path
                sys.path = original_path
            
            # Try to find a main widget class
            widget_class = None
            class_name = ''.join(word.capitalize() for word in app_folder.split('_'))
            
            # Try common class names
            potential_names = [
                class_name,
                f"{class_name}App",
                f"{class_name}Widget",
                f"{class_name}Window",
                'MainWindow', 
                'MainWidget', 
                'App', 
                'Application',
                'PaintApp',
                'WhatsAppApp',  # For WhatsApp
                'DeskToolsMain',
                'ChessGame',  # For Chess
                'NotesApp',   # For Notes
            ]
            
            for potential_name in potential_names:
                if hasattr(module, potential_name):
                    widget_class = getattr(module, potential_name)
                    print(f"✓ Found class: {potential_name}")
                    break
            
            if widget_class:
                try:
                    content_widget = widget_class(is_standalone=False)
                except TypeError:
                    try:
                        content_widget = widget_class()
                    except Exception as e:
                        print(f"✗ Failed to instantiate {widget_class.__name__}: {e}")
                        self.show_placeholder_window(name)
                        return
                
                # Create window
                icon = self.app_metadata.get(name, "📦")
                window_id = self.window_manager.create_window(name, icon, content_widget)
                self.update_taskbar()
                print(f"✓ Launched custom program {name} in window")
            else:
                print(f"✗ No suitable widget class found in {main_file}")
                print(f"   Available classes: {[name for name in dir(module) if not name.startswith('_') and isinstance(getattr(module, name), type)]}")
                self.show_placeholder_window(name)
                
        except Exception as e:
            print(f"✗ Failed to load custom program {name}: {e}")
            import traceback
            traceback.print_exc()
            self.show_placeholder_window(name)
    
    def launch_as_separate_process(self, script_path, name):
        """Launch program as separate process"""
        try:
            import subprocess
            import sys
            subprocess.Popen([sys.executable, str(script_path)])
            icon = self.app_metadata.get(name, "📦")
            print(f"✓ Launched {name} as separate process")
        except Exception as e:
            print(f"✗ Failed to launch {name} as separate process: {e}")
            self.show_placeholder_window(name)
    
    def launch_file_shortcut(self, name):
        """Launch a file shortcut in appropriate YouOS program"""
        if not hasattr(self, 'file_shortcuts') or name not in self.file_shortcuts:
            return
        
        file_path = self.file_shortcuts[name]
        
        try:
            import os
            from pathlib import Path
            
            if not os.path.exists(file_path):
                self.show_error("File Not Found", f"The file or directory '{file_path}' no longer exists.")
                return
            
            # Handle directories - open in File Manager
            if os.path.isdir(file_path):
                self.launch_file_manager_with_path(file_path)
            else:
                # Handle files based on extension
                ext = Path(file_path).suffix.lower()
                
                # Image, video, audio files - open in Media Viewer
                if ext in ['.jpg', '.jpeg', '.png', '.gif', '.bmp', '.webp', '.mp4', '.avi', '.mkv', '.mov', '.mp3', '.wav', '.ogg', '.flac']:
                    self.launch_media_viewer_with_file(file_path)
                # Text files - open in Text Editor
                elif ext in ['.txt', '.py', '.js', '.html', '.css', '.json', '.xml', '.md']:
                    self.launch_text_editor_with_file(file_path)
                else:
                    # Default - open in File Manager at file location
                    parent_dir = os.path.dirname(file_path)
                    self.launch_file_manager_with_path(parent_dir)
                    
        except Exception as e:
            self.show_error("Launch Error", f"Failed to open '{name}': {str(e)}")
    
    def launch_file_manager_with_path(self, path):
        """Launch File Manager with specific path"""
        try:
            module_path = BASE_DIR / "file_manager.py"
            if not module_path.exists():
                self.show_placeholder_window("File Manager")
                return
            
            import importlib.util
            spec = importlib.util.spec_from_file_location("file_manager", module_path)
            module = importlib.util.module_from_spec(spec)
            
            original_path = sys.path.copy()
            try:
                spec.loader.exec_module(module)
            finally:
                sys.path = original_path
            
            # Create File Manager instance
            file_manager = module.FileManager()
            file_manager.current_dir = path
            file_manager.load_directory(path)
            
            # Create window
            window_id = self.window_manager.create_window("File Manager", "📁", file_manager)
            self.update_taskbar()
            
        except Exception as e:
            print(f"✗ Failed to launch File Manager: {e}")
            self.show_placeholder_window("File Manager")
    
    def launch_media_viewer_with_file(self, file_path):
        """Launch Media Viewer with specific file"""
        try:
            module_path = BASE_DIR / "media_viewer.py"
            if not module_path.exists():
                self.show_placeholder_window("Media Viewer")
                return
            
            import importlib.util
            spec = importlib.util.spec_from_file_location("media_viewer", module_path)
            module = importlib.util.module_from_spec(spec)
            
            original_path = sys.path.copy()
            try:
                spec.loader.exec_module(module)
            finally:
                sys.path = original_path
            
            # Use the launch_media_viewer function
            viewer = module.launch_media_viewer(None, file_path)
            
            # Create window with the viewer
            window_id = self.window_manager.create_window("Media Viewer", "🖼️", viewer)
            self.update_taskbar()
            
        except Exception as e:
            print(f"✗ Failed to launch Media Viewer: {e}")
            # Fallback: open parent directory in file manager
            parent_dir = os.path.dirname(file_path)
            self.launch_file_manager_with_path(parent_dir)
    
    def launch_text_editor_with_file(self, file_path):
        """Launch Text Editor with specific file"""
        try:
            module_path = BASE_DIR / "text_editor.py"
            if not module_path.exists():
                self.show_placeholder_window("Text Editor")
                return
            
            import importlib.util
            spec = importlib.util.spec_from_file_location("text_editor", module_path)
            module = importlib.util.module_from_spec(spec)
            
            original_path = sys.path.copy()
            try:
                spec.loader.exec_module(module)
            finally:
                sys.path = original_path
            
            # Create Text Editor instance
            text_editor = module.TextEditor()
            text_editor.open_file(file_path)
            
            # Create window
            window_id = self.window_manager.create_window("Text Editor", "📝", text_editor)
            self.update_taskbar()
            
        except Exception as e:
            print(f"✗ Failed to launch Text Editor: {e}")
            self.show_placeholder_window("Text Editor")
        """Launch Task Manager"""
        try:
            # Import task manager from correct path
            task_manager_path = BASE_DIR / 'task_manager.py'
            if not task_manager_path.exists():
                raise ImportError("task_manager.py not found")
            
            import importlib.util
            spec = importlib.util.spec_from_file_location("task_manager_module", task_manager_path)
            task_manager_module = importlib.util.module_from_spec(spec)
            
            original_path = sys.path.copy()
            try:
                spec.loader.exec_module(task_manager_module)
            finally:
                sys.path = original_path
            
            TaskManager = task_manager_module.TaskManager
            
            # Create task manager widget with different parameter combinations
            task_manager = None
            try:
                task_manager = TaskManager(desktop_manager=self, is_standalone=False)
            except TypeError:
                try:
                    task_manager = TaskManager(desktop_manager=self)
                except TypeError:
                    try:
                        task_manager = TaskManager(is_standalone=False)
                    except TypeError:
                        task_manager = TaskManager()
            
            # Create window
            window_id = self.window_manager.create_window("Task Manager", "📊", task_manager)
            self.update_taskbar()
            
            print("✓ Task Manager opened in window")
            
        except Exception as e:
            print(f"✗ Failed to open Task Manager: {e}")
            import traceback
            traceback.print_exc()
            self.show_placeholder_window("Task Manager")
    
    def show_placeholder_window(self, name):
        """Show a placeholder window for programs not yet implemented"""
        placeholder = QWidget()
        layout = QVBoxLayout(placeholder)
        layout.setAlignment(Qt.AlignmentFlag.AlignCenter)
        
        icon = self.app_metadata.get(name, "📦")
        icon_label = QLabel(icon)
        icon_label.setStyleSheet("font-size: 72px;")
        icon_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        layout.addWidget(icon_label)
        
        title = QLabel(name)
        title.setStyleSheet("color: white; font-size: 24px; font-weight: bold;")
        title.setAlignment(Qt.AlignmentFlag.AlignCenter)
        layout.addWidget(title)
        
        layout.addSpacing(20)
        
        message = QLabel("This application is not yet implemented or not available.")
        message.setStyleSheet("color: #9ca3af; font-size: 14px;")
        message.setAlignment(Qt.AlignmentFlag.AlignCenter)
        message.setWordWrap(True)
        layout.addWidget(message)
        
        window_id = self.window_manager.create_window(name, icon, placeholder)
        self.update_taskbar()
    
    def start_whatsapp_service(self):
        """Start WhatsApp background notification service"""
        try:
            import psutil
            
            # Check if service is already running
            for proc in psutil.process_iter(['pid', 'name', 'cmdline']):
                try:
                    if proc.info['cmdline']:
                        cmdline = ' '.join(proc.info['cmdline'])
                        if 'whatsapp_service.py' in cmdline:
                            print("WhatsApp service already running")
                            return
                except (psutil.NoSuchProcess, psutil.AccessDenied):
                    continue
            
            # Start service
            service_path = PROGRAMS_DIR / 'whatsapp' / 'whatsapp_service.py'
            if service_path.exists():
                import subprocess
                subprocess.Popen([sys.executable, str(service_path)], 
                               stdout=subprocess.DEVNULL, 
                               stderr=subprocess.DEVNULL)
                print("Started WhatsApp notification service")
        except Exception as e:
            print(f"Failed to start WhatsApp service: {e}")
    
    def init_sports_notifications(self):
        """Sports notifications removed"""
        pass
    
    def setup_sports_notifications(self):
        """Sports notifications removed"""
        pass
    
    def on_match_starting(self, match_data):
        """Sports notifications removed"""
        pass
    
    def on_score_update(self, match_data):
        """Sports notifications removed"""
        pass
    
    def show_pin_score_dialog(self, match_data):
        """Sports notifications removed"""
        pass
    
    def pin_live_score(self, match_data):
        """Sports notifications removed"""
        pass
    
    def handle_notification_click(self, notification_type):
        """Handle notification click events"""
        if notification_type == 'update':
            # Start update installation
            if self.update_manager and self.update_manager.pending_updates:
                self.update_manager.install_updates()
            else:
                self.add_notification(
                    "No Updates",
                    "No pending updates to install",
                    "ℹ️",
                    "Now"
                )
    
    def initialize_updates(self):
        """Initialize system update manager"""
        try:
            from sysupdate import UpdateManager
            self.update_manager = UpdateManager(self)
            print("✅ System update manager initialized")
        except Exception as e:
            print(f"❌ Failed to initialize update manager: {e}")
    
    def check_for_updates(self):
        """Manually check for system updates"""
        if self.update_manager:
            # Trigger immediate check
            updates = self.update_manager.update_checker.check_for_updates()
            if updates:
                self.update_manager.on_updates_available(updates)
            else:
                self.add_notification(
                    "System Up to Date",
                    "YouOS is running the latest version",
                    "✅",
                    "Now"
                )
        else:
            self.add_notification(
                "Update Service Unavailable",
                "Update manager is not initialized",
                "❌",
                "Now"
            )
    
    def install_system_updates(self):
        """Install pending system updates"""
        if self.update_manager and self.update_manager.pending_updates:
            self.update_manager.install_updates()
        else:
            self.add_notification(
                "No Updates Available",
                "There are no pending updates to install",
                "ℹ️",
                "Now"
            )
    
    def closeEvent(self, event):
        """Clean up resources on close"""
        if self.update_manager:
            self.update_manager.stop()
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
