"""
YouOS Recycle Bin - Glassmorphic Design
Manage deleted files with restore and permanent deletion
"""

import sys
import os
import shutil
from pathlib import Path
from datetime import datetime
from PyQt6.QtWidgets import (QApplication, QMainWindow, QWidget, QVBoxLayout, 
                              QHBoxLayout, QLabel, QPushButton, QTableWidget,
                              QTableWidgetItem, QHeaderView, QMessageBox,
                              QGraphicsDropShadowEffect, QFrame)
from PyQt6.QtCore import Qt, QTimer
from PyQt6.QtGui import QColor, QPainter, QPainterPath, QLinearGradient, QPen

# Setup directories
BASE_DIR = Path(__file__).parent
TRASH_DIR = BASE_DIR / "recycle_bin"
USER_FILES_DIR = BASE_DIR / "user files"
ASSETS_DIR = BASE_DIR / "assets"

TRASH_DIR.mkdir(exist_ok=True)
USER_FILES_DIR.mkdir(exist_ok=True)

# Try to import sound utilities
try:
    from utils import play_sound
except ImportError:
    def play_sound(name):
        pass

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


def read_metadata(meta_file):
    """Read metadata from .meta file"""
    metadata = {}
    try:
        with open(meta_file, 'r') as f:
            for line in f:
                if '=' in line:
                    key, value = line.strip().split('=', 1)
                    metadata[key] = value
    except Exception as e:
        print(f"Error reading metadata: {e}")
    return metadata


class GlassFrame(QFrame):
    """Frame with glassmorphism effect"""
    
    def __init__(self, parent=None, opacity=0.15):
        super().__init__(parent)
        self.opacity = opacity
        self.setAttribute(Qt.WidgetAttribute.WA_TranslucentBackground)
        
        shadow = QGraphicsDropShadowEffect()
        shadow.setBlurRadius(30)
        shadow.setColor(QColor(0, 0, 0, 100))
        shadow.setOffset(0, 5)
        self.setGraphicsEffect(shadow)
    
    def paintEvent(self, event):
        from PyQt6.QtCore import QRectF
        
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        
        path = QPainterPath()
        path.addRoundedRect(QRectF(self.rect()), 16, 16)
        
        gradient = QLinearGradient(0, 0, 0, self.height())
        gradient.setColorAt(0, QColor(30, 30, 60, 220))
        gradient.setColorAt(1, QColor(20, 20, 50, 200))
        
        painter.fillPath(path, gradient)
        painter.setPen(QPen(QColor(255, 255, 255, 80), 1))
        painter.drawPath(path)


