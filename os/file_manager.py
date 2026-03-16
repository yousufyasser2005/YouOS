import os
import sys
import shutil
import subprocess
import json
import re
from datetime import datetime
from pathlib import Path
from PyQt6.QtWidgets import (QWidget, QVBoxLayout, QHBoxLayout, QLabel, 
                              QPushButton, QTreeWidget, QTreeWidgetItem, 
                              QHeaderView, QMenu, QMessageBox, QSplitter, QApplication)
from PyQt6.QtCore import Qt, QSize
from PyQt6.QtGui import QIcon, QAction

# Setup recycle bin directory
BASE_DIR = Path(__file__).parent
TRASH_DIR = BASE_DIR / "recycle_bin"
TRASH_DIR.mkdir(exist_ok=True)

# Import for playing sound
try:
    import pygame
    pygame.mixer.init()
    SOUND_ENABLED = True
except ImportError:
    print("Warning: pygame not installed. Sound effects disabled.")
    SOUND_ENABLED = False

try:
    from main import GlassWidget
except ImportError:
    from PyQt6.QtWidgets import QWidget as GlassWidget

COLORS = {
    'bg_primary': '#0f0f1e',
    'bg_secondary': '#1a1a2e',
    'bg_tertiary': '#252538',
    'accent_primary': '#3b82f6',
    'text_primary': '#ffffff',
    'text_secondary': '#9ca3af',
    'border': '#374151',
}

IMAGE_EXT = ('.jpg', '.jpeg', '.png', '.gif', '.bmp', '.webp')
VIDEO_EXT = ('.mp4', '.avi', '.mkv', '.mov')
AUDIO_EXT = ('.mp3', '.wav', '.ogg', '.flac')

# Sound file path
QUESTION_SOUND_PATH = str(BASE_DIR / "assets" / "sounds" / "question.wav")


class SoundManager:
    """Handles sound effects"""
    
    def __init__(self):
        self.enabled = SOUND_ENABLED
        self.question_sound = None
        
        if self.enabled:
            self.load_sounds()
    
    def load_sounds(self):
        """Load sound effects"""
        try:
            if os.path.exists(QUESTION_SOUND_PATH):
                self.question_sound = pygame.mixer.Sound(QUESTION_SOUND_PATH)
            else:
                print(f"Warning: Sound file not found at {QUESTION_SOUND_PATH}")
                self.enabled = False
        except Exception as e:
            print(f"Error loading sound: {e}")
            self.enabled = False
    
    def play_question_sound(self):
        """Play question sound effect"""
        if self.enabled and self.question_sound:
            try:
                self.question_sound.play()
            except Exception as e:
                print(f"Error playing sound: {e}")


# Global sound manager instance
sound_manager = SoundManager()


def get_file_properties(filepath):
    """Get file properties"""
    props = {}
    try:
        stat_info = os.stat(filepath)
        props["type"] = os.path.splitext(filepath)[1][1:].upper() or "File"
        props["size"] = stat_info.st_size
        props["modified"] = datetime.fromtimestamp(stat_info.st_mtime).strftime('%Y-%m-%d %H:%M:%S')
    except Exception:
        props = {"type": "N/A", "size": 0, "modified": "N/A"}
    return props


def get_all_block_devices():
    """Get all block devices using lsblk"""
    try:
        output = subprocess.check_output([
            'lsblk', '-o', 'NAME,FSTYPE,SIZE,MOUNTPOINT,TYPE,LABEL', '-J'
        ]).decode()
        data = json.loads(output)
        return data.get('blockdevices', [])
    except Exception as e:
        print(f"Error getting block devices: {e}")
        return []


def format_size(size_bytes):
    """Format size in bytes to human readable"""
    if size_bytes < 1024:
        return f"{size_bytes} B"
    elif size_bytes < 1024 * 1024:
        return f"{size_bytes // 1024} KB"
    elif size_bytes < 1024 * 1024 * 1024:
        return f"{size_bytes // (1024 * 1024)} MB"
    else:
        return f"{size_bytes // (1024 * 1024 * 1024)} GB"


