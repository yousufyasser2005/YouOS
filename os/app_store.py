"""
YouOS App Store - PyQt6 Version with Update Detection
Integrated app store for downloading, managing, and updating programs
"""

import os
import json
import requests
import shutil
import threading
import subprocess
import sys
from pathlib import Path
from datetime import datetime
from PyQt6.QtWidgets import (QWidget, QVBoxLayout, QHBoxLayout, QLabel, 
                              QPushButton, QLineEdit, QScrollArea, QFrame,
                              QGridLayout, QComboBox, QTextEdit, QProgressBar,
                              QMessageBox, QApplication)
from PyQt6.QtCore import Qt, QTimer, pyqtSignal, QThread
from PyQt6.QtGui import QPixmap, QIcon

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

# GitHub repository info
GITHUB_REPO = "yousufyasser2005/Youstore"
GITHUB_API_URL = f"https://api.github.com/repos/{GITHUB_REPO}/contents"
GITHUB_RAW_URL = f"https://raw.githubusercontent.com/{GITHUB_REPO}/main"

PROGRAMS_DIR = Path("/home/yousuf-yasser-elshaer/codes/os/programs")
STORE_DATA_FILE = BASE_DIR / "store_data.json"


def compare_versions(v1, v2):
    """Compare two version strings. Returns: -1 if v1 < v2, 0 if equal, 1 if v1 > v2"""
    def parse_version(v):
        v = v.strip().lstrip('v')
        parts = []
        for part in v.split('.'):
            try:
                parts.append(int(part.split('-')[0]))
            except ValueError:
                parts.append(0)
        while len(parts) < 3:
            parts.append(0)
        return parts[:3]
    
    try:
        p1 = parse_version(v1)
        p2 = parse_version(v2)
        
        for a, b in zip(p1, p2):
            if a < b:
                return -1
            elif a > b:
                return 1
        return 0
    except:
        return 0


class DownloadThread(QThread):
    """Thread for downloading and installing apps"""
    
    progress = pyqtSignal(int)
    status = pyqtSignal(str)
    finished = pyqtSignal(bool, str)
    
    def __init__(self, app_name, github_path, install_path, is_update=False):
        super().__init__()
        self.app_name = app_name
        self.github_path = github_path
        self.install_path = install_path
        self.is_update = is_update
    
    def run(self):
        try:
            action = "Updating" if self.is_update else "Downloading"
            self.status.emit(f"{action} {self.app_name}...")
            
            os.makedirs(self.install_path, exist_ok=True)
            
            files_url = f"{GITHUB_API_URL}/{self.github_path}"
            response = requests.get(files_url)
            response.raise_for_status()
            files = response.json()
            
            total_files = len(files)
            for i, file_info in enumerate(files):
                if file_info['type'] == 'file':
                    file_url = file_info['download_url']
                    file_name = file_info['name']
                    file_path = self.install_path / file_name
                    
                    file_response = requests.get(file_url)
                    file_response.raise_for_status()
                    
                    with open(file_path, 'wb') as f:
                        f.write(file_response.content)
                    
                    progress_percent = int((i + 1) / total_files * 100)
                    self.progress.emit(progress_percent)
            
            app_info = {
                'name': self.app_name,
                'install_date': datetime.now().strftime('%Y-%m-%d %H:%M:%S'),
                'source': 'YouStore',
                'last_update': datetime.now().strftime('%Y-%m-%d %H:%M:%S') if self.is_update else None
            }
            
            with open(self.install_path / 'app_info.json', 'w') as f:
                json.dump(app_info, f, indent=4)
            
            action_past = "updated" if self.is_update else "installed"
            self.finished.emit(True, f"{self.app_name} {action_past} successfully!")
            
        except Exception as e:
            action = "update" if self.is_update else "install"
            self.finished.emit(False, f"Failed to {action} {self.app_name}: {str(e)}")


