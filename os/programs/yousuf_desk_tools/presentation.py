"""
Yousuf Desk Tools - Presentation Module
presentation.py - Full-featured presentation application with slideshow
"""

import json
from pathlib import Path
from PyQt6.QtWidgets import (QWidget, QVBoxLayout, QHBoxLayout, QTextEdit, 
                              QPushButton, QLabel, QListWidget, QFileDialog,
                              QMessageBox, QFrame, QStackedWidget, QColorDialog,
                              QListWidgetItem, QComboBox, QFontComboBox, QSpinBox,
                              QApplication, QScrollArea)
from PyQt6.QtCore import Qt, pyqtSignal, QTimer, QPropertyAnimation, QRect
from PyQt6.QtGui import QFont, QColor, QTextCharFormat, QKeySequence

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
    'accent_slides': '#dc2626',
    'text_primary': '#ffffff',
    'text_secondary': '#9ca3af',
    'border': '#374151',
}

DEFAULT_SLIDE_BG = '#ffffff'
DEFAULT_TEXT_COLOR = '#000000'


class Slide:
    """Represents a single slide"""
    
    def __init__(self, title="", content="", bg_color=DEFAULT_SLIDE_BG):
        self.title = title
        self.content = content
        self.bg_color = bg_color
    
    def to_dict(self):
        return {
            'title': self.title,
            'content': self.content,
            'bg_color': self.bg_color
        }
    
    @staticmethod
    def from_dict(data):
        return Slide(
            data.get('title', ''),
            data.get('content', ''),
            data.get('bg_color', DEFAULT_SLIDE_BG)
        )


class SlidePreview(QFrame):
    """Preview widget for a slide in the sidebar"""
    
    clicked = pyqtSignal(int)
    
    def __init__(self, index, slide, parent=None):
        super().__init__(parent)
        self.index = index
        self.slide = slide
        self.selected = False
        self.setup_ui()
    
    def setup_ui(self):
        self.setFixedSize(180, 135)
        self.setCursor(Qt.CursorShape.PointingHandCursor)
        
        layout = QVBoxLayout(self)
        layout.setContentsMargins(10, 10, 10, 10)
        layout.setSpacing(5)
        
        # Slide number
        num_label = QLabel(f"Slide {self.index + 1}")
        num_label.setStyleSheet(f"color: {COLORS['text_secondary']}; font-size: 10px;")
        layout.addWidget(num_label)
        
        # Slide preview area
        preview = QFrame()
        preview.setFixedSize(160, 90)
        preview.setStyleSheet(f"""
            QFrame {{
                background: {self.slide.bg_color};
                border: 2px solid {COLORS['border']};
                border-radius: 4px;
            }}
        """)
        
        preview_layout = QVBoxLayout(preview)
        preview_layout.setContentsMargins(5, 5, 5, 5)
        
        title_preview = QLabel(self.slide.title[:20] + "..." if len(self.slide.title) > 20 else self.slide.title)
        title_preview.setStyleSheet("color: black; font-size: 9px; font-weight: bold;")
        title_preview.setWordWrap(True)
        preview_layout.addWidget(title_preview)
        
        content_preview = QLabel(self.slide.content[:30] + "..." if len(self.slide.content) > 30 else self.slide.content)
        content_preview.setStyleSheet("color: #666; font-size: 8px;")
        content_preview.setWordWrap(True)
        preview_layout.addWidget(content_preview)
        preview_layout.addStretch()
        
        layout.addWidget(preview)
        
        self.update_selection_style()
    
    def set_selected(self, selected):
        self.selected = selected
        self.update_selection_style()
    
    def update_selection_style(self):
        if self.selected:
            self.setStyleSheet(f"""
                SlidePreview {{
                    background: rgba(220, 38, 38, 0.2);
                    border: 2px solid {COLORS['accent_slides']};
                    border-radius: 8px;
                }}
            """)
        else:
            self.setStyleSheet(f"""
                SlidePreview {{
                    background: rgba(255, 255, 255, 0.05);
                    border: 1px solid rgba(255, 255, 255, 0.1);
                    border-radius: 8px;
                }}
                SlidePreview:hover {{
                    background: rgba(255, 255, 255, 0.1);
                }}
            """)
    
    def mousePressEvent(self, event):
        self.clicked.emit(self.index)