def move_to_recycle_bin(file_path):
    """Move a file to the recycle bin with metadata"""
    try:
        file_path = Path(file_path)
        if not file_path.exists():
            return False, "File does not exist"
        
        # Create metadata file with original path
        filename = file_path.name
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        trash_filename = f"{timestamp}_{filename}"
        trash_path = TRASH_DIR / trash_filename
        metadata_path = TRASH_DIR / f"{trash_filename}.meta"
        
        # Save metadata (original path and deletion time)
        with open(metadata_path, 'w') as f:
            f.write(f"original_path={str(file_path.absolute())}\n")
            f.write(f"deleted_time={datetime.now().isoformat()}\n")
            f.write(f"original_name={filename}\n")
        
        # Move file to trash
        if file_path.is_dir():
            shutil.move(str(file_path), str(trash_path))
        else:
            shutil.move(str(file_path), str(trash_path))
        
        return True, f"Moved to Recycle Bin: {filename}"
    except Exception as e:
        return False, f"Error moving to recycle bin: {str(e)}"


class QuestionMessageBox(QMessageBox):
    """Custom QMessageBox that plays a sound for questions"""
    
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setWindowTitle("Confirm Action")
        self.setIcon(QMessageBox.Icon.Question)
        
    def exec(self):
        """Play sound before showing the dialog"""
        sound_manager.play_question_sound()
        return super().exec()