class RecycleBinWindow(QMainWindow):
    """Main Recycle Bin window"""
    
    def __init__(self):
        super().__init__()
        self.setWindowTitle("🗑️ Recycle Bin - YouOS")
        self.setGeometry(100, 100, 1000, 600)
        
        # Apply gradient background
        self.setStyleSheet(f"""
            QMainWindow {{
                background: qlineargradient(
                    x1:0, y1:0, x2:1, y2:1,
                    stop:0 {COLORS['bg_primary']},
                    stop:0.5 #1a1a3e,
                    stop:1 {COLORS['bg_primary']}
                );
            }}
        """)
        
        self.setup_ui()
        self.refresh_files()
    
    def setup_ui(self):
        """Setup the user interface"""
        central = QWidget()
        self.setCentralWidget(central)
        
        layout = QVBoxLayout(central)
        layout.setContentsMargins(20, 20, 20, 20)
        layout.setSpacing(20)
        
        # Header
        header = GlassFrame(opacity=0.15)
        header_layout = QVBoxLayout(header)
        header_layout.setContentsMargins(30, 20, 30, 20)
        
        title_layout = QHBoxLayout()
        
        icon_label = QLabel("🗑️")
        icon_label.setStyleSheet("font-size: 48px;")
        title_layout.addWidget(icon_label)
        
        title_text_layout = QVBoxLayout()
        title_text_layout.setSpacing(5)
        
        title = QLabel("Recycle Bin")
        title.setStyleSheet(f"""
            color: {COLORS['text_primary']};
            font-size: 28px;
            font-weight: bold;
        """)
        title_text_layout.addWidget(title)
        
        self.subtitle = QLabel("0 items")
        self.subtitle.setStyleSheet(f"""
            color: {COLORS['text_secondary']};
            font-size: 14px;
        """)
        title_text_layout.addWidget(self.subtitle)
        
        title_layout.addLayout(title_text_layout)
        title_layout.addStretch()
        
        header_layout.addLayout(title_layout)
        layout.addWidget(header)
        
        # Table Container
        table_container = GlassFrame(opacity=0.15)
        table_layout = QVBoxLayout(table_container)
        table_layout.setContentsMargins(20, 20, 20, 20)
        
        # Table
        self.table = QTableWidget()
        self.table.setColumnCount(4)
        self.table.setHorizontalHeaderLabels(["📄 Name", "📁 Original Path", "🗓️ Deleted On", "💾 Size"])
        self.table.horizontalHeader().setSectionResizeMode(0, QHeaderView.ResizeMode.Interactive)
        self.table.horizontalHeader().setSectionResizeMode(1, QHeaderView.ResizeMode.Stretch)
        self.table.horizontalHeader().setSectionResizeMode(2, QHeaderView.ResizeMode.Interactive)
        self.table.horizontalHeader().setSectionResizeMode(3, QHeaderView.ResizeMode.ResizeToContents)
        self.table.setColumnWidth(0, 250)
        self.table.setColumnWidth(2, 180)
        self.table.setSelectionBehavior(QTableWidget.SelectionBehavior.SelectRows)
        self.table.setSelectionMode(QTableWidget.SelectionMode.SingleSelection)
        self.table.setAlternatingRowColors(True)
        
        self.table.setStyleSheet(f"""
            QTableWidget {{
                background: rgba(255, 255, 255, 0.05);
                color: {COLORS['text_primary']};
                border: none;
                border-radius: 8px;
                gridline-color: rgba(255, 255, 255, 0.1);
                font-size: 13px;
            }}
            QTableWidget::item {{
                padding: 8px;
                border: none;
            }}
            QTableWidget::item:selected {{
                background: rgba(59, 130, 246, 0.3);
            }}
            QTableWidget::item:hover {{
                background: rgba(255, 255, 255, 0.1);
            }}
            QHeaderView::section {{
                background: rgba(255, 255, 255, 0.1);
                color: {COLORS['text_primary']};
                padding: 10px;
                border: none;
                font-weight: bold;
                font-size: 13px;
            }}
            QTableWidget::alternating-row {{
                background: rgba(255, 255, 255, 0.02);
            }}
            QScrollBar:vertical {{
                background: rgba(255, 255, 255, 0.1);
                width: 10px;
                border-radius: 5px;
            }}
            QScrollBar::handle:vertical {{
                background: rgba(59, 130, 246, 0.5);
                border-radius: 5px;
                min-height: 20px;
            }}
            QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {{
                border: none;
                background: none;
            }}
        """)
        
        table_layout.addWidget(self.table)
        layout.addWidget(table_container, stretch=1)
        
        # Action Buttons
        button_container = GlassFrame(opacity=0.15)
        button_layout = QHBoxLayout(button_container)
        button_layout.setContentsMargins(20, 15, 20, 15)
        button_layout.setSpacing(15)
        
        # Restore Button
        restore_btn = QPushButton("↩️ Restore")
        restore_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        restore_btn.setStyleSheet(f"""
            QPushButton {{
                background: {COLORS['success']};
                color: white;
                border: none;
                border-radius: 8px;
                padding: 12px 24px;
                font-size: 14px;
                font-weight: bold;
            }}
            QPushButton:hover {{
                background: #059669;
            }}
            QPushButton:pressed {{
                background: #047857;
            }}
        """)
        restore_btn.clicked.connect(self.restore_file)
        button_layout.addWidget(restore_btn)
        
        # Delete Permanently Button
        delete_btn = QPushButton("🗑️ Delete Permanently")
        delete_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        delete_btn.setStyleSheet(f"""
            QPushButton {{
                background: {COLORS['error']};
                color: white;
                border: none;
                border-radius: 8px;
                padding: 12px 24px;
                font-size: 14px;
                font-weight: bold;
            }}
            QPushButton:hover {{
                background: #dc2626;
            }}
            QPushButton:pressed {{
                background: #b91c1c;
            }}
        """)
        delete_btn.clicked.connect(self.delete_permanently)
        button_layout.addWidget(delete_btn)
        
        button_layout.addStretch()
        
        # Empty Bin Button
        empty_btn = QPushButton("🧹 Empty Recycle Bin")
        empty_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        empty_btn.setStyleSheet(f"""
            QPushButton {{
                background: {COLORS['warning']};
                color: white;
                border: none;
                border-radius: 8px;
                padding: 12px 24px;
                font-size: 14px;
                font-weight: bold;
            }}
            QPushButton:hover {{
                background: #f59e0b;
            }}
            QPushButton:pressed {{
                background: #d97706;
            }}
        """)
        empty_btn.clicked.connect(self.empty_bin)
        button_layout.addWidget(empty_btn)
        
        # Refresh Button
        refresh_btn = QPushButton("🔄 Refresh")
        refresh_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        refresh_btn.setStyleSheet(f"""
            QPushButton {{
                background: {COLORS['accent_primary']};
                color: white;
                border: none;
                border-radius: 8px;
                padding: 12px 24px;
                font-size: 14px;
                font-weight: bold;
            }}
            QPushButton:hover {{
                background: {COLORS['accent_hover']};
            }}
            QPushButton:pressed {{
                background: #2563eb;
            }}
        """)
        refresh_btn.clicked.connect(self.refresh_files)
        button_layout.addWidget(refresh_btn)
        
        layout.addWidget(button_container)
    
    def refresh_files(self):
        """Refresh the list of deleted files"""
        self.table.setRowCount(0)
        
        if not TRASH_DIR.exists():
            self.subtitle.setText("0 items")
            return
        
        # Get all files (excluding .meta files)
        files = [f for f in TRASH_DIR.iterdir() if f.is_file() and not f.name.endswith('.meta')]
        self.subtitle.setText(f"{len(files)} item{'s' if len(files) != 1 else ''}")
        
        for idx, file_path in enumerate(sorted(files)):
            self.table.insertRow(idx)
            
            # Read metadata
            meta_file = TRASH_DIR / f"{file_path.name}.meta"
            metadata = read_metadata(meta_file) if meta_file.exists() else {}
            
            original_name = metadata.get('original_name', file_path.name)
            original_path = metadata.get('original_path', 'Unknown')
            deleted_time_str = metadata.get('deleted_time', '')
            
            # Name
            name_item = QTableWidgetItem(original_name)
            name_item.setData(Qt.ItemDataRole.UserRole, str(file_path))
            name_item.setData(Qt.ItemDataRole.UserRole + 1, original_path)
            self.table.setItem(idx, 0, name_item)
            
            # Original Path
            path_item = QTableWidgetItem(original_path)
            self.table.setItem(idx, 1, path_item)
            
            # Deleted Date
            if deleted_time_str:
                try:
                    deleted_time = datetime.fromisoformat(deleted_time_str)
                    date_str = deleted_time.strftime("%Y-%m-%d %H:%M:%S")
                except:
                    date_str = deleted_time_str
            else:
                deleted_time = datetime.fromtimestamp(file_path.stat().st_mtime)
                date_str = deleted_time.strftime("%Y-%m-%d %H:%M:%S")
            
            date_item = QTableWidgetItem(date_str)
            self.table.setItem(idx, 2, date_item)
            
            # Size
            size_bytes = file_path.stat().st_size
            size_str = self.format_size(size_bytes)
            size_item = QTableWidgetItem(size_str)
            self.table.setItem(idx, 3, size_item)
    
    def format_size(self, bytes):
        """Format file size in human-readable format"""
        for unit in ['B', 'KB', 'MB', 'GB', 'TB']:
            if bytes < 1024.0:
                return f"{bytes:.1f} {unit}"
            bytes /= 1024.0
        return f"{bytes:.1f} PB"
    
    def restore_file(self):
        """Restore selected file to original location"""
        current_row = self.table.currentRow()
        if current_row < 0:
            self.show_message("No Selection", "Please select a file to restore.", QMessageBox.Icon.Warning)
            return
        
        trash_file_path = Path(self.table.item(current_row, 0).data(Qt.ItemDataRole.UserRole))
        original_path = self.table.item(current_row, 0).data(Qt.ItemDataRole.UserRole + 1)
        
        if not trash_file_path.exists():
            self.show_message("Error", "File no longer exists in recycle bin.", QMessageBox.Icon.Critical)
            self.refresh_files()
            return
        
        # Validate original path
        destination = Path(original_path)
        
        # Check if parent directory exists
        if not destination.parent.exists():
            reply = QMessageBox.question(
                self,
                "Directory Not Found",
                f"Original directory does not exist:\n{destination.parent}\n\nRestore to user files folder instead?",
                QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
                QMessageBox.StandardButton.Yes
            )
            
            if reply == QMessageBox.StandardButton.Yes:
                destination = USER_FILES_DIR / destination.name
            else:
                return
        
        # Check if file already exists
        if destination.exists():
            reply = QMessageBox.question(
                self,
                "File Exists",
                f"A file with this name already exists:\n{destination}\n\nOverwrite?",
                QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
                QMessageBox.StandardButton.No
            )
            
            if reply != QMessageBox.StandardButton.Yes:
                return
            
            # Remove existing file
            try:
                if destination.is_dir():
                    shutil.rmtree(destination)
                else:
                    destination.unlink()
            except Exception as e:
                self.show_message("Error", f"Could not remove existing file: {str(e)}", QMessageBox.Icon.Critical)
                return
        
        try:
            # Move file back
            shutil.move(str(trash_file_path), str(destination))
            
            # Remove metadata file
            meta_file = TRASH_DIR / f"{trash_file_path.name}.meta"
            if meta_file.exists():
                meta_file.unlink()
            
            self.refresh_files()
            self.show_message("Restored", f"File has been restored to:\n{destination}", QMessageBox.Icon.Information)
            play_sound("success.wav")
        except Exception as e:
            self.show_message("Error", f"Could not restore file: {str(e)}", QMessageBox.Icon.Critical)
    
    def delete_permanently(self):
        """Delete selected file permanently"""
        current_row = self.table.currentRow()
        if current_row < 0:
            self.show_message("No Selection", "Please select a file to delete.", QMessageBox.Icon.Warning)
            return
        
        file_path = Path(self.table.item(current_row, 0).data(Qt.ItemDataRole.UserRole))
        filename = self.table.item(current_row, 0).text()
        
        reply = QMessageBox.question(
            self,
            "Confirm Deletion",
            f"Are you sure you want to permanently delete '{filename}'?\n\nThis action cannot be undone.",
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
            QMessageBox.StandardButton.No
        )
        
        if reply == QMessageBox.StandardButton.Yes:
            try:
                # Delete file
                if file_path.exists():
                    if file_path.is_dir():
                        shutil.rmtree(file_path)
                    else:
                        file_path.unlink()
                
                # Delete metadata
                meta_file = TRASH_DIR / f"{file_path.name}.meta"
                if meta_file.exists():
                    meta_file.unlink()
                
                self.refresh_files()
                self.show_message("Deleted", f"{filename} has been permanently deleted.", QMessageBox.Icon.Information)
                play_sound("success.wav")
            except Exception as e:
                self.show_message("Error", f"Could not delete {filename}: {str(e)}", QMessageBox.Icon.Critical)
    
    def empty_bin(self):
        """Empty the entire recycle bin"""
        if not TRASH_DIR.exists() or not any(f for f in TRASH_DIR.iterdir() if not f.name.endswith('.meta')):
            self.show_message("Empty", "Recycle Bin is already empty.", QMessageBox.Icon.Information)
            return
        
        reply = QMessageBox.question(
            self,
            "Empty Recycle Bin",
            "Are you sure you want to empty the Recycle Bin?\n\nAll items will be permanently deleted and cannot be recovered.",
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
            QMessageBox.StandardButton.No
        )
        
        if reply == QMessageBox.StandardButton.Yes:
            try:
                for file_path in TRASH_DIR.iterdir():
                    if file_path.is_file():
                        file_path.unlink()
                    elif file_path.is_dir():
                        shutil.rmtree(file_path)
                
                self.refresh_files()
                self.show_message("Success", "Recycle Bin has been emptied.", QMessageBox.Icon.Information)
                play_sound("success.wav")
            except Exception as e:
                self.show_message("Error", f"Could not empty recycle bin: {str(e)}", QMessageBox.Icon.Critical)
    
    def show_message(self, title, message, icon=QMessageBox.Icon.Information):
        """Show a styled message box"""
        msg = QMessageBox(self)
        msg.setWindowTitle(title)
        msg.setText(message)
        msg.setIcon(icon)
        msg.setStyleSheet(f"""
            QMessageBox {{
                background: {COLORS['bg_secondary']};
            }}
            QMessageBox QLabel {{
                color: {COLORS['text_primary']};
                font-size: 13px;
            }}
            QPushButton {{
                background: {COLORS['accent_primary']};
                color: white;
                border: none;
                border-radius: 6px;
                padding: 8px 16px;
                font-size: 13px;
                min-width: 80px;
            }}
            QPushButton:hover {{
                background: {COLORS['accent_hover']};
            }}
        """)
        msg.exec()


def main():
    """Main entry point"""
    app = QApplication(sys.argv)
    app.setStyle("Fusion")
    
    window = RecycleBinWindow()
    window.show()
    
    sys.exit(app.exec())


if __name__ == "__main__":
    main()