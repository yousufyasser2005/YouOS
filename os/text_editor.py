#!/usr/bin/env python3
"""
YouOS 10 - Text Editor Application
Modern text editor with YouOS theme
"""

import sys
from pathlib import Path
from PyQt6.QtWidgets import (QApplication, QMainWindow, QTextEdit, QFileDialog, 
                              QMessageBox, QMenuBar, QMenu, QToolBar, QStatusBar,
                              QWidget, QVBoxLayout, QHBoxLayout, QLabel, QPushButton)
from PyQt6.QtCore import Qt, QTimer
from PyQt6.QtGui import QAction, QFont, QIcon, QTextCursor, QKeySequence

COLORS = {
    'bg_primary': '#0f0f1e',
    'bg_secondary': '#1a1a2e',
    'bg_tertiary': '#252538',
    'accent_primary': '#3b82f6',
    'accent_hover': '#60a5fa',
    'text_primary': '#ffffff',
    'text_secondary': '#9ca3af',
    'border': '#374151',
}


class TextEditor(QMainWindow):
    def __init__(self):
        super().__init__()
        self.current_file = None
        self.is_modified = False
        self.init_ui()
        self.center_window()
    
    def init_ui(self):
        self.setWindowTitle("Text Editor - YouOS 10")
        self.setGeometry(100, 100, 900, 600)
        
        # Apply dark theme styling
        self.setStyleSheet(f"""
            QMainWindow {{
                background-color: {COLORS['bg_primary']};
            }}
            QMenuBar {{
                background-color: {COLORS['bg_secondary']};
                color: {COLORS['text_primary']};
                border-bottom: 1px solid {COLORS['border']};
                padding: 4px;
            }}
            QMenuBar::item {{
                background-color: transparent;
                padding: 6px 12px;
                border-radius: 4px;
            }}
            QMenuBar::item:selected {{
                background-color: {COLORS['accent_primary']};
            }}
            QMenu {{
                background-color: {COLORS['bg_secondary']};
                color: {COLORS['text_primary']};
                border: 1px solid {COLORS['border']};
                border-radius: 8px;
                padding: 5px;
            }}
            QMenu::item {{
                padding: 8px 30px 8px 20px;
                border-radius: 4px;
            }}
            QMenu::item:selected {{
                background-color: {COLORS['accent_primary']};
            }}
            QMenu::separator {{
                height: 1px;
                background: {COLORS['border']};
                margin: 5px 10px;
            }}
            QToolBar {{
                background-color: {COLORS['bg_secondary']};
                border-bottom: 1px solid {COLORS['border']};
                spacing: 5px;
                padding: 5px;
            }}
            QToolButton {{
                background-color: transparent;
                color: {COLORS['text_primary']};
                border: none;
                border-radius: 4px;
                padding: 6px 12px;
                font-size: 13px;
            }}
            QToolButton:hover {{
                background-color: {COLORS['accent_primary']};
            }}
            QStatusBar {{
                background-color: {COLORS['bg_secondary']};
                color: {COLORS['text_secondary']};
                border-top: 1px solid {COLORS['border']};
            }}
            QTextEdit {{
                background-color: {COLORS['bg_primary']};
                color: {COLORS['text_primary']};
                border: 1px solid {COLORS['border']};
                border-radius: 8px;
                padding: 10px;
                selection-background-color: {COLORS['accent_primary']};
                font-family: 'Consolas', 'Monaco', 'Courier New', monospace;
                font-size: 14px;
            }}
        """)
        
        # Create text edit widget
        self.text_edit = QTextEdit()
        self.text_edit.setFont(QFont('Consolas', 12))
        self.text_edit.textChanged.connect(self.on_text_changed)
        
        # Create central widget with padding
        central_widget = QWidget()
        layout = QVBoxLayout(central_widget)
        layout.setContentsMargins(10, 10, 10, 10)
        layout.addWidget(self.text_edit)
        self.setCentralWidget(central_widget)
        
        # Create menu bar
        self.create_menu_bar()
        
        # Create toolbar
        self.create_toolbar()
        
        # Create status bar
        self.create_status_bar()
        
        self.update_title()
    
    def create_menu_bar(self):
        menubar = self.menuBar()
        
        # File menu
        file_menu = menubar.addMenu("&File")
        
        new_action = QAction("📄 New", self)
        new_action.setShortcut(QKeySequence.StandardKey.New)
        new_action.triggered.connect(self.new_file)
        file_menu.addAction(new_action)
        
        open_action = QAction("📂 Open", self)
        open_action.setShortcut(QKeySequence.StandardKey.Open)
        open_action.triggered.connect(self.open_file)
        file_menu.addAction(open_action)
        
        file_menu.addSeparator()
        
        save_action = QAction("💾 Save", self)
        save_action.setShortcut(QKeySequence.StandardKey.Save)
        save_action.triggered.connect(self.save_file)
        file_menu.addAction(save_action)
        
        save_as_action = QAction("💾 Save As...", self)
        save_as_action.setShortcut(QKeySequence.StandardKey.SaveAs)
        save_as_action.triggered.connect(self.save_file_as)
        file_menu.addAction(save_as_action)
        
        file_menu.addSeparator()
        
        exit_action = QAction("🚪 Exit", self)
        exit_action.setShortcut(QKeySequence.StandardKey.Quit)
        exit_action.triggered.connect(self.close)
        file_menu.addAction(exit_action)
        
        # Edit menu
        edit_menu = menubar.addMenu("&Edit")
        
        undo_action = QAction("↶ Undo", self)
        undo_action.setShortcut(QKeySequence.StandardKey.Undo)
        undo_action.triggered.connect(self.text_edit.undo)
        edit_menu.addAction(undo_action)
        
        redo_action = QAction("↷ Redo", self)
        redo_action.setShortcut(QKeySequence.StandardKey.Redo)
        redo_action.triggered.connect(self.text_edit.redo)
        edit_menu.addAction(redo_action)
        
        edit_menu.addSeparator()
        
        cut_action = QAction("✂️ Cut", self)
        cut_action.setShortcut(QKeySequence.StandardKey.Cut)
        cut_action.triggered.connect(self.text_edit.cut)
        edit_menu.addAction(cut_action)
        
        copy_action = QAction("📋 Copy", self)
        copy_action.setShortcut(QKeySequence.StandardKey.Copy)
        copy_action.triggered.connect(self.text_edit.copy)
        edit_menu.addAction(copy_action)
        
        paste_action = QAction("📄 Paste", self)
        paste_action.setShortcut(QKeySequence.StandardKey.Paste)
        paste_action.triggered.connect(self.text_edit.paste)
        edit_menu.addAction(paste_action)
        
        edit_menu.addSeparator()
        
        select_all_action = QAction("🔘 Select All", self)
        select_all_action.setShortcut(QKeySequence.StandardKey.SelectAll)
        select_all_action.triggered.connect(self.text_edit.selectAll)
        edit_menu.addAction(select_all_action)
        
        # View menu
        view_menu = menubar.addMenu("&View")
        
        zoom_in_action = QAction("🔍 Zoom In", self)
        zoom_in_action.setShortcut(QKeySequence.StandardKey.ZoomIn)
        zoom_in_action.triggered.connect(self.zoom_in)
        view_menu.addAction(zoom_in_action)
        
        zoom_out_action = QAction("🔍 Zoom Out", self)
        zoom_out_action.setShortcut(QKeySequence.StandardKey.ZoomOut)
        zoom_out_action.triggered.connect(self.zoom_out)
        view_menu.addAction(zoom_out_action)
        
        reset_zoom_action = QAction("↺ Reset Zoom", self)
        reset_zoom_action.triggered.connect(self.reset_zoom)
        view_menu.addAction(reset_zoom_action)
    
    def create_toolbar(self):
        toolbar = QToolBar("Main Toolbar")
        toolbar.setMovable(False)
        self.addToolBar(toolbar)
        
        new_btn = QPushButton("📄 New")
        new_btn.clicked.connect(self.new_file)
        new_btn.setStyleSheet(self.get_button_style())
        toolbar.addWidget(new_btn)
        
        open_btn = QPushButton("📂 Open")
        open_btn.clicked.connect(self.open_file)
        open_btn.setStyleSheet(self.get_button_style())
        toolbar.addWidget(open_btn)
        
        save_btn = QPushButton("💾 Save")
        save_btn.clicked.connect(self.save_file)
        save_btn.setStyleSheet(self.get_button_style())
        toolbar.addWidget(save_btn)
        
        toolbar.addSeparator()
        
        undo_btn = QPushButton("↶ Undo")
        undo_btn.clicked.connect(self.text_edit.undo)
        undo_btn.setStyleSheet(self.get_button_style())
        toolbar.addWidget(undo_btn)
        
        redo_btn = QPushButton("↷ Redo")
        redo_btn.clicked.connect(self.text_edit.redo)
        redo_btn.setStyleSheet(self.get_button_style())
        toolbar.addWidget(redo_btn)
    
    def get_button_style(self):
        return f"""
            QPushButton {{
                background-color: rgba(59, 130, 246, 0.2);
                color: {COLORS['text_primary']};
                border: 1px solid {COLORS['border']};
                border-radius: 6px;
                padding: 6px 12px;
                font-size: 13px;
            }}
            QPushButton:hover {{
                background-color: {COLORS['accent_primary']};
            }}
            QPushButton:pressed {{
                background-color: {COLORS['accent_hover']};
            }}
        """
    
    def create_status_bar(self):
        self.status_bar = QStatusBar()
        self.setStatusBar(self.status_bar)
        
        self.line_col_label = QLabel("Line: 1 | Col: 1")
        self.line_col_label.setStyleSheet(f"color: {COLORS['text_secondary']}; padding: 0 10px;")
        self.status_bar.addPermanentWidget(self.line_col_label)
        
        self.char_count_label = QLabel("Characters: 0")
        self.char_count_label.setStyleSheet(f"color: {COLORS['text_secondary']}; padding: 0 10px;")
        self.status_bar.addPermanentWidget(self.char_count_label)
        
        # Update cursor position on cursor change
        self.text_edit.cursorPositionChanged.connect(self.update_cursor_position)
        
        self.status_bar.showMessage("Ready")
    
    def center_window(self):
        """Center window on screen"""
        screen = self.screen().geometry()
        x = (screen.width() - self.width()) // 2
        y = (screen.height() - self.height()) // 2
        self.move(x, y)
    
    def new_file(self):
        if self.maybe_save():
            self.text_edit.clear()
            self.current_file = None
            self.is_modified = False
            self.update_title()
            self.status_bar.showMessage("New file created", 3000)
    
    def open_file(self):
        if self.maybe_save():
            file_path, _ = QFileDialog.getOpenFileName(
                self,
                "Open File",
                "",
                "Text Files (*.txt);;Python Files (*.py);;All Files (*.*)"
            )
            
            if file_path:
                try:
                    with open(file_path, 'r', encoding='utf-8') as file:
                        self.text_edit.setPlainText(file.read())
                    self.current_file = file_path
                    self.is_modified = False
                    self.update_title()
                    self.status_bar.showMessage(f"Opened: {Path(file_path).name}", 3000)
                except Exception as e:
                    QMessageBox.critical(self, "Error", f"Could not open file:\n{str(e)}")
    
    def save_file(self):
        if self.current_file:
            return self.save_to_file(self.current_file)
        else:
            return self.save_file_as()
    
    def save_file_as(self):
        file_path, _ = QFileDialog.getSaveFileName(
            self,
            "Save File As",
            "",
            "Text Files (*.txt);;Python Files (*.py);;All Files (*.*)"
        )
        
        if file_path:
            return self.save_to_file(file_path)
        return False
    
    def save_to_file(self, file_path):
        try:
            with open(file_path, 'w', encoding='utf-8') as file:
                file.write(self.text_edit.toPlainText())
            self.current_file = file_path
            self.is_modified = False
            self.update_title()
            self.status_bar.showMessage(f"Saved: {Path(file_path).name}", 3000)
            return True
        except Exception as e:
            QMessageBox.critical(self, "Error", f"Could not save file:\n{str(e)}")
            return False
    
    def maybe_save(self):
        """Ask user to save if document is modified"""
        if not self.is_modified:
            return True
        
        reply = QMessageBox.question(
            self,
            "Unsaved Changes",
            "Do you want to save your changes?",
            QMessageBox.StandardButton.Save | 
            QMessageBox.StandardButton.Discard | 
            QMessageBox.StandardButton.Cancel
        )
        
        if reply == QMessageBox.StandardButton.Save:
            return self.save_file()
        elif reply == QMessageBox.StandardButton.Cancel:
            return False
        return True
    
    def on_text_changed(self):
        self.is_modified = True
        self.update_title()
        self.update_char_count()
    
    def update_title(self):
        title = "Text Editor - YouOS 10"
        if self.current_file:
            title += f" - {Path(self.current_file).name}"
        else:
            title += " - Untitled"
        if self.is_modified:
            title += " *"
        self.setWindowTitle(title)
    
    def update_cursor_position(self):
        cursor = self.text_edit.textCursor()
        line = cursor.blockNumber() + 1
        col = cursor.columnNumber() + 1
        self.line_col_label.setText(f"Line: {line} | Col: {col}")
    
    def update_char_count(self):
        char_count = len(self.text_edit.toPlainText())
        self.char_count_label.setText(f"Characters: {char_count}")
    
    def zoom_in(self):
        self.text_edit.zoomIn(1)
    
    def zoom_out(self):
        self.text_edit.zoomOut(1)
    
    def reset_zoom(self):
        self.text_edit.setFont(QFont('Consolas', 12))
    
    def closeEvent(self, event):
        if self.maybe_save():
            event.accept()
        else:
            event.ignore()


def main():
    app = QApplication(sys.argv)
    editor = TextEditor()
    editor.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()