class FileManager(QWidget):
    """File Manager / My Computer"""
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setWindowTitle("My Computer - File Manager")
        self.resize(1100, 650)
        
        self.setStyleSheet(f"""
            QWidget {{
                background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                    stop:0 rgba(15, 15, 30, 230),
                    stop:1 rgba(26, 26, 46, 230));
                border-radius: 12px;
                color: white;
            }}
            QPushButton {{
                background: {COLORS['bg_tertiary']};
                color: white;
                border: 1px solid {COLORS['border']};
                border-radius: 6px;
                padding: 8px 16px;
                font-size: 13px;
            }}
            QPushButton:hover {{
                background: {COLORS['accent_primary']};
                border-color: {COLORS['accent_primary']};
            }}
            QTreeWidget {{
                background: rgba(26, 26, 46, 0.6);
                border: 1px solid {COLORS['border']};
                border-radius: 8px;
                color: white;
                selection-background-color: {COLORS['accent_primary']};
            }}
            QTreeWidget::item {{
                padding: 5px;
            }}
            QTreeWidget::item:hover {{
                background: rgba(59, 130, 246, 0.2);
            }}
            QHeaderView::section {{
                background: {COLORS['bg_tertiary']};
                color: white;
                padding: 8px;
                border: none;
                border-right: 1px solid {COLORS['border']};
            }}
            QLabel {{
                color: {COLORS['text_secondary']};
            }}
            QMenu {{
                background: {COLORS['bg_secondary']};
                border: 1px solid {COLORS['border']};
                border-radius: 6px;
                padding: 5px;
            }}
            QMenu::item {{
                padding: 8px 25px;
                border-radius: 4px;
            }}
            QMenu::item:selected {{
                background: {COLORS['accent_primary']};
            }}
        """)
        
        self.current_dir = os.path.expanduser("~")
        self.history = [self.current_dir]
        self.history_index = 0
        self.clipboard = None
        self.clipboard_action = None
        
        self.setup_ui()
        self.load_disks()
        self.load_directory(self.current_dir)

    def setup_ui(self):
        """Setup UI layout"""
        layout = QVBoxLayout(self)
        layout.setContentsMargins(15, 15, 15, 15)
        layout.setSpacing(10)
        
        nav_layout = QHBoxLayout()
        
        self.back_btn = QPushButton("← Back")
        self.back_btn.setFixedWidth(80)
        self.back_btn.clicked.connect(self.go_back)
        
        self.forward_btn = QPushButton("Forward →")
        self.forward_btn.setFixedWidth(95)
        self.forward_btn.clicked.connect(self.go_forward)
        
        self.up_btn = QPushButton("↑ Up")
        self.up_btn.setFixedWidth(70)
        self.up_btn.clicked.connect(self.go_up)
        
        self.refresh_btn = QPushButton("🔄 Refresh")
        self.refresh_btn.setFixedWidth(95)
        self.refresh_btn.clicked.connect(self.refresh_directory)
        
        self.path_label = QLabel(self.current_dir)
        self.path_label.setStyleSheet(f"color: {COLORS['text_primary']}; font-size: 13px; padding: 5px;")
        
        nav_layout.addWidget(self.back_btn)
        nav_layout.addWidget(self.forward_btn)
        nav_layout.addWidget(self.up_btn)
        nav_layout.addWidget(self.refresh_btn)
        nav_layout.addWidget(self.path_label, stretch=1)
        
        layout.addLayout(nav_layout)

        splitter = QSplitter(Qt.Orientation.Horizontal)
        
        # Left: Disk Navigation
        left_widget = QWidget()
        left_layout = QVBoxLayout(left_widget)
        left_layout.setContentsMargins(0, 0, 0, 0)
        
        disk_label = QLabel("💾 Disk Navigation")
        disk_label.setStyleSheet(f"color: {COLORS['text_primary']}; font-size: 14px; font-weight: bold; padding: 5px;")
        left_layout.addWidget(disk_label)
        
        self.disk_tree = QTreeWidget()
        self.disk_tree.setHeaderHidden(True)
        self.disk_tree.setMaximumWidth(300)
        self.disk_tree.itemClicked.connect(self.on_disk_select)
        self.disk_tree.setContextMenuPolicy(Qt.ContextMenuPolicy.CustomContextMenu)
        self.disk_tree.customContextMenuRequested.connect(self.show_disk_context_menu)
        left_layout.addWidget(self.disk_tree)
        
        splitter.addWidget(left_widget)

        # Right: File List
        right_widget = QWidget()
        right_layout = QVBoxLayout(right_widget)
        right_layout.setContentsMargins(0, 0, 0, 0)
        
        file_label = QLabel("📁 Files and Folders")
        file_label.setStyleSheet(f"color: {COLORS['text_primary']}; font-size: 14px; font-weight: bold; padding: 5px;")
        right_layout.addWidget(file_label)
        
        self.file_tree = QTreeWidget()
        self.file_tree.setColumnCount(3)
        self.file_tree.setHeaderLabels(["Name", "Size", "Modified"])
        self.file_tree.header().setSectionResizeMode(0, QHeaderView.ResizeMode.Stretch)
        self.file_tree.header().setSectionResizeMode(1, QHeaderView.ResizeMode.ResizeToContents)
        self.file_tree.header().setSectionResizeMode(2, QHeaderView.ResizeMode.ResizeToContents)
        self.file_tree.itemDoubleClicked.connect(self.on_item_double_click)
        self.file_tree.setContextMenuPolicy(Qt.ContextMenuPolicy.CustomContextMenu)
        self.file_tree.customContextMenuRequested.connect(self.show_file_context_menu)
        right_layout.addWidget(self.file_tree)
        
        # Action buttons
        action_layout = QHBoxLayout()
        
        self.copy_btn = QPushButton("📋 Copy")
        self.copy_btn.clicked.connect(self.copy_file)
        
        self.cut_btn = QPushButton("✂ Cut")
        self.cut_btn.clicked.connect(self.cut_file)
        
        self.paste_btn = QPushButton("📌 Paste")
        self.paste_btn.clicked.connect(self.paste_file)
        
        self.delete_btn = QPushButton("🗑 Delete")
        self.delete_btn.clicked.connect(self.delete_file)
        
        for btn in [self.copy_btn, self.cut_btn, self.paste_btn, self.delete_btn]:
            btn.setFixedHeight(35)
            action_layout.addWidget(btn)
        
        action_layout.addStretch()
        right_layout.addLayout(action_layout)
        
        splitter.addWidget(right_widget)
        splitter.setSizes([280, 720])
        
        layout.addWidget(splitter, stretch=1)
        
        # Status bar
        self.status_label = QLabel("Ready")
        self.status_label.setStyleSheet(f"color: {COLORS['text_secondary']}; padding: 5px; font-size: 12px;")
        layout.addWidget(self.status_label)

    def load_disks(self):
        """Load disk devices and quick access"""
        self.disk_tree.clear()
        
        special_folders = [
            ("🏠 Home", os.path.expanduser("~")),
            ("🖥 Desktop", os.path.expanduser("~/Desktop")),
            ("📄 Documents", os.path.expanduser("~/Documents")),
            ("⬇ Downloads", os.path.expanduser("~/Downloads")),
            ("🖼 Pictures", os.path.expanduser("~/Pictures")),
            ("🎵 Music", os.path.expanduser("~/Music")),
            ("🎬 Videos", os.path.expanduser("~/Videos")),
            ("🗑 Recycle Bin", str(TRASH_DIR)),
        ]
        
        for name, path in special_folders:
            if os.path.exists(path):
                item = QTreeWidgetItem([name])
                item.setData(0, Qt.ItemDataRole.UserRole, path)
                item.setData(0, Qt.ItemDataRole.UserRole + 1, "folder")
                self.disk_tree.invisibleRootItem().addChild(item)
        
        devices_root = QTreeWidgetItem(["💾 This PC"])
        self.disk_tree.invisibleRootItem().addChild(devices_root)
        
        block_devices = get_all_block_devices()
        self.insert_devices(devices_root, block_devices)
        
        self.disk_tree.expandAll()

    def insert_devices(self, parent, devices):
        """Insert block devices recursively"""
        for dev in devices:
            text = f"🔷 {dev['name']}"
            if dev.get('label'):
                text += f" ({dev['label']})"
            text += f" - {dev['size']}"
            if dev.get('fstype'):
                text += f" [{dev['fstype'].upper()}]"
            
            item = QTreeWidgetItem([text])
            item.setData(0, Qt.ItemDataRole.UserRole, dev.get('mountpoint', ''))
            item.setData(0, Qt.ItemDataRole.UserRole + 1, dev['type'])
            item.setData(0, Qt.ItemDataRole.UserRole + 2, dev['name'])
            
            parent.addChild(item)
            
            if 'children' in dev:
                self.insert_devices(item, dev['children'])

    def on_disk_select(self, item):
        """Handle disk tree selection"""
        path = item.data(0, Qt.ItemDataRole.UserRole)
        if path and os.path.isdir(path):
            self.update_history(path)
            self.load_directory(path)

    def show_disk_context_menu(self, pos):
        """Show context menu for disk items"""
        item = self.disk_tree.itemAt(pos)
        if not item:
            return
        
        dev_type = item.data(0, Qt.ItemDataRole.UserRole + 1)
        if dev_type != "part":
            return
        
        menu = QMenu(self)
        
        mountpoint = item.data(0, Qt.ItemDataRole.UserRole)
        if mountpoint:
            unmount_action = QAction("Unmount", self)
            unmount_action.triggered.connect(lambda: self.unmount_device(item))
            menu.addAction(unmount_action)
        else:
            mount_action = QAction("Mount", self)
            mount_action.triggered.connect(lambda: self.mount_device(item))
            menu.addAction(mount_action)
        
        menu.exec(self.disk_tree.mapToGlobal(pos))

    def mount_device(self, item):
        """Mount a device"""
        device_name = item.data(0, Qt.ItemDataRole.UserRole + 2)
        device = f"/dev/{device_name}"
        
        try:
            output = subprocess.check_output(['udisksctl', 'mount', '-b', device], 
                                           stderr=subprocess.STDOUT).decode()
            match = re.search(r'at (.+)\.', output)
            if match:
                mountpoint = match.group(1)
                item.setData(0, Qt.ItemDataRole.UserRole, mountpoint)
                self.status_label.setText(f"✅ Mounted {device_name} at {mountpoint}")
                self.load_disks()
            else:
                raise ValueError("Could not parse mount point")
        except subprocess.CalledProcessError as e:
            QMessageBox.critical(self, "Mount Error", f"Failed to mount {device}:\n{e.output.decode()}")
        except Exception as e:
            QMessageBox.critical(self, "Mount Error", f"Error: {str(e)}")

    def unmount_device(self, item):
        """Unmount a device"""
        device_name = item.data(0, Qt.ItemDataRole.UserRole + 2)
        device = f"/dev/{device_name}"
        
        try:
            subprocess.check_call(['udisksctl', 'unmount', '-b', device])
            item.setData(0, Qt.ItemDataRole.UserRole, "")
            self.status_label.setText(f"✅ Unmounted {device_name}")
            self.load_disks()
        except subprocess.CalledProcessError as e:
            QMessageBox.critical(self, "Unmount Error", f"Failed to unmount {device}:\n{str(e)}")

    def load_directory(self, path):
        """Load directory contents"""
        if not os.path.isdir(path):
            QMessageBox.warning(self, "Error", "Invalid directory path")
            return
        
        self.current_dir = path
        self.path_label.setText(path)
        self.file_tree.clear()
        
        parent = os.path.dirname(path)
        if parent != path:
            parent_item = QTreeWidgetItem(["📁 ..", "", ""])
            parent_item.setData(0, Qt.ItemDataRole.UserRole, parent)
            parent_item.setData(0, Qt.ItemDataRole.UserRole + 1, "dir")
            self.file_tree.addTopLevelItem(parent_item)
        
        try:
            items = sorted(os.listdir(path))
            for item_name in items:
                full_path = os.path.join(path, item_name)
                
                try:
                    if os.path.isdir(full_path):
                        tree_item = QTreeWidgetItem([f"📁 {item_name}", "", ""])
                        tree_item.setData(0, Qt.ItemDataRole.UserRole, full_path)
                        tree_item.setData(0, Qt.ItemDataRole.UserRole + 1, "dir")
                        self.file_tree.addTopLevelItem(tree_item)
                except PermissionError:
                    continue
            
            for item_name in items:
                full_path = os.path.join(path, item_name)
                
                try:
                    if os.path.isfile(full_path):
                        props = get_file_properties(full_path)
                        size_str = format_size(props['size'])
                        
                        # Icon based on file type
                        icon = "📄"
                        ext = os.path.splitext(item_name)[1].lower()
                        if ext in IMAGE_EXT:
                            icon = "🖼"
                        elif ext in VIDEO_EXT:
                            icon = "🎬"
                        elif ext in AUDIO_EXT:
                            icon = "🎵"
                        
                        tree_item = QTreeWidgetItem([
                            f"{icon} {item_name}", 
                            size_str, 
                            props['modified']
                        ])
                        tree_item.setData(0, Qt.ItemDataRole.UserRole, full_path)
                        tree_item.setData(0, Qt.ItemDataRole.UserRole + 1, "file")
                        self.file_tree.addTopLevelItem(tree_item)
                except PermissionError:
                    continue
            
            self.status_label.setText(f"📂 Location: {path}")
            
        except PermissionError:
            QMessageBox.critical(self, "Error", f"Access denied to: {path}")
        except Exception as e:
            QMessageBox.critical(self, "Error", f"Failed to load directory:\n{str(e)}")

    def on_item_double_click(self, item, column):
        """Handle double-click on file/folder"""
        path = item.data(0, Qt.ItemDataRole.UserRole)
        item_type = item.data(0, Qt.ItemDataRole.UserRole + 1)
        
        if item_type == "dir":
            self.update_history(path)
            self.load_directory(path)
        elif item_type == "file":
            self.open_file(path)

    def open_file(self, path):
        """Open file with appropriate application"""
        ext = os.path.splitext(path)[1].lower()
        
        if ext in IMAGE_EXT or ext in VIDEO_EXT or ext in AUDIO_EXT:
            try:
                from media_viewer import launch_media_viewer
                launch_media_viewer(self, path)
            except Exception as e:
                QMessageBox.critical(self, "Error", f"Failed to open media:\n{str(e)}")
        else:
            # Open with default system application
            try:
                if sys.platform.startswith('linux'):
                    subprocess.Popen(['xdg-open', path], 
                                   stdout=subprocess.DEVNULL, 
                                   stderr=subprocess.DEVNULL)
                elif sys.platform == 'darwin':
                    subprocess.Popen(['open', path])
                elif sys.platform == 'win32':
                    os.startfile(path)
            except Exception as e:
                QMessageBox.critical(self, "Error", f"Cannot open file:\n{str(e)}")

    def update_history(self, path):
        """Update navigation history"""
        if self.history_index < len(self.history) - 1:
            self.history = self.history[:self.history_index + 1]
        if not self.history or self.history[-1] != path:
            self.history.append(path)
            self.history_index = len(self.history) - 1

    def go_back(self):
        """Go to previous directory in history"""
        if self.history_index > 0:
            self.history_index -= 1
            self.load_directory(self.history[self.history_index])

    def go_forward(self):
        """Go to next directory in history"""
        if self.history_index < len(self.history) - 1:
            self.history_index += 1
            self.load_directory(self.history[self.history_index])

    def go_up(self):
        """Go to parent directory"""
        parent = os.path.dirname(self.current_dir)
        if parent != self.current_dir:
            self.update_history(parent)
            self.load_directory(parent)

    def refresh_directory(self):
        """Refresh current directory"""
        self.load_directory(self.current_dir)

    def show_file_context_menu(self, pos):
        """Show context menu for files"""
        item = self.file_tree.itemAt(pos)
        if not item:
            return
        
        menu = QMenu(self)
        
        open_action = QAction("Open", self)
        open_action.triggered.connect(lambda: self.on_item_double_click(item, 0))
        menu.addAction(open_action)
        
        menu.addSeparator()
        
        copy_action = QAction("📋 Copy", self)
        copy_action.triggered.connect(self.copy_file)
        menu.addAction(copy_action)
        
        cut_action = QAction("✂ Cut", self)
        cut_action.triggered.connect(self.cut_file)
        menu.addAction(cut_action)
        
        paste_action = QAction("📌 Paste", self)
        paste_action.triggered.connect(self.paste_file)
        menu.addAction(paste_action)
        
        menu.addSeparator()
        
        delete_action = QAction("🗑 Delete", self)
        delete_action.triggered.connect(self.delete_file)
        menu.addAction(delete_action)
        
        menu.addSeparator()
        
        shortcut_action = QAction("🔗 Create Desktop Shortcut", self)
        shortcut_action.triggered.connect(self.create_desktop_shortcut)
        menu.addAction(shortcut_action)
        
        props_action = QAction("ℹ Properties", self)
        props_action.triggered.connect(self.show_properties)
        menu.addAction(props_action)
        
        menu.exec(self.file_tree.mapToGlobal(pos))

    def copy_file(self):
        """Copy selected file to clipboard"""
        item = self.file_tree.currentItem()
        if item:
            path = item.data(0, Qt.ItemDataRole.UserRole)
            self.clipboard = path
            self.clipboard_action = "copy"
            self.status_label.setText(f"📋 Copied: {os.path.basename(path)}")

    def cut_file(self):
        """Cut selected file to clipboard"""
        item = self.file_tree.currentItem()
        if item:
            path = item.data(0, Qt.ItemDataRole.UserRole)
            self.clipboard = path
            self.clipboard_action = "cut"
            self.status_label.setText(f"✂ Cut: {os.path.basename(path)}")

    def paste_file(self):
        """Paste file from clipboard"""
        if not self.clipboard:
            QMessageBox.warning(self, "Paste", "Nothing to paste")
            return
        
        src = self.clipboard
        dst = os.path.join(self.current_dir, os.path.basename(src))
        
        if os.path.exists(dst):
            reply = self.ask_question(
                "Overwrite File",
                f"File '{os.path.basename(dst)}' already exists. Overwrite?"
            )
            
            if reply != QMessageBox.StandardButton.Yes:
                return
        
        try:
            if os.path.isdir(src):
                if self.clipboard_action == "cut":
                    if os.path.exists(dst):
                        shutil.rmtree(dst)
                    shutil.move(src, dst)
                else:
                    if os.path.exists(dst):
                        shutil.rmtree(dst)
                    shutil.copytree(src, dst)
            else:
                if self.clipboard_action == "cut":
                    if os.path.exists(dst):
                        os.remove(dst)
                    shutil.move(src, dst)
                else:
                    if os.path.exists(dst):
                        os.remove(dst)
                    shutil.copy2(src, dst)
            
            self.refresh_directory()
            self.status_label.setText(f"✅ Pasted: {os.path.basename(dst)}")
            
            if self.clipboard_action == "cut":
                self.clipboard = None
                self.clipboard_action = None
                
        except Exception as e:
            QMessageBox.critical(self, "Paste Error", f"Failed to paste:\n{str(e)}")

    def ask_question(self, title, message):
        """Show a question dialog with sound effect"""
        dialog = QuestionMessageBox(self)
        dialog.setWindowTitle(title)
        dialog.setText(message)
        dialog.setStandardButtons(
            QMessageBox.StandardButton.Yes | 
            QMessageBox.StandardButton.No
        )
        dialog.setDefaultButton(QMessageBox.StandardButton.No)
        return dialog.exec()

    def delete_file(self):
        """Delete selected file - moves to recycle bin"""
        item = self.file_tree.currentItem()
        if not item:
            return
        
        path = item.data(0, Qt.ItemDataRole.UserRole)
        if not path or path == "..":
            return
        
        reply = self.ask_question(
            "Move to Recycle Bin", 
            f"Move '{os.path.basename(path)}' to Recycle Bin?"
        )
        
        if reply == QMessageBox.StandardButton.Yes:
            success, message = move_to_recycle_bin(path)
            
            if success:
                self.refresh_directory()
                self.status_label.setText(f"🗑 {message}")
            else:
                QMessageBox.critical(self, "Delete Error", message)

    def create_desktop_shortcut(self):
        """Create desktop shortcut for selected file/directory"""
        item = self.file_tree.currentItem()
        if not item:
            return
        
        path = item.data(0, Qt.ItemDataRole.UserRole)
        if not path or path == "..":
            return
        
        try:
            # Try to get desktop manager from parent window
            desktop_manager = None
            parent = self.parent()
            while parent:
                if hasattr(parent, 'add_icon_to_desktop'):
                    desktop_manager = parent
                    break
                parent = parent.parent()
            
            if desktop_manager:
                # Get file/folder name and create icon
                name = os.path.basename(path)
                
                # Determine icon based on type
                if os.path.isdir(path):
                    icon = "📁"  # Folder icon
                else:
                    ext = os.path.splitext(path)[1].lower()
                    if ext in IMAGE_EXT:
                        icon = "🖼"
                    elif ext in VIDEO_EXT:
                        icon = "🎦"
                    elif ext in AUDIO_EXT:
                        icon = "🎵"
                    else:
                        icon = "📄"
                
                # Add to desktop manager's installed programs and create icon
                if (icon, name) not in desktop_manager.installed_programs:
                    desktop_manager.installed_programs.append((icon, name))
                    desktop_manager.app_metadata[name] = icon
                
                # Create desktop shortcut by adding to desktop
                desktop_manager.add_icon_to_desktop(name)
                
                # Store the file path for later use
                if not hasattr(desktop_manager, 'file_shortcuts'):
                    desktop_manager.file_shortcuts = {}
                desktop_manager.file_shortcuts[name] = path
                
                # Save file shortcuts to user data
                desktop_manager.save_user_state()
                
                self.status_label.setText(f"✅ Created desktop shortcut for {name}")
            else:
                QMessageBox.information(self, "Desktop Shortcut", 
                                       f"Desktop shortcut created for: {os.path.basename(path)}")
                
        except Exception as e:
            QMessageBox.critical(self, "Error", f"Failed to create desktop shortcut:\n{str(e)}")
    
    def show_properties(self):
        """Show file properties"""
        item = self.file_tree.currentItem()
        if not item:
            return
        
        path = item.data(0, Qt.ItemDataRole.UserRole)
        if not path or path == "..":
            return
        
        try:
            props = get_file_properties(path)
            info_text = f"Path: {path}\n"
            info_text += f"Type: {props['type']}\n"
            info_text += f"Size: {format_size(props['size'])}\n"
            info_text += f"Modified: {props['modified']}"
            
            QMessageBox.information(self, "Properties", info_text)
        except Exception as e:
            QMessageBox.critical(self, "Error", f"Cannot get properties:\n{str(e)}")


def open_file_manager(parent=None):
    """Launch file manager window"""
    fm = FileManager(parent)
    fm.show()
    return fm


if __name__ == "__main__":
    print("Starting File Manager...")
    try:
        app = QApplication(sys.argv)
        app.setStyle("Fusion")
        print("QApplication created")
        fm = open_file_manager()
        print("File Manager window created")
        sys.exit(app.exec())
    except Exception as e:
        print(f"ERROR: {e}")
        import traceback
        traceback.print_exc()