"""
YouOS Notes App
A beautiful note-taking application with rich features
"""

import sys
import json
from pathlib import Path
from datetime import datetime
from PyQt6.QtWidgets import (QApplication, QWidget, QVBoxLayout, QHBoxLayout, 
                              QLabel, QPushButton, QTextEdit, QLineEdit, 
                              QScrollArea, QFrame, QMessageBox, QComboBox)
from PyQt6.QtCore import Qt, QTimer, pyqtSignal
from PyQt6.QtGui import QFont, QTextCharFormat, QColor

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

# Get the app directory
APP_DIR = Path(__file__).parent
NOTES_FILE = APP_DIR / "notes.json"


class GlassFrame(QFrame):
    """Glassmorphic frame effect"""
    def __init__(self, parent=None, opacity=0.15):
        super().__init__(parent)
        self.setStyleSheet(f"""
            QFrame {{
                background: rgba(255, 255, 255, {opacity});
                border: 1px solid rgba(255, 255, 255, 0.2);
                border-radius: 16px;
            }}
        """)


class NoteCard(GlassFrame):
    """Individual note card widget"""
    
    clicked = pyqtSignal(dict)
    deleted = pyqtSignal(dict)
    
    def __init__(self, note_data, parent=None):
        super().__init__(parent, opacity=0.1)
        self.note_data = note_data
        self.setup_ui()
    
    def setup_ui(self):
        layout = QVBoxLayout(self)
        layout.setContentsMargins(20, 15, 20, 15)
        layout.setSpacing(10)
        
        # Header
        header_layout = QHBoxLayout()
        
        title_label = QLabel(self.note_data['title'])
        title_label.setStyleSheet(f"""
            color: {COLORS['text_primary']};
            font-size: 16px;
            font-weight: bold;
        """)
        title_label.setWordWrap(True)
        header_layout.addWidget(title_label, stretch=1)
        
        # Category badge
        category = self.note_data.get('category', 'General')
        category_colors = {
            'Personal': '#10b981',
            'Work': '#3b82f6',
            'Ideas': '#f59e0b',
            'Todo': '#ef4444',
            'General': '#9ca3af'
        }
        category_label = QLabel(category)
        category_label.setStyleSheet(f"""
            background: {category_colors.get(category, '#9ca3af')};
            color: white;
            padding: 4px 12px;
            border-radius: 12px;
            font-size: 11px;
            font-weight: bold;
        """)
        header_layout.addWidget(category_label)
        
        layout.addLayout(header_layout)
        
        # Content preview
        content_preview = self.note_data['content'][:150]
        if len(self.note_data['content']) > 150:
            content_preview += "..."
        
        content_label = QLabel(content_preview)
        content_label.setStyleSheet(f"""
            color: {COLORS['text_secondary']};
            font-size: 13px;
        """)
        content_label.setWordWrap(True)
        layout.addWidget(content_label)
        
        # Footer
        footer_layout = QHBoxLayout()
        
        date_label = QLabel(f"📅 {self.note_data['date']}")
        date_label.setStyleSheet(f"""
            color: {COLORS['text_secondary']};
            font-size: 11px;
        """)
        footer_layout.addWidget(date_label)
        
        footer_layout.addStretch()
        
        # Buttons
        edit_btn = QPushButton("✏️ Edit")
        edit_btn.setFixedHeight(30)
        edit_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        edit_btn.setStyleSheet(f"""
            QPushButton {{
                background: {COLORS['accent_primary']};
                color: white;
                border: none;
                border-radius: 6px;
                font-size: 12px;
                font-weight: bold;
                padding: 0 15px;
            }}
            QPushButton:hover {{
                background: {COLORS['accent_hover']};
            }}
        """)
        edit_btn.clicked.connect(lambda: self.clicked.emit(self.note_data))
        footer_layout.addWidget(edit_btn)
        
        delete_btn = QPushButton("🗑️")
        delete_btn.setFixedSize(30, 30)
        delete_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        delete_btn.setStyleSheet(f"""
            QPushButton {{
                background: {COLORS['error']};
                color: white;
                border: none;
                border-radius: 6px;
                font-size: 14px;
            }}
            QPushButton:hover {{
                background: #dc2626;
            }}
        """)
        delete_btn.clicked.connect(lambda: self.deleted.emit(self.note_data))
        footer_layout.addWidget(delete_btn)
        
        layout.addLayout(footer_layout)


