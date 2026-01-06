"""
YouOS Paint App
A feature-rich painting application with multiple tools and colors
"""

import sys
from pathlib import Path
from PyQt6.QtWidgets import (QApplication, QWidget, QVBoxLayout, QHBoxLayout,
                              QLabel, QPushButton, QFrame, QFileDialog, 
                              QSlider, QColorDialog, QMessageBox, QButtonGroup)
from PyQt6.QtCore import Qt, QPoint, QRect, pyqtSignal
from PyQt6.QtGui import (QPainter, QPen, QColor, QPixmap, QImage, QPainterPath,
                        QBrush, QLinearGradient, QRadialGradient)

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

APP_DIR = Path(__file__).parent


class GlassFrame(QFrame):
    """Glassmorphic frame"""
    def __init__(self, parent=None, opacity=0.15):
        super().__init__(parent)
        self.setStyleSheet(f"""
            QFrame {{
                background: rgba(255, 255, 255, {opacity});
                border: 1px solid rgba(255, 255, 255, 0.2);
                border-radius: 16px;
            }}
        """)


class Canvas(QWidget):
    """Drawing canvas widget"""
    
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setFixedSize(1000, 600)
        self.image = QImage(1000, 600, QImage.Format.Format_RGB32)
        self.image.fill(Qt.GlobalColor.white)
        
        self.drawing = False
        self.last_point = QPoint()
        
        # Tool settings
        self.tool = "pen"  # pen, eraser, line, rectangle, circle, fill, select
        self.pen_color = QColor(0, 0, 0)
        self.pen_width = 3
        
        # For shape drawing
        self.start_point = None
        self.temp_pixmap = None
        
        # Undo/Redo history
        self.history = []
        self.history_index = -1
        self.max_history = 50
        self.save_state()
        
        # Selection tool
        self.selection_rect = None
        self.selected_image = None
        self.selection_offset = None
        self.is_moving = False
        
        self.setMouseTracking(True)
    
    def paintEvent(self, event):
        painter = QPainter(self)
        painter.drawImage(0, 0, self.image)
        
        # Draw temporary shape while dragging
        if self.temp_pixmap:
            painter.drawPixmap(0, 0, self.temp_pixmap)
        
        # Draw selection rectangle
        if self.selection_rect and self.tool == "select":
            painter.setPen(QPen(QColor(59, 130, 246), 2, Qt.PenStyle.DashLine))
            painter.drawRect(self.selection_rect)
            
            # Draw resize handles
            handles = self.get_selection_handles()
            painter.setBrush(QBrush(QColor(59, 130, 246)))
            for handle in handles:
                painter.drawRect(handle)
        
        # Draw selected image being moved
        if self.selected_image and self.is_moving:
            painter.drawImage(self.selection_rect.topLeft(), self.selected_image)
    
    def mousePressEvent(self, event):
        if event.button() == Qt.MouseButton.LeftButton:
            self.drawing = True
            self.last_point = event.pos()
            self.start_point = event.pos()
            
            if self.tool == "select":
                # Check if clicking inside selection to move
                if self.selection_rect and self.selection_rect.contains(event.pos()):
                    self.is_moving = True
                    self.selection_offset = event.pos() - self.selection_rect.topLeft()
                else:
                    # Start new selection
                    if self.selected_image:
                        self.paste_selection()
                    self.selection_rect = None
                    self.selected_image = None
                    self.temp_pixmap = QPixmap(self.size())
                    self.temp_pixmap.fill(Qt.GlobalColor.transparent)
            elif self.tool in ["line", "rectangle", "circle", "ellipse"]:
                # Store current canvas state for shape preview
                self.temp_pixmap = QPixmap(self.size())
                self.temp_pixmap.fill(Qt.GlobalColor.transparent)
            elif self.tool == "fill":
                self.flood_fill(event.pos())
                self.save_state()
    
    def mouseMoveEvent(self, event):
        if self.drawing and event.buttons() & Qt.MouseButton.LeftButton:
            if self.tool == "pen":
                self.draw_line(self.last_point, event.pos())
                self.last_point = event.pos()
            elif self.tool == "eraser":
                self.erase_area(event.pos())
                self.last_point = event.pos()
            elif self.tool == "select":
                if self.is_moving and self.selection_rect:
                    # Move selection
                    new_pos = event.pos() - self.selection_offset
                    self.selection_rect.moveTo(new_pos)
                    self.update()
                elif self.temp_pixmap:
                    # Drawing selection rectangle
                    self.update_selection_preview(event.pos())
            elif self.tool in ["line", "rectangle", "circle", "ellipse"]:
                # Update preview
                self.update_shape_preview(event.pos())
        
        # Change cursor over selection
        if self.tool == "select" and self.selection_rect:
            if self.selection_rect.contains(event.pos()):
                self.setCursor(Qt.CursorShape.SizeAllCursor)
            else:
                self.setCursor(Qt.CursorShape.CrossCursor)
    
    def mouseReleaseEvent(self, event):
        if event.button() == Qt.MouseButton.LeftButton and self.drawing:
            self.drawing = False
            
            if self.tool == "pen" or self.tool == "eraser":
                self.save_state()
            elif self.tool == "select":
                if self.is_moving:
                    # Finalize move
                    self.is_moving = False
                elif self.temp_pixmap and self.start_point:
                    # Finalize selection rectangle
                    self.finalize_selection(event.pos())
                    self.temp_pixmap = None
            elif self.tool in ["line", "rectangle", "circle", "ellipse"]:
                # Finalize shape on canvas
                self.finalize_shape(event.pos())
                self.temp_pixmap = None
                self.save_state()
            
            self.update()
    
    def draw_line(self, start, end):
        painter = QPainter(self.image)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        pen = QPen(self.pen_color, self.pen_width, Qt.PenStyle.SolidLine, 
                   Qt.PenCapStyle.RoundCap, Qt.PenJoinStyle.RoundJoin)
        painter.setPen(pen)
        painter.drawLine(start, end)
        self.update()
    
    def erase_area(self, pos):
        painter = QPainter(self.image)
        pen = QPen(Qt.GlobalColor.white, self.pen_width * 2, Qt.PenStyle.SolidLine,
                   Qt.PenCapStyle.RoundCap, Qt.PenJoinStyle.RoundJoin)
        painter.setPen(pen)
        painter.drawLine(self.last_point, pos)
        self.update()
    
    def update_shape_preview(self, end_pos):
        if not self.start_point:
            return
        
        # Redraw preview
        self.temp_pixmap = QPixmap(self.size())
        self.temp_pixmap.fill(Qt.GlobalColor.transparent)
        
        painter = QPainter(self.temp_pixmap)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        pen = QPen(self.pen_color, self.pen_width, Qt.PenStyle.SolidLine)
        painter.setPen(pen)
        
        if self.tool == "line":
            painter.drawLine(self.start_point, end_pos)
        elif self.tool == "rectangle":
            rect = QRect(self.start_point, end_pos).normalized()
            painter.drawRect(rect)
        elif self.tool == "circle":
            # Draw circle using bounding square
            rect = QRect(self.start_point, end_pos).normalized()
            size = min(rect.width(), rect.height())
            rect.setWidth(size)
            rect.setHeight(size)
            painter.drawEllipse(rect)
        elif self.tool == "ellipse":
            rect = QRect(self.start_point, end_pos).normalized()
            painter.drawEllipse(rect)
        
        self.update()
    
    def finalize_shape(self, end_pos):
        if not self.start_point:
            return
        
        painter = QPainter(self.image)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        pen = QPen(self.pen_color, self.pen_width, Qt.PenStyle.SolidLine)
        painter.setPen(pen)
        
        if self.tool == "line":
            painter.drawLine(self.start_point, end_pos)
        elif self.tool == "rectangle":
            rect = QRect(self.start_point, end_pos).normalized()
            painter.drawRect(rect)
        elif self.tool == "circle":
            rect = QRect(self.start_point, end_pos).normalized()
            size = min(rect.width(), rect.height())
            rect.setWidth(size)
            rect.setHeight(size)
            painter.drawEllipse(rect)
        elif self.tool == "ellipse":
            rect = QRect(self.start_point, end_pos).normalized()
            painter.drawEllipse(rect)
    
    def flood_fill(self, pos):
        """Simple flood fill algorithm"""
        if not (0 <= pos.x() < self.image.width() and 0 <= pos.y() < self.image.height()):
            return
        
        target_color = self.image.pixelColor(pos.x(), pos.y())
        if target_color == self.pen_color:
            return
        
        painter = QPainter(self.image)
        painter.setPen(self.pen_color)
        
        # Simple flood fill using stack
        stack = [(pos.x(), pos.y())]
        visited = set()
        
        while stack and len(visited) < 50000:  # Limit iterations
            x, y = stack.pop()
            
            if (x, y) in visited:
                continue
            if not (0 <= x < self.image.width() and 0 <= y < self.image.height()):
                continue
            
            current_color = self.image.pixelColor(x, y)
            if current_color != target_color:
                continue
            
            self.image.setPixelColor(x, y, self.pen_color)
            visited.add((x, y))
            
            # Add neighbors
            stack.extend([(x+1, y), (x-1, y), (x, y+1), (x, y-1)])
        
        self.update()
    
    def update_selection_preview(self, end_pos):
        """Update selection rectangle preview"""
        if not self.start_point:
            return
        
        self.temp_pixmap = QPixmap(self.size())
        self.temp_pixmap.fill(Qt.GlobalColor.transparent)
        
        painter = QPainter(self.temp_pixmap)
        pen = QPen(QColor(59, 130, 246), 2, Qt.PenStyle.DashLine)
        painter.setPen(pen)
        
        rect = QRect(self.start_point, end_pos).normalized()
        painter.drawRect(rect)
        
        self.update()
    
    def finalize_selection(self, end_pos):
        """Finalize selection and extract image"""
        if not self.start_point:
            return
        
        self.selection_rect = QRect(self.start_point, end_pos).normalized()
        
        # Extract selected area
        self.selected_image = self.image.copy(self.selection_rect)
        
        # Clear the selected area on main canvas (cut operation)
        painter = QPainter(self.image)
        painter.fillRect(self.selection_rect, Qt.GlobalColor.white)
        painter.end()
        
        self.update()
    
    def paste_selection(self):
        """Paste the selected image back onto canvas"""
        if self.selected_image and self.selection_rect:
            painter = QPainter(self.image)
            painter.drawImage(self.selection_rect.topLeft(), self.selected_image)
            painter.end()
            
            self.selected_image = None
            self.selection_rect = None
            self.save_state()
            self.update()
    
    def get_selection_handles(self):
        """Get resize handle rectangles for selection"""
        if not self.selection_rect:
            return []
        
        handle_size = 8
        rect = self.selection_rect
        
        handles = [
            QRect(rect.left() - handle_size//2, rect.top() - handle_size//2, handle_size, handle_size),
            QRect(rect.right() - handle_size//2, rect.top() - handle_size//2, handle_size, handle_size),
            QRect(rect.left() - handle_size//2, rect.bottom() - handle_size//2, handle_size, handle_size),
            QRect(rect.right() - handle_size//2, rect.bottom() - handle_size//2, handle_size, handle_size),
        ]
        
        return handles
    
    def save_state(self):
        """Save current canvas state to history"""
        # Remove any states after current index
        self.history = self.history[:self.history_index + 1]
        
        # Add current state
        self.history.append(self.image.copy())
        
        # Limit history size
        if len(self.history) > self.max_history:
            self.history.pop(0)
        else:
            self.history_index += 1
        
        if self.history_index >= self.max_history:
            self.history_index = self.max_history - 1
    
    def undo(self):
        """Undo last action"""
        if self.history_index > 0:
            self.history_index -= 1
            self.image = self.history[self.history_index].copy()
            self.selection_rect = None
            self.selected_image = None
            self.update()
            return True
        return False
    
    def redo(self):
        """Redo last undone action"""
        if self.history_index < len(self.history) - 1:
            self.history_index += 1
            self.image = self.history[self.history_index].copy()
            self.selection_rect = None
            self.selected_image = None
            self.update()
            return True
        return False
    
    def clear_canvas(self):
        self.image.fill(Qt.GlobalColor.white)
        self.selection_rect = None
        self.selected_image = None
        self.save_state()
        self.update()
    
    def set_tool(self, tool):
        self.tool = tool
    
    def set_color(self, color):
        self.pen_color = color
    
    def set_pen_width(self, width):
        self.pen_width = width


class ColorButton(QPushButton):
    """Color selection button"""
    
    def __init__(self, color, parent=None):
        super().__init__(parent)
        self.color = color
        self.setFixedSize(40, 40)
        self.setCursor(Qt.CursorShape.PointingHandCursor)
        self.update_style()
    
    def update_style(self):
        self.setStyleSheet(f"""
            QPushButton {{
                background: {self.color.name()};
                border: 2px solid rgba(255, 255, 255, 0.3);
                border-radius: 8px;
            }}
            QPushButton:hover {{
                border: 2px solid rgba(59, 130, 246, 0.8);
            }}
            QPushButton:checked {{
                border: 3px solid #3b82f6;
            }}
        """)


class ToolButton(QPushButton):
    """Tool selection button"""
    
    def __init__(self, icon, tool_name, parent=None):
        super().__init__(icon, parent)
        self.tool_name = tool_name
        self.setFixedSize(50, 50)
        self.setCursor(Qt.CursorShape.PointingHandCursor)
        self.setCheckable(True)
        self.setToolTip(tool_name.title())
        self.update_style()
    
    def update_style(self):
        self.setStyleSheet(f"""
            QPushButton {{
                background: {COLORS['bg_tertiary']};
                border: 1px solid {COLORS['border']};
                border-radius: 10px;
                color: white;
                font-size: 20px;
            }}
            QPushButton:hover {{
                background: {COLORS['accent_primary']};
                border: 1px solid {COLORS['accent_hover']};
            }}
            QPushButton:checked {{
                background: {COLORS['accent_primary']};
                border: 2px solid {COLORS['accent_hover']};
            }}
        """)


class PaintApp(QWidget):
    """Main Paint Application Window"""
    
    def __init__(self):
        super().__init__()
        self.canvas = Canvas()
        self.current_file = None
        self.setup_ui()
        self.setWindowTitle("🎨 Paint - YouOS")
        self.resize(1100, 800)
        
        # Setup keyboard shortcuts
        from PyQt6.QtGui import QShortcut, QKeySequence
        from PyQt6.QtCore import QTimer as QtTimer
        
        undo_shortcut = QShortcut(QKeySequence("Ctrl+Z"), self)
        undo_shortcut.activated.connect(self.undo)
        
        redo_shortcut = QShortcut(QKeySequence("Ctrl+Y"), self)
        redo_shortcut.activated.connect(self.redo)
        
        # Initialize button states
        QtTimer.singleShot(100, self.update_undo_redo_buttons)
    
    def setup_ui(self):
        layout = QVBoxLayout(self)
        layout.setContentsMargins(15, 15, 15, 15)
        layout.setSpacing(15)
        
        # Header with file controls
        header = self.create_header()
        layout.addWidget(header)
        
        # Main content area
        content_layout = QHBoxLayout()
        
        # Left toolbar
        toolbar = self.create_toolbar()
        content_layout.addWidget(toolbar)
        
        # Canvas
        canvas_frame = GlassFrame(opacity=0.05)
        canvas_layout = QVBoxLayout(canvas_frame)
        canvas_layout.setContentsMargins(10, 10, 10, 10)
        canvas_layout.addWidget(self.canvas, alignment=Qt.AlignmentFlag.AlignCenter)
        content_layout.addWidget(canvas_frame, stretch=1)
        
        layout.addLayout(content_layout, stretch=1)
        
        # Apply dark theme
        self.setStyleSheet(f"""
            QWidget {{
                background: {COLORS['bg_primary']};
                color: {COLORS['text_primary']};
            }}
        """)
    
    def create_header(self):
        header = GlassFrame(opacity=0.1)
        header_layout = QHBoxLayout(header)
        header_layout.setContentsMargins(20, 15, 20, 15)
        
        title = QLabel("🎨 Paint")
        title.setStyleSheet(f"""
            color: {COLORS['text_primary']};
            font-size: 24px;
            font-weight: bold;
        """)
        header_layout.addWidget(title)
        
        header_layout.addStretch()
        
        # File buttons
        new_btn = QPushButton("📄 New")
        new_btn.setFixedHeight(40)
        new_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        new_btn.setStyleSheet(self.get_button_style(COLORS['success']))
        new_btn.clicked.connect(self.new_canvas)
        header_layout.addWidget(new_btn)
        
        open_btn = QPushButton("📁 Open")
        open_btn.setFixedHeight(40)
        open_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        open_btn.setStyleSheet(self.get_button_style(COLORS['accent_primary']))
        open_btn.clicked.connect(self.open_file)
        header_layout.addWidget(open_btn)
        
        save_btn = QPushButton("💾 Save")
        save_btn.setFixedHeight(40)
        save_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        save_btn.setStyleSheet(self.get_button_style(COLORS['accent_primary']))
        save_btn.clicked.connect(self.save_file)
        header_layout.addWidget(save_btn)
        
        save_as_btn = QPushButton("💾 Save As")
        save_as_btn.setFixedHeight(40)
        save_as_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        save_as_btn.setStyleSheet(self.get_button_style(COLORS['bg_tertiary']))
        save_as_btn.clicked.connect(self.save_file_as)
        header_layout.addWidget(save_as_btn)
        
        return header
    
    def create_toolbar(self):
        toolbar = GlassFrame(opacity=0.1)
        toolbar.setFixedWidth(180)
        toolbar_layout = QVBoxLayout(toolbar)
        toolbar_layout.setContentsMargins(15, 20, 15, 20)
        toolbar_layout.setSpacing(20)
        
        # Tools section
        tools_label = QLabel("🛠️ Tools")
        tools_label.setStyleSheet(f"""
            color: {COLORS['text_primary']};
            font-size: 16px;
            font-weight: bold;
        """)
        toolbar_layout.addWidget(tools_label)
        
        # Tool buttons
        self.tool_group = QButtonGroup(self)
        tools = [
            ("✏️", "pen"),
            ("🧹", "eraser"),
            ("✂️", "select"),
            ("📏", "line"),
            ("⬜", "rectangle"),
            ("⭕", "circle"),
            ("🥚", "ellipse"),
            ("🪣", "fill"),
        ]
        
        tools_grid = QHBoxLayout()
        tools_grid.setSpacing(10)
        
        for i, (icon, tool) in enumerate(tools):
            btn = ToolButton(icon, tool)
            btn.clicked.connect(lambda checked, t=tool: self.on_tool_changed(t))
            self.tool_group.addButton(btn)
            
            if i % 3 == 0 and i > 0:
                toolbar_layout.addLayout(tools_grid)
                tools_grid = QHBoxLayout()
                tools_grid.setSpacing(10)
            
            tools_grid.addWidget(btn)
            
            if tool == "pen":
                btn.setChecked(True)
        
        toolbar_layout.addLayout(tools_grid)
        
        # Pen width section
        width_label = QLabel("📐 Pen Size")
        width_label.setStyleSheet(f"""
            color: {COLORS['text_primary']};
            font-size: 14px;
            font-weight: bold;
        """)
        toolbar_layout.addWidget(width_label)
        
        self.width_slider = QSlider(Qt.Orientation.Horizontal)
        self.width_slider.setMinimum(1)
        self.width_slider.setMaximum(50)
        self.width_slider.setValue(3)
        self.width_slider.setStyleSheet(f"""
            QSlider::groove:horizontal {{
                background: {COLORS['bg_tertiary']};
                height: 8px;
                border-radius: 4px;
            }}
            QSlider::handle:horizontal {{
                background: {COLORS['accent_primary']};
                width: 18px;
                margin: -5px 0;
                border-radius: 9px;
            }}
        """)
        self.width_slider.valueChanged.connect(self.on_width_changed)
        toolbar_layout.addWidget(self.width_slider)
        
        self.width_value_label = QLabel("3 px")
        self.width_value_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.width_value_label.setStyleSheet(f"""
            color: {COLORS['text_secondary']};
            font-size: 12px;
        """)
        toolbar_layout.addWidget(self.width_value_label)
        
        # Colors section
        colors_label = QLabel("🎨 Colors")
        colors_label.setStyleSheet(f"""
            color: {COLORS['text_primary']};
            font-size: 14px;
            font-weight: bold;
            margin-top: 10px;
        """)
        toolbar_layout.addWidget(colors_label)
        
        # Predefined colors
        self.color_group = QButtonGroup(self)
        colors_grid = QHBoxLayout()
        colors_grid.setSpacing(8)
        
        preset_colors = [
            QColor(0, 0, 0),        # Black
            QColor(255, 0, 0),      # Red
            QColor(0, 255, 0),      # Green
            QColor(0, 0, 255),      # Blue
        ]
        
        for i, color in enumerate(preset_colors):
            btn = ColorButton(color)
            btn.setCheckable(True)
            btn.clicked.connect(lambda checked, c=color: self.canvas.set_color(c))
            self.color_group.addButton(btn)
            colors_grid.addWidget(btn)
            
            if i == 0:
                btn.setChecked(True)
        
        toolbar_layout.addLayout(colors_grid)
        
        colors_grid2 = QHBoxLayout()
        colors_grid2.setSpacing(8)
        
        preset_colors2 = [
            QColor(255, 255, 0),    # Yellow
            QColor(255, 0, 255),    # Magenta
            QColor(0, 255, 255),    # Cyan
            QColor(255, 165, 0),    # Orange
        ]
        
        for color in preset_colors2:
            btn = ColorButton(color)
            btn.setCheckable(True)
            btn.clicked.connect(lambda checked, c=color: self.canvas.set_color(c))
            self.color_group.addButton(btn)
            colors_grid2.addWidget(btn)
        
        toolbar_layout.addLayout(colors_grid2)
        
        # Custom color button
        custom_color_btn = QPushButton("🎨 Custom Color")
        custom_color_btn.setFixedHeight(40)
        custom_color_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        custom_color_btn.setStyleSheet(self.get_button_style(COLORS['bg_tertiary']))
        custom_color_btn.clicked.connect(self.choose_custom_color)
        toolbar_layout.addWidget(custom_color_btn)
        
        # Clear button
        clear_btn = QPushButton("🗑️ Clear Canvas")
        clear_btn.setFixedHeight(40)
        clear_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        clear_btn.setStyleSheet(self.get_button_style(COLORS['error']))
        clear_btn.clicked.connect(self.clear_canvas)
        toolbar_layout.addWidget(clear_btn)
        
        # Undo/Redo buttons
        undo_redo_layout = QHBoxLayout()
        undo_redo_layout.setSpacing(8)
        
        self.undo_btn = QPushButton("↶")
        self.undo_btn.setFixedSize(40, 40)
        self.undo_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        self.undo_btn.setToolTip("Undo (Ctrl+Z)")
        self.undo_btn.setStyleSheet(f"""
            QPushButton {{
                background: {COLORS['bg_tertiary']};
                border: 1px solid {COLORS['border']};
                border-radius: 8px;
                color: white;
                font-size: 20px;
                font-weight: bold;
            }}
            QPushButton:hover {{
                background: {COLORS['accent_primary']};
            }}
            QPushButton:disabled {{
                background: {COLORS['bg_secondary']};
                color: {COLORS['text_secondary']};
            }}
        """)
        self.undo_btn.clicked.connect(self.undo)
        undo_redo_layout.addWidget(self.undo_btn)
        
        self.redo_btn = QPushButton("↷")
        self.redo_btn.setFixedSize(40, 40)
        self.redo_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        self.redo_btn.setToolTip("Redo (Ctrl+Y)")
        self.redo_btn.setStyleSheet(f"""
            QPushButton {{
                background: {COLORS['bg_tertiary']};
                border: 1px solid {COLORS['border']};
                border-radius: 8px;
                color: white;
                font-size: 20px;
                font-weight: bold;
            }}
            QPushButton:hover {{
                background: {COLORS['accent_primary']};
            }}
            QPushButton:disabled {{
                background: {COLORS['bg_secondary']};
                color: {COLORS['text_secondary']};
            }}
        """)
        self.redo_btn.clicked.connect(self.redo)
        undo_redo_layout.addWidget(self.redo_btn)
        
        toolbar_layout.addLayout(undo_redo_layout)
        
        toolbar_layout.addStretch()
        
        return toolbar
    
    def get_button_style(self, bg_color):
        return f"""
            QPushButton {{
                background: {bg_color};
                color: white;
                border: none;
                border-radius: 8px;
                font-size: 13px;
                font-weight: bold;
                padding: 0 15px;
            }}
            QPushButton:hover {{
                background: {COLORS['accent_hover']};
            }}
        """
    
    def on_tool_changed(self, tool):
        """Handle tool change"""
        # Paste any active selection before changing tools
        if self.canvas.selected_image and tool != "select":
            self.canvas.paste_selection()
        
        self.canvas.set_tool(tool)
        
        # Reset cursor
        if tool == "select":
            self.canvas.setCursor(Qt.CursorShape.CrossCursor)
        else:
            self.canvas.setCursor(Qt.CursorShape.ArrowCursor)
    
    def undo(self):
        """Undo last action"""
        if self.canvas.undo():
            self.update_undo_redo_buttons()
    
    def redo(self):
        """Redo last undone action"""
        if self.canvas.redo():
            self.update_undo_redo_buttons()
    
    def update_undo_redo_buttons(self):
        """Update undo/redo button states"""
        self.undo_btn.setEnabled(self.canvas.history_index > 0)
        self.redo_btn.setEnabled(self.canvas.history_index < len(self.canvas.history) - 1)
    
    def on_width_changed(self, value):
        self.canvas.set_pen_width(value)
        self.width_value_label.setText(f"{value} px")
    
    def choose_custom_color(self):
        color = QColorDialog.getColor()
        if color.isValid():
            self.canvas.set_color(color)
            
            # Uncheck all preset color buttons
            for btn in self.color_group.buttons():
                btn.setChecked(False)
    
    def clear_canvas(self):
        reply = QMessageBox.question(
            self,
            "Clear Canvas",
            "Are you sure you want to clear the entire canvas?\n\nThis action cannot be undone.",
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No
        )
        
        if reply == QMessageBox.StandardButton.Yes:
            self.canvas.clear_canvas()
            self.update_undo_redo_buttons()
    
    def new_canvas(self):
        reply = QMessageBox.question(
            self,
            "New Canvas",
            "Create a new canvas?\n\nUnsaved changes will be lost.",
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No
        )
        
        if reply == QMessageBox.StandardButton.Yes:
            self.canvas.clear_canvas()
            self.current_file = None
            self.update_undo_redo_buttons()
    
    def open_file(self):
        file_path, _ = QFileDialog.getOpenFileName(
            self,
            "Open Image",
            str(APP_DIR),
            "Image Files (*.png *.jpg *.jpeg *.bmp)"
        )
        
        if file_path:
            image = QImage(file_path)
            if not image.isNull():
                # Scale image to canvas size if needed
                if image.width() != 1000 or image.height() != 600:
                    image = image.scaled(1000, 600, Qt.AspectRatioMode.IgnoreAspectRatio,
                                       Qt.TransformationMode.SmoothTransformation)
                
                self.canvas.image = image
                self.canvas.update()
                self.canvas.save_state()
                self.update_undo_redo_buttons()
                self.current_file = file_path
            else:
                QMessageBox.warning(self, "Error", "Failed to load image!")
    
    def save_file(self):
        if self.current_file:
            self.canvas.image.save(self.current_file)
            QMessageBox.information(self, "Saved", f"Image saved to:\n{self.current_file}")
        else:
            self.save_file_as()
    
    def save_file_as(self):
        file_path, _ = QFileDialog.getSaveFileName(
            self,
            "Save Image",
            str(APP_DIR / "untitled.png"),
            "PNG Image (*.png);;JPEG Image (*.jpg);;BMP Image (*.bmp)"
        )
        
        if file_path:
            self.canvas.image.save(file_path)
            self.current_file = file_path
            QMessageBox.information(self, "Saved", f"Image saved to:\n{file_path}")


if __name__ == "__main__":
    app = QApplication(sys.argv)
    window = PaintApp()
    window.show()
    sys.exit(app.exec())
