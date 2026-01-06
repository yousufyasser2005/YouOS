"""
Yousuf Desk Tools - Word Processor Module
word_processor.py - Full-featured word processing application
"""

import json
from pathlib import Path
from PyQt6.QtWidgets import (QWidget, QVBoxLayout, QHBoxLayout, QTextEdit, 
                              QPushButton, QToolBar, QLabel, QComboBox, 
                              QSpinBox, QColorDialog, QFileDialog, QMessageBox,
                              QFontComboBox, QFrame)
from PyQt6.QtCore import Qt, pyqtSignal, QTimer
from PyQt6.QtGui import (QFont, QTextCharFormat, QColor, QTextCursor, 
                         QTextListFormat, QTextBlockFormat, QPageSize,
                         QTextDocument, QTextTableFormat, QIcon)
from PyQt6.QtPrintSupport import QPrinter, QPrintDialog

try:
    from widgets import GlassFrame
    from utils import play_sound
except ImportError:
    def play_sound(name):
        pass
    class GlassFrame(QFrame):
        def __init__(self, parent=None, opacity=0.15):
            super().__init__(parent)

COLORS = {
    'bg_primary': '#0f0f1e',
    'bg_secondary': '#1a1a2e',
    'bg_tertiary': '#252538',
    'accent_primary': '#3b82f6',
    'accent_word': '#2563eb',
    'text_primary': '#ffffff',
    'text_secondary': '#9ca3af',
}