class NotesApp(QWidget):
    """Main Notes Application Window"""
    
    def __init__(self):
        super().__init__()
        self.notes = []
        self.current_note = None
        self.load_notes()
        self.setup_ui()
        self.setWindowTitle("📝 Notes - YouOS")
        self.resize(1100, 700)
        
        # Auto-save timer
        self.autosave_timer = QTimer()
        self.autosave_timer.timeout.connect(self.autosave_current_note)
        self.autosave_timer.start(5000)  # Auto-save every 5 seconds
    
    def setup_ui(self):
        layout = QHBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(0)
        
        # Left sidebar
        sidebar = self.create_sidebar()
        layout.addWidget(sidebar)
        
        # Main content area
        main_content = self.create_main_content()
        layout.addWidget(main_content, stretch=1)
        
        # Apply dark theme
        self.setStyleSheet(f"""
            QWidget {{
                background: {COLORS['bg_primary']};
                color: {COLORS['text_primary']};
            }}
        """)
    
    def create_sidebar(self):
        sidebar = QFrame()
        sidebar.setFixedWidth(350)
        sidebar.setStyleSheet(f"""
            QFrame {{
                background: {COLORS['bg_secondary']};
                border-right: 1px solid {COLORS['border']};
            }}
        """)
        
        layout = QVBoxLayout(sidebar)
        layout.setContentsMargins(20, 20, 20, 20)
        layout.setSpacing(15)
        
        # Header
        header = QLabel("📝 My Notes")
        header.setStyleSheet(f"""
            color: {COLORS['text_primary']};
            font-size: 24px;
            font-weight: bold;
        """)
        layout.addWidget(header)
        
        # New note button
        new_note_btn = QPushButton("➕ New Note")
        new_note_btn.setFixedHeight(45)
        new_note_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        new_note_btn.setStyleSheet(f"""
            QPushButton {{
                background: {COLORS['accent_primary']};
                color: white;
                border: none;
                border-radius: 10px;
                font-size: 14px;
                font-weight: bold;
            }}
            QPushButton:hover {{
                background: {COLORS['accent_hover']};
            }}
        """)
        new_note_btn.clicked.connect(self.create_new_note)
        layout.addWidget(new_note_btn)
        
        # Search bar
        self.search_input = QLineEdit()
        self.search_input.setPlaceholderText("🔍 Search notes...")
        self.search_input.setFixedHeight(40)
        self.search_input.setStyleSheet(f"""
            QLineEdit {{
                background: {COLORS['bg_tertiary']};
                border: 1px solid {COLORS['border']};
                border-radius: 8px;
                color: {COLORS['text_primary']};
                padding: 0 15px;
                font-size: 13px;
            }}
            QLineEdit:focus {{
                border: 1px solid {COLORS['accent_primary']};
            }}
        """)
        self.search_input.textChanged.connect(self.filter_notes)
        layout.addWidget(self.search_input)
        
        # Filter by category
        filter_layout = QHBoxLayout()
        filter_label = QLabel("Filter:")
        filter_label.setStyleSheet(f"color: {COLORS['text_secondary']}; font-size: 12px;")
        filter_layout.addWidget(filter_label)
        
        self.category_filter = QComboBox()
        self.category_filter.addItems(['All', 'Personal', 'Work', 'Ideas', 'Todo', 'General'])
        self.category_filter.setStyleSheet(f"""
            QComboBox {{
                background: {COLORS['bg_tertiary']};
                border: 1px solid {COLORS['border']};
                border-radius: 6px;
                color: {COLORS['text_primary']};
                padding: 5px 10px;
                font-size: 12px;
            }}
            QComboBox::drop-down {{
                border: none;
            }}
            QComboBox QAbstractItemView {{
                background: {COLORS['bg_tertiary']};
                color: {COLORS['text_primary']};
                selection-background-color: {COLORS['accent_primary']};
            }}
        """)
        self.category_filter.currentTextChanged.connect(self.filter_notes)
        filter_layout.addWidget(self.category_filter, stretch=1)
        layout.addLayout(filter_layout)
        
        # Notes list
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setStyleSheet("""
            QScrollArea {
                border: none;
                background: transparent;
            }
            QScrollBar:vertical {
                background: rgba(255, 255, 255, 0.05);
                width: 8px;
                border-radius: 4px;
            }
            QScrollBar::handle:vertical {
                background: rgba(59, 130, 246, 0.5);
                border-radius: 4px;
            }
        """)
        
        self.notes_container = QWidget()
        self.notes_layout = QVBoxLayout(self.notes_container)
        self.notes_layout.setSpacing(10)
        self.notes_layout.addStretch()
        
        scroll.setWidget(self.notes_container)
        layout.addWidget(scroll, stretch=1)
        
        # Stats
        self.stats_label = QLabel()
        self.stats_label.setStyleSheet(f"""
            color: {COLORS['text_secondary']};
            font-size: 11px;
            padding: 10px;
            background: {COLORS['bg_tertiary']};
            border-radius: 6px;
        """)
        layout.addWidget(self.stats_label)
        self.update_stats()
        
        return sidebar
    
    def create_main_content(self):
        content = QFrame()
        layout = QVBoxLayout(content)
        layout.setContentsMargins(30, 30, 30, 30)
        layout.setSpacing(20)
        
        # Note editor header
        editor_header = QHBoxLayout()
        
        self.title_input = QLineEdit()
        self.title_input.setPlaceholderText("Note Title...")
        self.title_input.setFixedHeight(50)
        self.title_input.setStyleSheet(f"""
            QLineEdit {{
                background: transparent;
                border: none;
                border-bottom: 2px solid {COLORS['border']};
                color: {COLORS['text_primary']};
                font-size: 24px;
                font-weight: bold;
                padding: 5px;
            }}
            QLineEdit:focus {{
                border-bottom: 2px solid {COLORS['accent_primary']};
            }}
        """)
        editor_header.addWidget(self.title_input, stretch=1)
        
        # Category selector
        self.category_combo = QComboBox()
        self.category_combo.addItems(['General', 'Personal', 'Work', 'Ideas', 'Todo'])
        self.category_combo.setFixedWidth(120)
        self.category_combo.setStyleSheet(f"""
            QComboBox {{
                background: {COLORS['bg_secondary']};
                border: 1px solid {COLORS['border']};
                border-radius: 8px;
                color: {COLORS['text_primary']};
                padding: 8px 12px;
                font-size: 13px;
            }}
            QComboBox::drop-down {{
                border: none;
            }}
            QComboBox QAbstractItemView {{
                background: {COLORS['bg_secondary']};
                color: {COLORS['text_primary']};
                selection-background-color: {COLORS['accent_primary']};
            }}
        """)
        editor_header.addWidget(self.category_combo)
        
        layout.addLayout(editor_header)
        
        # Text editor
        self.text_editor = QTextEdit()
        self.text_editor.setPlaceholderText("Start writing your note here...")
        self.text_editor.setStyleSheet(f"""
            QTextEdit {{
                background: {COLORS['bg_secondary']};
                border: 1px solid {COLORS['border']};
                border-radius: 12px;
                color: {COLORS['text_primary']};
                font-size: 14px;
                padding: 20px;
                line-height: 1.6;
            }}
        """)
        layout.addWidget(self.text_editor, stretch=1)
        
        # Action buttons
        actions_layout = QHBoxLayout()
        
        self.last_saved_label = QLabel("📝 Auto-saving enabled")
        self.last_saved_label.setStyleSheet(f"""
            color: {COLORS['text_secondary']};
            font-size: 12px;
        """)
        actions_layout.addWidget(self.last_saved_label)
        
        actions_layout.addStretch()
        
        save_btn = QPushButton("💾 Save Note")
        save_btn.setFixedHeight(40)
        save_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        save_btn.setStyleSheet(f"""
            QPushButton {{
                background: {COLORS['success']};
                color: white;
                border: none;
                border-radius: 8px;
                font-size: 13px;
                font-weight: bold;
                padding: 0 25px;
            }}
            QPushButton:hover {{
                background: #059669;
            }}
        """)
        save_btn.clicked.connect(self.save_current_note)
        actions_layout.addWidget(save_btn)
        
        layout.addLayout(actions_layout)
        
        # Show placeholder if no note selected
        self.show_empty_state()
        
        return content
    
    def show_empty_state(self):
        self.title_input.setEnabled(False)
        self.category_combo.setEnabled(False)
        self.text_editor.setEnabled(False)
        self.title_input.clear()
        self.text_editor.clear()
        self.text_editor.setPlaceholderText("Select a note or create a new one to start writing...")
    
    def create_new_note(self):
        note = {
            'id': datetime.now().strftime('%Y%m%d%H%M%S%f'),
            'title': 'Untitled Note',
            'content': '',
            'category': 'General',
            'date': datetime.now().strftime('%Y-%m-%d %H:%M')
        }
        self.notes.insert(0, note)
        self.current_note = note
        self.save_notes()
        self.refresh_notes_list()
        self.load_note_to_editor(note)
        self.title_input.setFocus()
    
    def load_note_to_editor(self, note):
        self.current_note = note
        self.title_input.setEnabled(True)
        self.category_combo.setEnabled(True)
        self.text_editor.setEnabled(True)
        
        self.title_input.setText(note['title'])
        self.text_editor.setText(note['content'])
        self.category_combo.setCurrentText(note.get('category', 'General'))
    
    def save_current_note(self):
        if not self.current_note:
            return
        
        self.current_note['title'] = self.title_input.text() or 'Untitled Note'
        self.current_note['content'] = self.text_editor.toPlainText()
        self.current_note['category'] = self.category_combo.currentText()
        self.current_note['date'] = datetime.now().strftime('%Y-%m-%d %H:%M')
        
        self.save_notes()
        self.refresh_notes_list()
        self.last_saved_label.setText(f"✓ Saved at {datetime.now().strftime('%H:%M:%S')}")
        QTimer.singleShot(2000, lambda: self.last_saved_label.setText("📝 Auto-saving enabled"))
    
    def autosave_current_note(self):
        if self.current_note and (self.title_input.text() or self.text_editor.toPlainText()):
            self.save_current_note()
    
    def delete_note(self, note):
        reply = QMessageBox.question(
            self,
            "Delete Note",
            f"Are you sure you want to delete '{note['title']}'?",
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No
        )
        
        if reply == QMessageBox.StandardButton.Yes:
            self.notes.remove(note)
            if self.current_note == note:
                self.current_note = None
                self.show_empty_state()
            self.save_notes()
            self.refresh_notes_list()
    
    def filter_notes(self):
        search_text = self.search_input.text().lower()
        category = self.category_filter.currentText()
        
        filtered_notes = self.notes
        
        if category != 'All':
            filtered_notes = [n for n in filtered_notes if n.get('category') == category]
        
        if search_text:
            filtered_notes = [n for n in filtered_notes 
                            if search_text in n['title'].lower() or 
                               search_text in n['content'].lower()]
        
        self.refresh_notes_list(filtered_notes)
    
    def refresh_notes_list(self, notes_to_show=None):
        # Clear existing cards
        for i in reversed(range(self.notes_layout.count())):
            widget = self.notes_layout.itemAt(i).widget()
            if widget:
                widget.deleteLater()
        
        notes = notes_to_show if notes_to_show is not None else self.notes
        
        if not notes:
            empty_label = QLabel("No notes found\nCreate your first note!")
            empty_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
            empty_label.setStyleSheet(f"""
                color: {COLORS['text_secondary']};
                font-size: 14px;
            """)
            self.notes_layout.insertWidget(0, empty_label)
        else:
            for note in notes:
                card = NoteCard(note)
                card.clicked.connect(self.load_note_to_editor)
                card.deleted.connect(self.delete_note)
                self.notes_layout.insertWidget(self.notes_layout.count() - 1, card)
        
        self.update_stats()
    
    def update_stats(self):
        total = len(self.notes)
        total_words = sum(len(n['content'].split()) for n in self.notes)
        self.stats_label.setText(f"📊 {total} notes • {total_words} words")
    
    def load_notes(self):
        if NOTES_FILE.exists():
            try:
                with open(NOTES_FILE, 'r') as f:
                    self.notes = json.load(f)
            except:
                self.notes = []
        else:
            self.notes = []
    
    def save_notes(self):
        with open(NOTES_FILE, 'w') as f:
            json.dump(self.notes, f, indent=4)
    
    def closeEvent(self, event):
        if self.current_note:
            self.save_current_note()
        event.accept()


if __name__ == "__main__":
    app = QApplication(sys.argv)
    window = NotesApp()
    window.show()
    sys.exit(app.exec())