class AppCard(GlassFrame):
    """Card widget for displaying an app"""
    
    install_clicked = pyqtSignal(dict)
    uninstall_clicked = pyqtSignal(dict)
    launch_clicked = pyqtSignal(dict)
    update_clicked = pyqtSignal(dict)
    
    def __init__(self, app_data, is_installed=False, has_update=False, parent=None):
        super().__init__(parent, opacity=0.15)
        self.app_data = app_data
        self.is_installed = is_installed
        self.has_update = has_update
        self.setup_ui()
    
    def setup_ui(self):
        layout = QVBoxLayout(self)
        layout.setContentsMargins(20, 20, 20, 20)
        layout.setSpacing(10)
        
        header_layout = QHBoxLayout()
        
        icon_label = QLabel(self.app_data.get('icon', '📦'))
        icon_label.setStyleSheet("font-size: 48px;")
        header_layout.addWidget(icon_label)
        
        info_layout = QVBoxLayout()
        name_label = QLabel(self.app_data['name'])
        name_label.setStyleSheet(f"color: {COLORS['text_primary']}; font-size: 18px; font-weight: bold;")
        info_layout.addWidget(name_label)
        
        version_text = f"v{self.app_data.get('version', '1.0')} • {self.app_data.get('category', 'Utilities')}"
        if self.has_update:
            version_text += " • 🔄 Update Available"
        
        version_label = QLabel(version_text)
        version_label.setStyleSheet(f"color: {COLORS['warning'] if self.has_update else COLORS['text_secondary']}; font-size: 12px;")
        info_layout.addWidget(version_label)
        
        header_layout.addLayout(info_layout)
        header_layout.addStretch()
        layout.addLayout(header_layout)
        
        desc_label = QLabel(self.app_data.get('description', 'No description available'))
        desc_label.setWordWrap(True)
        desc_label.setStyleSheet(f"color: {COLORS['text_secondary']}; font-size: 13px;")
        layout.addWidget(desc_label)
        
        meta_label = QLabel(f"By {self.app_data.get('author', 'Unknown')} • {self.app_data.get('size', 'Unknown size')}")
        meta_label.setStyleSheet(f"color: {COLORS['text_secondary']}; font-size: 11px;")
        layout.addWidget(meta_label)
        
        button_layout = QHBoxLayout()
        button_layout.addStretch()
        
        if self.is_installed:
            if self.has_update:
                update_btn = QPushButton("🔄 Update")
                update_btn.setFixedHeight(35)
                update_btn.setCursor(Qt.CursorShape.PointingHandCursor)
                update_btn.setStyleSheet(f"""
                    QPushButton {{
                        background: {COLORS['warning']};
                        color: white;
                        border: none;
                        border-radius: 8px;
                        font-size: 13px;
                        font-weight: bold;
                        padding: 0 20px;
                    }}
                    QPushButton:hover {{ background: #f59e0b; }}
                """)
                update_btn.clicked.connect(lambda: self.update_clicked.emit(self.app_data))
                button_layout.addWidget(update_btn)
            
            launch_btn = QPushButton("🚀 Launch")
            launch_btn.setFixedHeight(35)
            launch_btn.setCursor(Qt.CursorShape.PointingHandCursor)
            launch_btn.setStyleSheet(f"""
                QPushButton {{
                    background: {COLORS['accent_primary']};
                    color: white;
                    border: none;
                    border-radius: 8px;
                    font-size: 13px;
                    font-weight: bold;
                    padding: 0 20px;
                }}
                QPushButton:hover {{ background: {COLORS['accent_hover']}; }}
            """)
            launch_btn.clicked.connect(lambda: self.launch_clicked.emit(self.app_data))
            button_layout.addWidget(launch_btn)
            
            uninstall_btn = QPushButton("🗑️ Uninstall")
            uninstall_btn.setFixedHeight(35)
            uninstall_btn.setCursor(Qt.CursorShape.PointingHandCursor)
            uninstall_btn.setStyleSheet(f"""
                QPushButton {{
                    background: {COLORS['error']};
                    color: white;
                    border: none;
                    border-radius: 8px;
                    font-size: 13px;
                    font-weight: bold;
                    padding: 0 20px;
                }}
                QPushButton:hover {{ background: #dc2626; }}
            """)
            uninstall_btn.clicked.connect(lambda: self.uninstall_clicked.emit(self.app_data))
            button_layout.addWidget(uninstall_btn)
        else:
            install_btn = QPushButton("📥 Install")
            install_btn.setFixedHeight(35)
            install_btn.setCursor(Qt.CursorShape.PointingHandCursor)
            install_btn.setStyleSheet(f"""
                QPushButton {{
                    background: {COLORS['success']};
                    color: white;
                    border: none;
                    border-radius: 8px;
                    font-size: 13px;
                    font-weight: bold;
                    padding: 0 20px;
                }}
                QPushButton:hover {{ background: #059669; }}
            """)
            install_btn.clicked.connect(lambda: self.install_clicked.emit(self.app_data))
            button_layout.addWidget(install_btn)
        
        layout.addLayout(button_layout)