class WordProcessor(QWidget):
    """Word processing module"""
    
    back_requested = pyqtSignal()
    
    def __init__(self, parent=None):
        super().__init__(parent)
        self.current_file = None
        self.is_modified = False
        self.setup_ui()
        self.setup_shortcuts()
    
    def setup_ui(self):
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(0)
        
        # Top bar
        top_bar = self.create_top_bar()
        layout.addWidget(top_bar)
        
        # Toolbar
        toolbar = self.create_toolbar()
        layout.addWidget(toolbar)
        
        # Editor area
        editor_container = QFrame()
        editor_container.setStyleSheet(f"background: {COLORS['bg_secondary']}; padding: 20px;")
        editor_layout = QVBoxLayout(editor_container)
        
        # Text editor
        self.editor = QTextEdit()
        self.editor.setStyleSheet(f"""
            QTextEdit {{
                background: white;
                color: black;
                border: 1px solid {COLORS['bg_tertiary']};
                border-radius: 8px;
                padding: 40px;
                font-family: 'Calibri', 'Arial';
                font-size: 12pt;
            }}
        """)
        self.editor.textChanged.connect(self.on_text_changed)
        self.editor.cursorPositionChanged.connect(self.update_format_buttons)
        editor_layout.addWidget(self.editor)
        
        layout.addWidget(editor_container)
        
        # Status bar
        status_bar = self.create_status_bar()
        layout.addWidget(status_bar)
    
    def create_top_bar(self):
        """Create top navigation bar"""
        bar = QFrame()
        bar.setFixedHeight(60)
        bar.setStyleSheet(f"""
            QFrame {{
                background: {COLORS['accent_word']};
                border-bottom: 1px solid rgba(255, 255, 255, 0.1);
            }}
        """)
        
        layout = QHBoxLayout(bar)
        layout.setContentsMargins(20, 10, 20, 10)
        
        # Back button
        back_btn = QPushButton("← Back")
        back_btn.setFixedHeight(40)
        back_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        back_btn.setStyleSheet("""
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
        back_btn.clicked.connect(self.go_back)
        layout.addWidget(back_btn)
        
        # Title
        title = QLabel("📝 Word Processor")
        title.setStyleSheet("color: white; font-size: 20px; font-weight: bold; margin-left: 20px;")
        layout.addWidget(title)
        
        layout.addStretch()
        
        # Document name
        self.doc_name = QLabel("Untitled Document")
        self.doc_name.setStyleSheet("color: white; font-size: 14px;")
        layout.addWidget(self.doc_name)
        
        return bar
    
    def create_toolbar(self):
        """Create formatting toolbar"""
        toolbar = QFrame()
        toolbar.setFixedHeight(70)
        toolbar.setStyleSheet(f"background: {COLORS['bg_tertiary']};")
        
        layout = QHBoxLayout(toolbar)
        layout.setContentsMargins(20, 10, 20, 10)
        layout.setSpacing(10)
        
        # File operations
        new_btn = self.create_tool_button("📄 New", self.new_document)
        layout.addWidget(new_btn)
        
        open_btn = self.create_tool_button("📂 Open", self.open_document)
        layout.addWidget(open_btn)
        
        save_btn = self.create_tool_button("💾 Save", self.save_document)
        layout.addWidget(save_btn)
        
        save_as_btn = self.create_tool_button("💾 Save As", self.save_as_document)
        layout.addWidget(save_as_btn)
        
        layout.addWidget(self.create_separator())
        
        # Font controls
        self.font_combo = QFontComboBox()
        self.font_combo.setFixedWidth(150)
        self.font_combo.setCurrentFont(QFont("Calibri"))
        self.font_combo.currentFontChanged.connect(self.change_font)
        self.font_combo.setStyleSheet(self.get_combo_style())
        layout.addWidget(self.font_combo)
        
        self.font_size = QSpinBox()
        self.font_size.setRange(6, 72)
        self.font_size.setValue(12)
        self.font_size.setSuffix(" pt")
        self.font_size.valueChanged.connect(self.change_font_size)
        self.font_size.setStyleSheet(self.get_combo_style())
        layout.addWidget(self.font_size)
        
        layout.addWidget(self.create_separator())
        
        # Text formatting
        self.bold_btn = self.create_tool_button("B", self.toggle_bold, checkable=True)
        self.bold_btn.setFont(QFont("Arial", 10, QFont.Weight.Bold))
        layout.addWidget(self.bold_btn)
        
        self.italic_btn = self.create_tool_button("I", self.toggle_italic, checkable=True)
        self.italic_btn.setFont(QFont("Arial", 10, QFont.Weight.Normal, True))
        layout.addWidget(self.italic_btn)
        
        self.underline_btn = self.create_tool_button("U", self.toggle_underline, checkable=True)
        layout.addWidget(self.underline_btn)
        
        color_btn = self.create_tool_button("🎨", self.change_text_color)
        layout.addWidget(color_btn)
        
        layout.addWidget(self.create_separator())
        
        # Alignment
        left_btn = self.create_tool_button("≡", lambda: self.set_alignment(Qt.AlignmentFlag.AlignLeft))
        layout.addWidget(left_btn)
        
        center_btn = self.create_tool_button("≣", lambda: self.set_alignment(Qt.AlignmentFlag.AlignCenter))
        layout.addWidget(center_btn)
        
        right_btn = self.create_tool_button("≡", lambda: self.set_alignment(Qt.AlignmentFlag.AlignRight))
        layout.addWidget(right_btn)
        
        layout.addWidget(self.create_separator())
        
        # Lists
        bullet_btn = self.create_tool_button("• List", self.insert_bullet_list)
        layout.addWidget(bullet_btn)
        
        number_btn = self.create_tool_button("1. List", self.insert_numbered_list)
        layout.addWidget(number_btn)
        
        layout.addWidget(self.create_separator())
        
        # Insert
        table_btn = self.create_tool_button("📊 Table", self.insert_table)
        layout.addWidget(table_btn)
        
        image_btn = self.create_tool_button("🖼️ Image", self.insert_image)
        layout.addWidget(image_btn)
        
        layout.addWidget(self.create_separator())
        
        # Print
        print_btn = self.create_tool_button("🖨️ Print", self.print_document)
        layout.addWidget(print_btn)
        
        layout.addStretch()
        
        return toolbar
    
    def create_status_bar(self):
        """Create status bar"""
        bar = QFrame()
        bar.setFixedHeight(40)
        bar.setStyleSheet(f"background: {COLORS['bg_tertiary']};")
        
        layout = QHBoxLayout(bar)
        layout.setContentsMargins(20, 5, 20, 5)
        
        self.word_count = QLabel("Words: 0 | Characters: 0")
        self.word_count.setStyleSheet(f"color: {COLORS['text_secondary']}; font-size: 12px;")
        layout.addWidget(self.word_count)
        
        layout.addStretch()
        
        self.cursor_pos = QLabel("Line 1, Column 1")
        self.cursor_pos.setStyleSheet(f"color: {COLORS['text_secondary']}; font-size: 12px;")
        layout.addWidget(self.cursor_pos)
        
        # Update timer
        self.status_timer = QTimer()
        self.status_timer.timeout.connect(self.update_status)
        self.status_timer.start(500)
        
        return bar
    
    def create_tool_button(self, text, callback, checkable=False):
        """Create a toolbar button"""
        btn = QPushButton(text)
        btn.setCheckable(checkable)
        btn.setFixedSize(70, 40)
        btn.setCursor(Qt.CursorShape.PointingHandCursor)
        btn.setStyleSheet(f"""
            QPushButton {{
                background: rgba(255, 255, 255, 0.1);
                color: {COLORS['text_primary']};
                border: 1px solid rgba(255, 255, 255, 0.2);
                border-radius: 6px;
                font-size: 11px;
            }}
            QPushButton:hover {{
                background: rgba(59, 130, 246, 0.3);
            }}
            QPushButton:checked {{
                background: {COLORS['accent_primary']};
                border: 1px solid {COLORS['accent_primary']};
            }}
        """)
        btn.clicked.connect(callback)
        return btn
    
    def create_separator(self):
        """Create toolbar separator"""
        sep = QFrame()
        sep.setFrameShape(QFrame.Shape.VLine)
        sep.setStyleSheet("background: rgba(255, 255, 255, 0.2); max-width: 1px;")
        return sep
    
    def get_combo_style(self):
        """Get stylesheet for combo boxes"""
        return f"""
            QComboBox, QSpinBox {{
                background: rgba(255, 255, 255, 0.1);
                color: {COLORS['text_primary']};
                border: 1px solid rgba(255, 255, 255, 0.2);
                border-radius: 6px;
                padding: 5px 10px;
            }}
            QComboBox::drop-down, QSpinBox::up-button, QSpinBox::down-button {{
                border: none;
            }}
            QComboBox QAbstractItemView {{
                background: {COLORS['bg_tertiary']};
                color: {COLORS['text_primary']};
                selection-background-color: {COLORS['accent_primary']};
            }}
        """
    
    def setup_shortcuts(self):
        """Setup keyboard shortcuts"""
        from PyQt6.QtGui import QShortcut, QKeySequence
        
        QShortcut(QKeySequence.StandardKey.New, self, self.new_document)
        QShortcut(QKeySequence.StandardKey.Open, self, self.open_document)
        QShortcut(QKeySequence.StandardKey.Save, self, self.save_document)
        QShortcut(QKeySequence.StandardKey.Print, self, self.print_document)
        QShortcut(QKeySequence.StandardKey.Bold, self, self.toggle_bold)
        QShortcut(QKeySequence.StandardKey.Italic, self, self.toggle_italic)
        QShortcut(QKeySequence.StandardKey.Underline, self, self.toggle_underline)
    
    def go_back(self):
        """Return to launcher"""
        if self.is_modified:
            reply = QMessageBox.question(
                self, "Unsaved Changes",
                "Do you want to save changes before going back?",
                QMessageBox.StandardButton.Save | 
                QMessageBox.StandardButton.Discard | 
                QMessageBox.StandardButton.Cancel
            )
            
            if reply == QMessageBox.StandardButton.Save:
                self.save_document()
                self.back_requested.emit()
            elif reply == QMessageBox.StandardButton.Discard:
                self.back_requested.emit()
        else:
            self.back_requested.emit()
    
    def on_text_changed(self):
        """Handle text changes"""
        self.is_modified = True
        self.update_status()
    
    def update_status(self):
        """Update status bar"""
        text = self.editor.toPlainText()
        words = len([w for w in text.split() if w])
        chars = len(text)
        self.word_count.setText(f"Words: {words} | Characters: {chars}")
        
        cursor = self.editor.textCursor()
        line = cursor.blockNumber() + 1
        col = cursor.columnNumber() + 1
        self.cursor_pos.setText(f"Line {line}, Column {col}")
    
    def update_format_buttons(self):
        """Update formatting button states"""
        fmt = self.editor.currentCharFormat()
        self.bold_btn.setChecked(fmt.fontWeight() == QFont.Weight.Bold)
        self.italic_btn.setChecked(fmt.fontItalic())
        self.underline_btn.setChecked(fmt.fontUnderline())
    
    def new_document(self):
        """Create new document"""
        if self.is_modified:
            reply = QMessageBox.question(
                self, "Unsaved Changes",
                "Do you want to save changes?",
                QMessageBox.StandardButton.Save | 
                QMessageBox.StandardButton.Discard | 
                QMessageBox.StandardButton.Cancel
            )
            
            if reply == QMessageBox.StandardButton.Save:
                self.save_document()
            elif reply == QMessageBox.StandardButton.Cancel:
                return
        
        self.editor.clear()
        self.current_file = None
        self.is_modified = False
        self.doc_name.setText("Untitled Document")
        play_sound("click.wav")
    
    def open_document(self):
        """Open existing document"""
        file_path, _ = QFileDialog.getOpenFileName(
            self, "Open Document", "",
            "HTML Files (*.html);;Text Files (*.txt);;All Files (*.*)"
        )
        
        if file_path:
            try:
                with open(file_path, 'r', encoding='utf-8') as f:
                    content = f.read()
                    if file_path.endswith('.html'):
                        self.editor.setHtml(content)
                    else:
                        self.editor.setPlainText(content)
                
                self.current_file = file_path
                self.is_modified = False
                self.doc_name.setText(Path(file_path).name)
                play_sound("success.wav")
            except Exception as e:
                QMessageBox.critical(self, "Error", f"Failed to open file: {str(e)}")
    
    def save_document(self):
        """Save document"""
        if self.current_file:
            self.save_to_file(self.current_file)
        else:
            self.save_as_document()
    
    def save_as_document(self):
        """Save document as new file"""
        file_path, _ = QFileDialog.getSaveFileName(
            self, "Save Document", "",
            "HTML Files (*.html);;Text Files (*.txt);;All Files (*.*)"
        )
        
        if file_path:
            self.save_to_file(file_path)
    
    def save_to_file(self, file_path):
        """Save content to file"""
        try:
            content = self.editor.toHtml() if file_path.endswith('.html') else self.editor.toPlainText()
            with open(file_path, 'w', encoding='utf-8') as f:
                f.write(content)
            
            self.current_file = file_path
            self.is_modified = False
            self.doc_name.setText(Path(file_path).name)
            play_sound("success.wav")
        except Exception as e:
            QMessageBox.critical(self, "Error", f"Failed to save file: {str(e)}")
    
    def change_font(self, font):
        """Change font family"""
        fmt = QTextCharFormat()
        fmt.setFontFamily(font.family())
        self.merge_format(fmt)
    
    def change_font_size(self, size):
        """Change font size"""
        fmt = QTextCharFormat()
        fmt.setFontPointSize(size)
        self.merge_format(fmt)
    
    def toggle_bold(self):
        """Toggle bold formatting"""
        fmt = QTextCharFormat()
        fmt.setFontWeight(QFont.Weight.Bold if not self.bold_btn.isChecked() else QFont.Weight.Normal)
        self.merge_format(fmt)
    
    def toggle_italic(self):
        """Toggle italic formatting"""
        fmt = QTextCharFormat()
        fmt.setFontItalic(self.italic_btn.isChecked())
        self.merge_format(fmt)
    
    def toggle_underline(self):
        """Toggle underline formatting"""
        fmt = QTextCharFormat()
        fmt.setFontUnderline(self.underline_btn.isChecked())
        self.merge_format(fmt)
    
    def change_text_color(self):
        """Change text color"""
        color = QColorDialog.getColor()
        if color.isValid():
            fmt = QTextCharFormat()
            fmt.setForeground(color)
            self.merge_format(fmt)
    
    def merge_format(self, fmt):
        """Apply format to selection"""
        cursor = self.editor.textCursor()
        if not cursor.hasSelection():
            cursor.select(QTextCursor.SelectionType.WordUnderCursor)
        cursor.mergeCharFormat(fmt)
        self.editor.mergeCurrentCharFormat(fmt)
    
    def set_alignment(self, alignment):
        """Set text alignment"""
        self.editor.setAlignment(alignment)
    
    def insert_bullet_list(self):
        """Insert bullet list"""
        cursor = self.editor.textCursor()
        cursor.insertList(QTextListFormat.Style.ListDisc)
    
    def insert_numbered_list(self):
        """Insert numbered list"""
        cursor = self.editor.textCursor()
        cursor.insertList(QTextListFormat.Style.ListDecimal)
    
    def insert_table(self):
        """Insert table"""
        cursor = self.editor.textCursor()
        table_format = QTextTableFormat()
        table_format.setBorder(1)
        table_format.setCellPadding(10)
        cursor.insertTable(3, 3, table_format)
    
    def insert_image(self):
        """Insert image"""
        file_path, _ = QFileDialog.getOpenFileName(
            self, "Insert Image", "",
            "Images (*.png *.jpg *.jpeg *.gif *.bmp);;All Files (*.*)"
        )
        
        if file_path:
            cursor = self.editor.textCursor()
            cursor.insertImage(file_path)
    
    def print_document(self):
        """Print document"""
        printer = QPrinter(QPrinter.PrinterMode.HighResolution)
        dialog = QPrintDialog(printer, self)
        
        if dialog.exec() == QPrintDialog.DialogCode.Accepted:
            self.editor.document().print(printer)
            play_sound("success.wav")