class SlideshowWindow(QWidget):
    """Fullscreen slideshow window"""
    
    closed = pyqtSignal()
    
    def __init__(self, slides, parent=None):
        super().__init__(parent)
        self.slides = slides
        self.current_index = 0
        self.setWindowFlags(Qt.WindowType.Window | Qt.WindowType.FramelessWindowHint)
        self.setAttribute(Qt.WidgetAttribute.WA_DeleteOnClose)
        self.setup_ui()
        self.showFullScreen()
    
    def setup_ui(self):
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        
        # Slide display
        self.slide_display = QFrame()
        slide_layout = QVBoxLayout(self.slide_display)
        slide_layout.setContentsMargins(100, 80, 100, 80)
        slide_layout.setSpacing(30)
        
        # Title
        self.title_label = QLabel()
        self.title_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.title_label.setWordWrap(True)
        self.title_label.setStyleSheet("font-size: 48px; font-weight: bold;")
        slide_layout.addWidget(self.title_label)
        
        # Content
        self.content_label = QLabel()
        self.content_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.content_label.setWordWrap(True)
        self.content_label.setStyleSheet("font-size: 32px;")
        slide_layout.addWidget(self.content_label)
        
        slide_layout.addStretch()
        
        layout.addWidget(self.slide_display)
        
        # Controls overlay
        controls = QFrame(self)
        controls.setFixedHeight(80)
        controls.setStyleSheet("""
            QFrame {
                background: rgba(0, 0, 0, 0.7);
            }
        """)
        controls_layout = QHBoxLayout(controls)
        controls_layout.setContentsMargins(30, 15, 30, 15)
        
        # Previous button
        prev_btn = QPushButton("◀ Previous")
        prev_btn.setFixedHeight(50)
        prev_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        prev_btn.setStyleSheet("""
            QPushButton {
                background: rgba(255, 255, 255, 0.2);
                color: white;
                border: none;
                border-radius: 8px;
                font-size: 16px;
                padding: 0 30px;
            }
            QPushButton:hover { background: rgba(255, 255, 255, 0.3); }
        """)
        prev_btn.clicked.connect(self.previous_slide)
        controls_layout.addWidget(prev_btn)
        
        # Slide counter
        self.slide_counter = QLabel()
        self.slide_counter.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.slide_counter.setStyleSheet("color: white; font-size: 18px; font-weight: bold;")
        controls_layout.addWidget(self.slide_counter)
        
        # Next button
        next_btn = QPushButton("Next ▶")
        next_btn.setFixedHeight(50)
        next_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        next_btn.setStyleSheet("""
            QPushButton {
                background: rgba(255, 255, 255, 0.2);
                color: white;
                border: none;
                border-radius: 8px;
                font-size: 16px;
                padding: 0 30px;
            }
            QPushButton:hover { background: rgba(255, 255, 255, 0.3); }
        """)
        next_btn.clicked.connect(self.next_slide)
        controls_layout.addWidget(next_btn)
        
        # Exit button
        exit_btn = QPushButton("✕ Exit")
        exit_btn.setFixedHeight(50)
        exit_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        exit_btn.setStyleSheet("""
            QPushButton {
                background: rgba(220, 38, 38, 0.7);
                color: white;
                border: none;
                border-radius: 8px;
                font-size: 16px;
                padding: 0 30px;
            }
            QPushButton:hover { background: rgba(220, 38, 38, 0.9); }
        """)
        exit_btn.clicked.connect(self.close)
        controls_layout.addWidget(exit_btn)
        
        controls.move(0, self.height() - 80)
        controls.resize(self.width(), 80)
        
        self.show_slide(0)
    
    def show_slide(self, index):
        """Display a slide"""
        if 0 <= index < len(self.slides):
            self.current_index = index
            slide = self.slides[index]
            
            self.slide_display.setStyleSheet(f"background: {slide.bg_color};")
            self.title_label.setText(slide.title)
            self.content_label.setText(slide.content)
            self.slide_counter.setText(f"{index + 1} / {len(self.slides)}")
    
    def next_slide(self):
        """Go to next slide"""
        if self.current_index < len(self.slides) - 1:
            self.show_slide(self.current_index + 1)
            play_sound("click.wav")
    
    def previous_slide(self):
        """Go to previous slide"""
        if self.current_index > 0:
            self.show_slide(self.current_index - 1)
            play_sound("click.wav")
    
    def keyPressEvent(self, event):
        """Handle keyboard navigation"""
        if event.key() in (Qt.Key.Key_Right, Qt.Key.Key_Space, Qt.Key.Key_PageDown):
            self.next_slide()
        elif event.key() in (Qt.Key.Key_Left, Qt.Key.Key_PageUp):
            self.previous_slide()
        elif event.key() == Qt.Key.Key_Escape:
            self.close()
        elif event.key() == Qt.Key.Key_Home:
            self.show_slide(0)
        elif event.key() == Qt.Key.Key_End:
            self.show_slide(len(self.slides) - 1)
    
    def closeEvent(self, event):
        self.closed.emit()
        event.accept()