class AppStoreWindow(GlassFrame):
    """Main App Store Window"""
    
    closed = pyqtSignal()
    app_installed = pyqtSignal(str)
    notification_requested = pyqtSignal(str, str, str)  # title, message, icon
    
    def __init__(self, parent=None):
        super().__init__(parent, opacity=0.20)
        self.setFixedSize(900, 700)
        self.available_apps = []
        self.installed_apps = {}
        self.download_thread = None
        self.updates_available = {}
        self.setup_ui()
        self.load_data()
    
    def setup_ui(self):
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(0)
        
        header = self.create_header()
        layout.addWidget(header)
        
        tab_container = QFrame()
        tab_container.setStyleSheet(f"background: {COLORS['bg_secondary']};")
        tab_layout = QHBoxLayout(tab_container)
        tab_layout.setContentsMargins(20, 10, 20, 10)
        
        self.browse_btn = QPushButton("🔍 Browse Apps")
        self.browse_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        self.browse_btn.setCheckable(True)
        self.browse_btn.setChecked(True)
        self.browse_btn.clicked.connect(lambda: self.switch_tab(0))
        
        self.updates_btn = QPushButton("🔄 Updates")
        self.updates_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        self.updates_btn.setCheckable(True)
        self.updates_btn.clicked.connect(lambda: self.switch_tab(1))
        
        self.installed_btn = QPushButton("✅ Installed")
        self.installed_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        self.installed_btn.setCheckable(True)
        self.installed_btn.clicked.connect(lambda: self.switch_tab(2))
        
        tab_style = f"""
            QPushButton {{
                background: transparent;
                color: {COLORS['text_secondary']};
                border: none;
                padding: 10px 20px;
                font-size: 14px;
                font-weight: bold;
            }}
            QPushButton:hover {{ color: {COLORS['text_primary']}; }}
            QPushButton:checked {{
                color: {COLORS['accent_primary']};
                border-bottom: 2px solid {COLORS['accent_primary']};
            }}
        """
        self.browse_btn.setStyleSheet(tab_style)
        self.updates_btn.setStyleSheet(tab_style)
        self.installed_btn.setStyleSheet(tab_style)
        
        tab_layout.addWidget(self.browse_btn)
        tab_layout.addWidget(self.updates_btn)
        tab_layout.addWidget(self.installed_btn)
        tab_layout.addStretch()
        
        self.update_badge = QLabel()
        self.update_badge.setStyleSheet(f"""
            background: {COLORS['error']};
            color: white;
            border-radius: 10px;
            padding: 2px 8px;
            font-size: 11px;
            font-weight: bold;
        """)
        self.update_badge.hide()
        tab_layout.addWidget(self.update_badge)
        
        layout.addWidget(tab_container)
        
        self.content_stack = QFrame()
        content_layout = QVBoxLayout(self.content_stack)
        content_layout.setContentsMargins(0, 0, 0, 0)
        
        self.browse_widget = self.create_browse_tab()
        content_layout.addWidget(self.browse_widget)
        
        self.updates_widget = self.create_updates_tab()
        self.updates_widget.hide()
        content_layout.addWidget(self.updates_widget)
        
        self.installed_widget = self.create_installed_tab()
        self.installed_widget.hide()
        content_layout.addWidget(self.installed_widget)
        
        layout.addWidget(self.content_stack, stretch=1)
        
        self.status_bar = self.create_status_bar()
        layout.addWidget(self.status_bar)
    
    def create_header(self):
        header = QFrame()
        header.setFixedHeight(80)
        header.setStyleSheet(f"background: {COLORS['accent_primary']};")
        
        layout = QHBoxLayout(header)
        layout.setContentsMargins(30, 20, 30, 20)
        
        title = QLabel("📱 YouOS App Store")
        title.setStyleSheet("color: white; font-size: 24px; font-weight: bold;")
        layout.addWidget(title)
        
        layout.addStretch()
        
        check_updates_btn = QPushButton("🔄 Check Updates")
        check_updates_btn.setFixedHeight(40)
        check_updates_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        check_updates_btn.setStyleSheet("""
            QPushButton {
                background: rgba(255, 255, 255, 0.2);
                color: white;
                border: none;
                border-radius: 8px;
                padding: 0 20px;
                font-size: 13px;
                font-weight: bold;
            }
            QPushButton:hover { background: rgba(255, 255, 255, 0.3); }
        """)
        check_updates_btn.clicked.connect(self.check_for_updates)
        layout.addWidget(check_updates_btn)
        
        refresh_btn = QPushButton("🔄 Refresh")
        refresh_btn.setFixedHeight(40)
        refresh_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        refresh_btn.setStyleSheet("""
            QPushButton {
                background: rgba(255, 255, 255, 0.2);
                color: white;
                border: none;
                border-radius: 8px;
                padding: 0 20px;
                font-size: 13px;
                font-weight: bold;
            }
            QPushButton:hover { background: rgba(255, 255, 255, 0.3); }
        """)
        refresh_btn.clicked.connect(self.refresh_store)
        layout.addWidget(refresh_btn)
        
        close_btn = QPushButton("✕")
        close_btn.setFixedSize(40, 40)
        close_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        close_btn.setStyleSheet("""
            QPushButton {
                background: rgba(255, 255, 255, 0.2);
                color: white;
                border: none;
                border-radius: 20px;
                font-size: 18px;
                font-weight: bold;
            }
            QPushButton:hover { background: rgba(239, 68, 68, 0.8); }
        """)
        close_btn.clicked.connect(self.close)
        layout.addWidget(close_btn)
        
        return header
    
    def create_browse_tab(self):
        widget = QFrame()
        layout = QVBoxLayout(widget)
        layout.setContentsMargins(20, 20, 20, 20)
        
        search_layout = QHBoxLayout()
        self.search_input = QLineEdit()
        self.search_input.setPlaceholderText("Search apps...")
        self.search_input.setFixedHeight(40)
        self.search_input.setStyleSheet(f"""
            QLineEdit {{
                background: {COLORS['bg_tertiary']};
                border: 1px solid {COLORS['border']};
                border-radius: 8px;
                color: {COLORS['text_primary']};
                padding: 0 15px;
                font-size: 14px;
            }}
            QLineEdit:focus {{ border: 1px solid {COLORS['accent_primary']}; }}
        """)
        self.search_input.textChanged.connect(self.filter_apps)
        search_layout.addWidget(self.search_input)
        layout.addLayout(search_layout)
        
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setStyleSheet("QScrollArea { border: none; background: transparent; }")
        
        self.browse_container = QWidget()
        self.browse_layout = QVBoxLayout(self.browse_container)
        self.browse_layout.setSpacing(15)
        self.browse_layout.addStretch()
        
        scroll.setWidget(self.browse_container)
        layout.addWidget(scroll)
        
        return widget
    
    def create_updates_tab(self):
        widget = QFrame()
        layout = QVBoxLayout(widget)
        layout.setContentsMargins(20, 20, 20, 20)
        
        update_all_layout = QHBoxLayout()
        self.update_all_btn = QPushButton("🔄 Update All Apps")
        self.update_all_btn.setFixedHeight(45)
        self.update_all_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        self.update_all_btn.setStyleSheet(f"""
            QPushButton {{
                background: {COLORS['warning']};
                color: white;
                border: none;
                border-radius: 8px;
                font-size: 14px;
                font-weight: bold;
            }}
            QPushButton:hover {{ background: #f59e0b; }}
            QPushButton:disabled {{
                background: {COLORS['bg_tertiary']};
                color: {COLORS['text_secondary']};
            }}
        """)
        self.update_all_btn.clicked.connect(self.update_all_apps)
        update_all_layout.addWidget(self.update_all_btn)
        layout.addLayout(update_all_layout)
        
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setStyleSheet("QScrollArea { border: none; background: transparent; }")
        
        self.updates_container = QWidget()
        self.updates_layout = QVBoxLayout(self.updates_container)
        self.updates_layout.setSpacing(15)
        self.updates_layout.addStretch()
        
        scroll.setWidget(self.updates_container)
        layout.addWidget(scroll)
        
        return widget
    
    def create_installed_tab(self):
        widget = QFrame()
        layout = QVBoxLayout(widget)
        layout.setContentsMargins(20, 20, 20, 20)
        
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setStyleSheet("QScrollArea { border: none; background: transparent; }")
        
        self.installed_container = QWidget()
        self.installed_layout = QVBoxLayout(self.installed_container)
        self.installed_layout.setSpacing(15)
        self.installed_layout.addStretch()
        
        scroll.setWidget(self.installed_container)
        layout.addWidget(scroll)
        
        return widget
    
    def create_status_bar(self):
        status_bar = QFrame()
        status_bar.setFixedHeight(50)
        status_bar.setStyleSheet(f"background: {COLORS['bg_secondary']};")
        
        layout = QHBoxLayout(status_bar)
        layout.setContentsMargins(20, 10, 20, 10)
        
        self.status_label = QLabel("Ready")
        self.status_label.setStyleSheet(f"color: {COLORS['text_secondary']}; font-size: 12px;")
        layout.addWidget(self.status_label)
        
        layout.addStretch()
        
        self.progress_bar = QProgressBar()
        self.progress_bar.setFixedWidth(200)
        self.progress_bar.setFixedHeight(20)
        self.progress_bar.setStyleSheet(f"""
            QProgressBar {{
                border: 1px solid {COLORS['border']};
                border-radius: 4px;
                background: {COLORS['bg_tertiary']};
                text-align: center;
                color: {COLORS['text_primary']};
            }}
            QProgressBar::chunk {{
                background: {COLORS['accent_primary']};
                border-radius: 3px;
            }}
        """)
        self.progress_bar.hide()
        layout.addWidget(self.progress_bar)
        
        return status_bar
    
    def switch_tab(self, index):
        self.browse_btn.setChecked(index == 0)
        self.updates_btn.setChecked(index == 1)
        self.installed_btn.setChecked(index == 2)
        
        self.browse_widget.setVisible(index == 0)
        self.updates_widget.setVisible(index == 1)
        self.installed_widget.setVisible(index == 2)
        
        if index == 1:
            self.refresh_updates_tab()
        elif index == 2:
            self.refresh_installed()
    
    def load_data(self):
        self.scan_installed_apps()
        self.refresh_store()
        # Auto-check for updates when store opens
        QTimer.singleShot(3000, self.check_for_updates)  # Check after 3 seconds
    
    def scan_installed_apps(self):
        self.installed_apps.clear()
        
        if PROGRAMS_DIR.exists():
            for item in PROGRAMS_DIR.iterdir():
                if item.is_dir():
                    metadata_file = item / "metadata.json"
                    if metadata_file.exists():
                        try:
                            with open(metadata_file, 'r') as f:
                                metadata = json.load(f)
                                app_name = item.name
                                version = metadata.get('version', '1.0.0')
                                self.installed_apps[app_name] = version
                        except:
                            self.installed_apps[item.name] = '1.0.0'
                    else:
                        self.installed_apps[item.name] = '1.0.0'
    
    def check_for_updates(self):
        play_sound("click.wav")
        self.status_label.setText("Checking for updates...")
        self.updates_available.clear()
        
        for app_name, installed_version in self.installed_apps.items():
            for app in self.available_apps:
                if app.get('github_path') == app_name:
                    available_version = app.get('version', '1.0.0')
                    if compare_versions(available_version, installed_version) > 0:
                        self.updates_available[app_name] = available_version
                    break
        
        update_count = len(self.updates_available)
        if update_count > 0:
            self.status_label.setText(f"{update_count} update(s) available!")
            self.update_badge.setText(str(update_count))
            self.update_badge.show()
            play_sound("notification.wav")
            
            # Send notification about updates
            if update_count == 1:
                app_name = list(self.updates_available.keys())[0]
                message = f"Update available for {app_name}"
            else:
                message = f"{update_count} app updates available"
            
            self.notification_requested.emit("App Store", message, "🏪")
        else:
            self.status_label.setText("All apps are up to date!")
            self.update_badge.hide()
            play_sound("success.wav")
        
        self.refresh_browse_tab()
        self.refresh_updates_tab()
    
    def refresh_store(self):
        self.status_label.setText("Loading apps from YouStore...")
        play_sound("click.wav")
        
        try:
            response = requests.get(GITHUB_API_URL, timeout=10)
            response.raise_for_status()
            contents = response.json()
            
            self.available_apps = []
            for item in contents:
                if item['type'] == 'dir':
                    app_name = item['name']
                    try:
                        metadata_url = f"{GITHUB_RAW_URL}/{app_name}/metadata.json"
                        meta_response = requests.get(metadata_url, timeout=5)
                        if meta_response.status_code == 200:
                            metadata = meta_response.json()
                            metadata['github_path'] = app_name
                            self.available_apps.append(metadata)
                        else:
                            self.available_apps.append({
                                'name': app_name,
                                'description': f'{app_name} application',
                                'version': '1.0',
                                'author': 'Unknown',
                                'category': 'Utilities',
                                'size': 'Unknown',
                                'icon': '📦',
                                'github_path': app_name
                            })
                    except:
                        pass
            
            self.check_for_updates()
            self.refresh_browse_tab()
            self.status_label.setText(f"Found {len(self.available_apps)} apps")
            play_sound("success.wav")
            
        except Exception as e:
            self.status_label.setText(f"Failed to load apps: {str(e)}")
            play_sound("error.wav")
    
    def refresh_browse_tab(self):
        for i in reversed(range(self.browse_layout.count())):
            widget = self.browse_layout.itemAt(i).widget()
            if widget:
                widget.deleteLater()
        
        for app in self.available_apps:
            app_name = app['github_path']
            is_installed = app_name in self.installed_apps
            has_update = app_name in self.updates_available
            
            card = AppCard(app, is_installed, has_update)
            card.install_clicked.connect(self.install_app)
            card.uninstall_clicked.connect(self.uninstall_app)
            card.launch_clicked.connect(self.launch_app)
            card.update_clicked.connect(self.update_app)
            self.browse_layout.insertWidget(self.browse_layout.count() - 1, card)
    
    def refresh_updates_tab(self):
        for i in reversed(range(self.updates_layout.count())):
            widget = self.updates_layout.itemAt(i).widget()
            if widget:
                widget.deleteLater()
        
        if not self.updates_available:
            no_updates = QLabel("🎉 All apps are up to date!")
            no_updates.setAlignment(Qt.AlignmentFlag.AlignCenter)
            no_updates.setStyleSheet(f"color: {COLORS['text_secondary']}; font-size: 16px;")
            self.updates_layout.insertWidget(0, no_updates)
            self.update_all_btn.setEnabled(False)
        else:
            self.update_all_btn.setEnabled(True)
            for app in self.available_apps:
                if app['github_path'] in self.updates_available:
                    card = AppCard(app, True, True)
                    card.update_clicked.connect(self.update_app)
                    card.launch_clicked.connect(self.launch_app)
                    card.uninstall_clicked.connect(self.uninstall_app)
                    self.updates_layout.insertWidget(self.updates_layout.count() - 1, card)
    
    def refresh_installed(self):
        for i in reversed(range(self.installed_layout.count())):
            widget = self.installed_layout.itemAt(i).widget()
            if widget:
                widget.deleteLater()
        
        self.scan_installed_apps()
        
        installed_count = 0
        for app in self.available_apps:
            if app['github_path'] in self.installed_apps:
                has_update = app['github_path'] in self.updates_available
                card = AppCard(app, True, has_update)
                card.uninstall_clicked.connect(self.uninstall_app)
                card.launch_clicked.connect(self.launch_app)
                card.update_clicked.connect(self.update_app)
                self.installed_layout.insertWidget(self.installed_layout.count() - 1, card)
                installed_count += 1
        
        if installed_count == 0:
            no_apps = QLabel("No apps installed yet.\nBrowse the store to install apps!")
            no_apps.setAlignment(Qt.AlignmentFlag.AlignCenter)
            no_apps.setStyleSheet(f"color: {COLORS['text_secondary']}; font-size: 16px;")
            self.installed_layout.insertWidget(0, no_apps)
    
    def filter_apps(self):
        query = self.search_input.text().lower()
        
        for i in range(self.browse_layout.count() - 1):
            widget = self.browse_layout.itemAt(i).widget()
            if isinstance(widget, AppCard):
                app_name = widget.app_data['name'].lower()
                app_desc = widget.app_data.get('description', '').lower()
                app_category = widget.app_data.get('category', '').lower()
                
                if query in app_name or query in app_desc or query in app_category:
                    widget.show()
                else:
                    widget.hide()
    
    def install_app(self, app_data):
        if self.download_thread and self.download_thread.isRunning():
            QMessageBox.warning(self, "Download in Progress", "Please wait for current download to finish.")
            return
        
        play_sound("click.wav")
        
        app_name = app_data['github_path']
        install_path = PROGRAMS_DIR / app_name
        
        if install_path.exists():
            QMessageBox.information(self, "Already Installed", f"{app_data['name']} is already installed.")
            return
        
        self.download_thread = DownloadThread(app_data['name'], app_name, install_path, False)
        self.download_thread.progress.connect(self.update_progress)
        self.download_thread.status.connect(self.update_status)
        self.download_thread.finished.connect(self.install_finished)
        
        self.progress_bar.show()
        self.progress_bar.setValue(0)
        self.download_thread.start()
    
    def update_app(self, app_data):
        if self.download_thread and self.download_thread.isRunning():
            QMessageBox.warning(self, "Download in Progress", "Please wait for current download to finish.")
            return
        
        play_sound("click.wav")
        
        app_name = app_data['github_path']
        install_path = PROGRAMS_DIR / app_name
        
        self.download_thread = DownloadThread(app_data['name'], app_name, install_path, True)
        self.download_thread.progress.connect(self.update_progress)
        self.download_thread.status.connect(self.update_status)
        self.download_thread.finished.connect(self.update_finished)
        
        self.progress_bar.show()
        self.progress_bar.setValue(0)
        self.download_thread.start()
    
    def update_all_apps(self):
        if not self.updates_available:
            return
        
        reply = QMessageBox.question(
            self,
            "Update All Apps",
            f"Update {len(self.updates_available)} app(s)?",
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No
        )
        
        if reply == QMessageBox.StandardButton.Yes:
            for app in self.available_apps:
                if app['github_path'] in self.updates_available:
                    self.update_app(app)
                    break
    
    def update_progress(self, value):
        self.progress_bar.setValue(value)
    
    def update_status(self, message):
        self.status_label.setText(message)
    
    def install_finished(self, success, message):
        self.progress_bar.hide()
        
        if success:
            play_sound("success.wav")
            QMessageBox.information(self, "Installation Complete", message)
            self.scan_installed_apps()
            self.check_for_updates()
            self.refresh_browse_tab()
            
            app_name = message.split(" installed")[0]
            self.app_installed.emit(app_name)
        else:
            play_sound("error.wav")
            QMessageBox.critical(self, "Installation Failed", message)
        
        self.status_label.setText("Ready")
    
    def update_finished(self, success, message):
        self.progress_bar.hide()
        
        if success:
            play_sound("success.wav")
            QMessageBox.information(self, "Update Complete", message)
            self.scan_installed_apps()
            self.check_for_updates()
            self.refresh_browse_tab()
            self.refresh_updates_tab()
            
            if self.updates_available:
                reply = QMessageBox.question(
                    self,
                    "More Updates",
                    f"{len(self.updates_available)} more update(s) available.\n\nContinue?",
                    QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No
                )
                if reply == QMessageBox.StandardButton.Yes:
                    self.update_all_apps()
        else:
            play_sound("error.wav")
            QMessageBox.critical(self, "Update Failed", message)
        
        self.status_label.setText("Ready")
    
    def uninstall_app(self, app_data):
        reply = QMessageBox.question(
            self, 
            "Confirm Uninstall",
            f"Uninstall {app_data['name']}?\n\nThis will delete all app files.",
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No
        )
        
        if reply == QMessageBox.StandardButton.Yes:
            try:
                app_path = PROGRAMS_DIR / app_data['github_path']
                if app_path.exists():
                    shutil.rmtree(app_path)
                    play_sound("success.wav")
                    QMessageBox.information(self, "Uninstall Complete", f"{app_data['name']} uninstalled.")
                    self.scan_installed_apps()
                    self.check_for_updates()
                    self.refresh_browse_tab()
                    self.refresh_installed()
                else:
                    QMessageBox.warning(self, "Not Found", "App folder not found.")
            except Exception as e:
                play_sound("error.wav")
                QMessageBox.critical(self, "Uninstall Failed", f"Failed to uninstall: {str(e)}")
    
    def launch_app(self, app_data):
        play_sound("click.wav")
        
        app_path = PROGRAMS_DIR / app_data['github_path']
        
        main_file = None
        for filename in ['main.py', 'app.py', f"{app_data['github_path']}.py"]:
            potential_file = app_path / filename
            if potential_file.exists():
                main_file = potential_file
                break
        
        if main_file:
            try:
                subprocess.Popen([sys.executable, str(main_file)])
                self.status_label.setText(f"Launched {app_data['name']}")
            except Exception as e:
                QMessageBox.critical(self, "Launch Failed", f"Failed to launch: {str(e)}")
        else:
            QMessageBox.warning(self, "Launch Failed", "No executable file found")
    
    def closeEvent(self, event):
        self.closed.emit()
        super().closeEvent(event)


def open_app_store(parent=None):
    """Open the app store window"""
    store = AppStoreWindow(parent)
    store.show()
    return store


if __name__ == "__main__":
    app = QApplication(sys.argv)
    window = AppStoreWindow()
    window.show()
    sys.exit(app.exec())