class Presentation(QWidget):
    """Presentation module"""
    
    back_requested = pyqtSignal()
    
    def __init__(self, parent=None):
        super().__init__(parent)
        self.slides = [Slide("Welcome", "Click to edit this slide")]
        self.current_slide_index = 0
        self.current_file = None
        self.is_modified = False
        self.slideshow_window = None
        self.slide_previews = []
        self.setup_ui()
        self.setup_shortcuts()
        self.load_slide(0)
    
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
        
        # Main content area
        content = QFrame()
        content.setStyleSheet(f"background: {COLORS['bg_secondary']};")
        content_layout = QHBoxLayout(content)
        content_layout.setContentsMargins(10, 10, 10, 10)
        content_layout.setSpacing(10)
        
        # Slide thumbnails sidebar
        sidebar = self.create_sidebar()
        content_layout.addWidget(sidebar)
        
        # Editor area
        editor = self.create_editor()
        content_layout.addWidget(editor, stretch=1)
        
        layout.addWidget(content)
        
        # Status bar
        status_bar = self.create_status_bar()
        layout.addWidget(status_bar)
    
    def create_top_bar(self):
        """Create top navigation bar"""
        bar = QFrame()
        bar.setFixedHeight(60)
        bar.setStyleSheet(f"""
            QFrame {{
                background: {COLORS['accent_slides']};
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
        title = QLabel("📽️ Presentation")
        title.setStyleSheet("color: white; font-size: 20px; font-weight: bold; margin-left: 20px;")
        layout.addWidget(title)
        
        layout.addStretch()
        
        # Document name
        self.doc_name = QLabel("Untitled Presentation")
        self.doc_name.setStyleSheet("color: white; font-size: 14px;")
        layout.addWidget(self.doc_name)
        
        return bar
    
    def create_toolbar(self):
        """Create toolbar"""
        toolbar = QFrame()
        toolbar.setFixedHeight(60)
        toolbar.setStyleSheet(f"background: {COLORS['bg_tertiary']};")
        
        layout = QHBoxLayout(toolbar)
        layout.setContentsMargins(20, 10, 20, 10)
        layout.setSpacing(10)
        
        # File operations
        new_btn = self.create_tool_button("📄 New", self.new_presentation)
        layout.addWidget(new_btn)
        
        open_btn = self.create_tool_button("📂 Open", self.open_presentation)
        layout.addWidget(open_btn)
        
        save_btn = self.create_tool_button("💾 Save", self.save_presentation)
        layout.addWidget(save_btn)
        
        layout.addWidget(self.create_separator())
        
        # Slide operations
        add_slide_btn = self.create_tool_button("➕ Slide", self.add_slide)
        layout.addWidget(add_slide_btn)
        
        del_slide_btn = self.create_tool_button("🗑️ Delete", self.delete_slide)
        layout.addWidget(del_slide_btn)
        
        layout.addWidget(self.create_separator())
        
        # Slide background
        bg_btn = self.create_tool_button("🎨 Background", self.change_slide_bg)
        layout.addWidget(bg_btn)
        
        layout.addWidget(self.create_separator())
        
        # Slideshow
        play_btn = QPushButton("▶ Start Slideshow")
        play_btn.setFixedHeight(40)
        play_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        play_btn.setStyleSheet(f"""
            QPushButton {{
                background: {COLORS['accent_slides']};
                color: white;
                border: none;
                border-radius: 8px;
                font-size: 14px;
                font-weight: bold;
                padding: 0 25px;
            }}
            QPushButton:hover {{
                background: #b91c1c;
            }}
        """)
        play_btn.clicked.connect(self.start_slideshow)
        layout.addWidget(play_btn)
        
        layout.addStretch()
        
        return toolbar
    
    def create_sidebar(self):
        """Create slide thumbnails sidebar"""
        sidebar = QFrame()
        sidebar.setFixedWidth(220)
        sidebar.setStyleSheet(f"""
            QFrame {{
                background: {COLORS['bg_tertiary']};
                border-radius: 8px;
            }}
        """)
        
        layout = QVBoxLayout(sidebar)
        layout.setContentsMargins(10, 10, 10, 10)
        
        label = QLabel("Slides")
        label.setStyleSheet(f"color: {COLORS['text_primary']}; font-size: 14px; font-weight: bold;")
        layout.addWidget(label)
        
        # Scroll area for slide previews
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setStyleSheet("QScrollArea { border: none; background: transparent; }")
        
        self.slide_list = QWidget()
        self.slide_list_layout = QVBoxLayout(self.slide_list)
        self.slide_list_layout.setSpacing(10)
        self.slide_list_layout.addStretch()
        
        scroll.setWidget(self.slide_list)
        layout.addWidget(scroll)
        
        self.refresh_slide_list()
        
        return sidebar
    
    def create_editor(self):
        """Create slide editor"""
        editor = QFrame()
        editor.setStyleSheet(f"""
            QFrame {{
                background: {COLORS['bg_tertiary']};
                border-radius: 8px;
            }}
        """)
        
        layout = QVBoxLayout(editor)
        layout.setContentsMargins(20, 20, 20, 20)
        layout.setSpacing(15)
        
        # Slide preview
        self.slide_preview = QFrame()
        self.slide_preview.setStyleSheet(f"""
            QFrame {{
                background: white;
                border: 2px solid {COLORS['border']};
                border-radius: 8px;
            }}
        """)
        preview_layout = QVBoxLayout(self.slide_preview)
        preview_layout.setContentsMargins(40, 40, 40, 40)
        preview_layout.setSpacing(20)
        
        # Title input
        title_label = QLabel("Slide Title:")
        title_label.setStyleSheet("color: black; font-size: 14px; font-weight: bold;")
        preview_layout.addWidget(title_label)
        
        self.title_input = QTextEdit()
        self.title_input.setMaximumHeight(80)
        self.title_input.setStyleSheet("""
            QTextEdit {
                background: white;
                color: black;
                border: 2px solid #e5e7eb;
                border-radius: 4px;
                padding: 10px;
                font-size: 24px;
                font-weight: bold;
            }
        """)
        self.title_input.textChanged.connect(self.on_content_changed)
        preview_layout.addWidget(self.title_input)
        
        # Content input
        content_label = QLabel("Slide Content:")
        content_label.setStyleSheet("color: black; font-size: 14px; font-weight: bold;")
        preview_layout.addWidget(content_label)
        
        self.content_input = QTextEdit()
        self.content_input.setStyleSheet("""
            QTextEdit {
                background: white;
                color: black;
                border: 2px solid #e5e7eb;
                border-radius: 4px;
                padding: 10px;
                font-size: 18px;
            }
        """)
        self.content_input.textChanged.connect(self.on_content_changed)
        preview_layout.addWidget(self.content_input)
        
        layout.addWidget(self.slide_preview)
        
        return editor
    
    def create_status_bar(self):
        """Create status bar"""
        bar = QFrame()
        bar.setFixedHeight(40)
        bar.setStyleSheet(f"background: {COLORS['bg_tertiary']};")
        
        layout = QHBoxLayout(bar)
        layout.setContentsMargins(20, 5, 20, 5)
        
        self.status_label = QLabel("Ready")
        self.status_label.setStyleSheet(f"color: {COLORS['text_secondary']}; font-size: 12px;")
        layout.addWidget(self.status_label)
        
        layout.addStretch()
        
        self.slide_info = QLabel(f"Slide 1 of {len(self.slides)}")
        self.slide_info.setStyleSheet(f"color: {COLORS['text_secondary']}; font-size: 12px;")
        layout.addWidget(self.slide_info)
        
        return bar
    
    def create_tool_button(self, text, callback):
        """Create toolbar button"""
        btn = QPushButton(text)
        btn.setFixedSize(90, 35)
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
                background: rgba(220, 38, 38, 0.3);
            }}
        """)
        btn.clicked.connect(callback)
        return btn
    
    def create_separator(self):
        """Create separator"""
        sep = QFrame()
        sep.setFrameShape(QFrame.Shape.VLine)
        sep.setStyleSheet("background: rgba(255, 255, 255, 0.2); max-width: 1px;")
        return sep
    
    def setup_shortcuts(self):
        """Setup keyboard shortcuts"""
        from PyQt6.QtGui import QShortcut
        
        QShortcut(QKeySequence.StandardKey.New, self, self.new_presentation)
        QShortcut(QKeySequence.StandardKey.Open, self, self.open_presentation)
        QShortcut(QKeySequence.StandardKey.Save, self, self.save_presentation)
        QShortcut(QKeySequence(Qt.Key.Key_F5), self, self.start_slideshow)
    
    def refresh_slide_list(self):
        """Refresh slide preview list"""
        # Clear existing previews
        for preview in self.slide_previews:
            preview.deleteLater()
        self.slide_previews.clear()
        
        # Add new previews
        for i, slide in enumerate(self.slides):
            preview = SlidePreview(i, slide)
            preview.clicked.connect(self.load_slide)
            preview.set_selected(i == self.current_slide_index)
            self.slide_list_layout.insertWidget(i, preview)
            self.slide_previews.append(preview)
    
    def load_slide(self, index):
        """Load a slide for editing"""
        if 0 <= index < len(self.slides):
            self.save_current_slide()
            self.current_slide_index = index
            slide = self.slides[index]
            
            self.title_input.setPlainText(slide.title)
            self.content_input.setPlainText(slide.content)
            self.slide_preview.setStyleSheet(f"""
                QFrame {{
                    background: {slide.bg_color};
                    border: 2px solid {COLORS['border']};
                    border-radius: 8px;
                }}
            """)
            
            # Update selection in sidebar
            for i, preview in enumerate(self.slide_previews):
                preview.set_selected(i == index)
            
            self.slide_info.setText(f"Slide {index + 1} of {len(self.slides)}")
    
    def save_current_slide(self):
        """Save current slide content"""
        if 0 <= self.current_slide_index < len(self.slides):
            slide = self.slides[self.current_slide_index]
            slide.title = self.title_input.toPlainText()
            slide.content = self.content_input.toPlainText()
    
    def on_content_changed(self):
        """Handle content changes"""
        self.is_modified = True
        self.save_current_slide()
    
    def add_slide(self):
        """Add new slide"""
        new_slide = Slide("New Slide", "Enter your content here")
        self.slides.append(new_slide)
        self.refresh_slide_list()
        self.load_slide(len(self.slides) - 1)
        play_sound("click.wav")
    
    def delete_slide(self):
        """Delete current slide"""
        if len(self.slides) > 1:
            reply = QMessageBox.question(
                self, "Delete Slide",
                f"Delete slide {self.current_slide_index + 1}?",
                QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No
            )
            
            if reply == QMessageBox.StandardButton.Yes:
                del self.slides[self.current_slide_index]
                if self.current_slide_index >= len(self.slides):
                    self.current_slide_index = len(self.slides) - 1
                self.refresh_slide_list()
                self.load_slide(self.current_slide_index)
                play_sound("click.wav")
        else:
            QMessageBox.warning(self, "Cannot Delete", "Presentation must have at least one slide")
    
    def change_slide_bg(self):
        """Change slide background color"""
        color = QColorDialog.getColor()
        if color.isValid():
            self.slides[self.current_slide_index].bg_color = color.name()
            self.slide_preview.setStyleSheet(f"""
                QFrame {{
                    background: {color.name()};
                    border: 2px solid {COLORS['border']};
                    border-radius: 8px;
                }}
            """)
            self.refresh_slide_list()
            play_sound("click.wav")
    
    def start_slideshow(self):
        """Start slideshow presentation"""
        self.save_current_slide()
        
        if self.slideshow_window:
            self.slideshow_window.close()
        
        self.slideshow_window = SlideshowWindow(self.slides, self)
        self.slideshow_window.closed.connect(self.on_slideshow_closed)
        play_sound("click.wav")
    
    def on_slideshow_closed(self):
        """Handle slideshow close"""
        self.slideshow_window = None
    
    def new_presentation(self):
        """Create new presentation"""
        if self.is_modified:
            reply = QMessageBox.question(
                self, "Unsaved Changes",
                "Do you want to save changes?",
                QMessageBox.StandardButton.Save | 
                QMessageBox.StandardButton.Discard | 
                QMessageBox.StandardButton.Cancel
            )
            
            if reply == QMessageBox.StandardButton.Save:
                self.save_presentation()
            elif reply == QMessageBox.StandardButton.Cancel:
                return
        
        self.slides = [Slide("Welcome", "New Presentation")]
        self.current_slide_index = 0
        self.current_file = None
        self.is_modified = False
        self.doc_name.setText("Untitled Presentation")
        self.refresh_slide_list()
        self.load_slide(0)
        play_sound("click.wav")
    
    def open_presentation(self):
        """Open presentation"""
        file_path, _ = QFileDialog.getOpenFileName(
            self, "Open Presentation", "",
            "JSON Files (*.json);;All Files (*.*)"
        )
        
        if file_path:
            try:
                with open(file_path, 'r') as f:
                    data = json.load(f)
                    self.slides = [Slide.from_dict(s) for s in data['slides']]
                
                self.current_file = file_path
                self.is_modified = False
                self.doc_name.setText(Path(file_path).name)
                self.current_slide_index = 0
                self.refresh_slide_list()
                self.load_slide(0)
                play_sound("success.wav")
            except Exception as e:
                QMessageBox.critical(self, "Error", f"Failed to open file: {str(e)}")
    def save_presentation(self):
        """Save presentation"""
        self.save_current_slide()
        
        if self.current_file:
            self.save_to_file(self.current_file)
        else:
            self.save_as_presentation()
    
    def save_as_presentation(self):
        """Save presentation as"""
        file_path, _ = QFileDialog.getSaveFileName(
            self, "Save Presentation", "",
            "JSON Files (*.json);;All Files (*.*)"
        )
        
        if file_path:
            self.save_to_file(file_path)
    
    def save_to_file(self, file_path):
        """Save to file"""
        try:
            data = {
                'slides': [s.to_dict() for s in self.slides]
            }
            
            with open(file_path, 'w') as f:
                json.dump(data, f, indent=2)
            
            self.current_file = file_path
            self.is_modified = False
            self.doc_name.setText(Path(file_path).name)
            self.status_label.setText("Presentation saved")
            play_sound("success.wav")
        except Exception as e:
            QMessageBox.critical(self, "Error", f"Failed to save file: {str(e)}")
    
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
                self.save_presentation()
                self.back_requested.emit()
            elif reply == QMessageBox.StandardButton.Discard:
                self.back_requested.emit()
        else:
            self.back_requested.emit()
